#include "media.hpp"

#include <algorithm>
#include <cstdint>
#include <cstdio>

#if defined(DECKDS_FFMPEG)
#include "music_identity.hpp"

#include <array>
#include <cctype>
#include <charconv>
#include <chrono>
#include <cmath>
#include <ctime>
#include <iterator>
#include <mutex>
#include <sqlite3.h>
#include <stdexcept>
#include <thread>
#include <unordered_map>
#include <unordered_set>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libswresample/swresample.h>
}
#endif

std::string mediaKey(std::string_view value)
{
    uint64_t hash = 14695981039346656037ULL;
    for (const unsigned char byte : value) {
        hash ^= byte;
        hash *= 1099511628211ULL;
    }
    char digits[17];
    std::snprintf(digits, sizeof(digits), "%016llx", static_cast<unsigned long long>(hash));
    return digits;
}

#if defined(DECKDS_FFMPEG)
namespace {
void closeFormat(AVFormatContext* p) { avformat_close_input(&p); }
void closeCodec(AVCodecContext* p) { avcodec_free_context(&p); }
void closeFrame(AVFrame* p) { av_frame_free(&p); }
void closePacket(AVPacket* p) { av_packet_free(&p); }
void closeResampler(SwrContext* p) { swr_free(&p); }
using Format = std::unique_ptr<AVFormatContext, decltype(&closeFormat)>;

Format openFile(const std::string& path, const std::stop_token* stop = nullptr)
{
    static std::once_flag logSetup;
    std::call_once(logSetup, [] { av_log_set_level(AV_LOG_FATAL); });
    auto* context = avformat_alloc_context();
    if (!context)
        return Format(nullptr, closeFormat);
    if (stop) {
        context->interrupt_callback = {
            [](void* token) { return static_cast<int>(static_cast<const std::stop_token*>(token)->stop_requested()); },
            const_cast<std::stop_token*>(stop),
        };
    }
    AVDictionary* options = nullptr;
    av_dict_set(&options, "protocol_whitelist", "file", 0);
    const int result = avformat_open_input(&context, path.c_str(), nullptr, &options);
    av_dict_free(&options);
    return Format(result < 0 ? nullptr : context, closeFormat);
}

std::string lowercase(std::string value)
{
    for (char& c : value)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return value;
}

const AVDictionaryEntry* metadataTag(const AVFormatContext& file, const AVStream& audio, const char* key)
{
    const auto* value = av_dict_get(audio.metadata, key, nullptr, 0);
    if (!value || !*value->value)
        value = av_dict_get(file.metadata, key, nullptr, 0);
    return value;
}

std::string tag(const AVFormatContext& file, const AVStream& audio, const char* key)
{
    const auto* value = metadataTag(file, audio, key);
    return value ? value->value : "";
}

int positivePrefix(std::string_view value, int fallback = 0)
{
    int number = 0;
    const auto result = std::from_chars(value.data(), value.data() + value.size(), number);
    return result.ec == std::errc{} && number > 0 ? number : fallback;
}

std::string modificationTime(std::filesystem::file_time_type stamp)
{
    const auto time = std::filesystem::file_time_type::clock::to_sys(stamp);
    const auto seconds = std::chrono::floor<std::chrono::seconds>(time);
    const auto epochSeconds = std::chrono::system_clock::to_time_t(seconds);
    std::tm utc{};
#if defined(_WIN32)
    if (gmtime_s(&utc, &epochSeconds))
#else
    if (!gmtime_r(&epochSeconds, &utc))
#endif
        return {};
    char date[32]{};
    const auto length = std::strftime(date, sizeof(date), "%Y-%m-%dT%H:%M:%S", &utc);
    if (!length)
        return {};
    const auto nanos = std::chrono::duration_cast<std::chrono::nanoseconds>(time - seconds).count();
    std::snprintf(date + length, sizeof(date) - length, ".%09lld", static_cast<long long>(nanos));
    return date;
}

bool readMetadata(const std::filesystem::path& path, MediaFile& media, const std::stop_token& stop)
{
    media.path = path.string();
    auto file = openFile(media.path, &stop);
    if (!file)
        return false;

    int index = av_find_best_stream(file.get(), AVMEDIA_TYPE_AUDIO, -1, -1, nullptr, 0);
    if (index < 0 || file->streams[index]->codecpar->sample_rate <= 0 || file->duration <= 0) {
        if (avformat_find_stream_info(file.get(), nullptr) < 0)
            return false;
        index = av_find_best_stream(file.get(), AVMEDIA_TYPE_AUDIO, -1, -1, nullptr, 0);
    }
    if (index < 0)
        return false;
    const auto& stream = *file->streams[index];
    const auto& codec = *stream.codecpar;
    if (!avcodec_find_decoder(codec.codec_id))
        return false;
    const double duration = stream.duration > 0 ? stream.duration * av_q2d(stream.time_base)
                                                : static_cast<double>(file->duration) / AV_TIME_BASE;
    if (duration <= 0 || !std::isfinite(duration) || duration > INT32_MAX)
        return false;

    media.title = tag(*file, stream, "title");
    media.artist = tag(*file, stream, "artist");
    media.album = tag(*file, stream, "album");
    media.albumArtist = tag(*file, stream, "album_artist");
    const bool flac = lowercase(path.extension().string()) == ".flac";
    const bool hasRawAlbum = flac && metadataTag(*file, stream, "album");
    media.identityAlbum = hasRawAlbum ? media.album : musicIdentifier(media.album);
    media.identityArtist = musicIdentifier(media.artist);
    auto albumArtist = musicIdentifier(tag(*file, stream, "albumartist"));
    if (albumArtist.empty())
        albumArtist = musicIdentifier(media.albumArtist);
    media.identityAlbumArtist = albumArtist;
    for (const auto* key : {"album artist", "ensemble", "band"}) {
        if (!media.identityAlbumArtist.empty())
            break;
        media.identityAlbumArtist = musicIdentifier(tag(*file, stream, key));
    }
    if (!hasRawAlbum && media.identityAlbum.empty())
        media.identityAlbum = "Unknown Album";
    if (media.identityArtist.empty()) {
        const auto& fallback = flac ? albumArtist : media.identityAlbumArtist;
        media.identityArtist = fallback.empty() ? "Unknown Artist" : fallback;
    }
    if (media.identityAlbumArtist.empty())
        media.identityAlbumArtist = media.identityArtist;
    if (media.title.empty())
        media.title = path.stem().string();
    if (media.album.empty()) {
        media.album = path.parent_path().filename().string();
        if (media.album.empty())
            media.album = "Unknown Album";
    }
    if (media.artist.empty())
        media.artist = media.albumArtist.empty() ? "Unknown Artist" : media.albumArtist;
    if (media.albumArtist.empty())
        media.albumArtist = media.artist;
    media.number = positivePrefix(tag(*file, stream, "track"));
    media.disc = positivePrefix(tag(*file, stream, "disc"), 1);
    media.year = positivePrefix(std::string_view(tag(*file, stream, "date")).substr(0, 4));
    media.duration = std::max(1, static_cast<int>(std::lround(duration)));
    const std::string_view codecName = avcodec_get_name(codec.codec_id);
    media.format = codecName.starts_with("pcm_") ? "PCM" : std::string(codecName);
    for (char& c : media.format)
        c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    if (const auto* descriptor = avcodec_descriptor_get(codec.codec_id))
        media.lossless = descriptor->props & AV_CODEC_PROP_LOSSLESS;
    if (codec.sample_rate > 0) {
        char text[32];
        std::snprintf(text, sizeof(text), "%.3g kHz", codec.sample_rate / 1000.0);
        media.rate = text;
    }
    const auto bitrate = codec.bit_rate > 0 ? codec.bit_rate : file->bit_rate;
    if (bitrate > 0)
        media.bitrate = std::to_string(bitrate / 1000) + " kbps";
    media.channels = codec.ch_layout.nb_channels;
    if (media.lossless)
        media.bitDepth = codec.bits_per_raw_sample > 0 ? codec.bits_per_raw_sample : codec.bits_per_coded_sample;
    return true;
}

void checkSql(sqlite3* db, int result)
{
    if (result != SQLITE_OK)
        throw std::runtime_error(sqlite3_errmsg(db));
}

class Statement {
public:
    Statement(sqlite3* db, const char* sql) : connection(db)
    {
        sqlite3_stmt* statement = nullptr;
        checkSql(db, sqlite3_prepare_v2(db, sql, -1, &statement, nullptr));
        handle.reset(statement);
    }

    void bind(int index, std::string_view text)
    {
        checkSql(connection, sqlite3_bind_text(handle.get(), index, text.empty() ? "" : text.data(),
                                               static_cast<int>(text.size()), SQLITE_TRANSIENT));
    }
    void bind(int index, sqlite3_int64 value)
    {
        checkSql(connection, sqlite3_bind_int64(handle.get(), index, value));
    }
    bool next()
    {
        const int result = sqlite3_step(handle.get());
        if (result == SQLITE_DONE)
            return false;
        if (result == SQLITE_ROW)
            return true;
        checkSql(connection, result);
        return false;
    }
    void write()
    {
        next();
        checkSql(connection, sqlite3_reset(handle.get()));
    }
    std::string text(int index) const
    {
        const auto* value = sqlite3_column_text(handle.get(), index);
        return value ? std::string(reinterpret_cast<const char*>(value), sqlite3_column_bytes(handle.get(), index)) : "";
    }
    sqlite3_int64 number(int index) const { return sqlite3_column_int64(handle.get(), index); }

private:
    sqlite3* connection;
    std::unique_ptr<sqlite3_stmt, decltype(&sqlite3_finalize)> handle{nullptr, sqlite3_finalize};
};

class MusicIndex {
public:
    MusicIndex(const std::filesystem::path& database, int flags)
    {
        sqlite3* connection = nullptr;
        const auto filename = database.empty() ? std::string(":memory:") : database.string();
        const int result = sqlite3_open_v2(filename.c_str(), &connection, flags, nullptr);
        db.reset(connection);
        if (!db)
            throw std::runtime_error(sqlite3_errstr(result));
        checkSql(db.get(), result);
    }

    void exec(const char* sql) { checkSql(db.get(), sqlite3_exec(db.get(), sql, nullptr, nullptr, nullptr)); }
    Statement query(const char* sql) { return Statement(db.get(), sql); }
    int changes() const { return sqlite3_changes(db.get()); }

    bool hasTable(std::string_view name)
    {
        auto result = query("SELECT 1 FROM sqlite_schema WHERE type='table' AND name=?;");
        result.bind(1, name);
        return result.next();
    }

    void begin(const std::stop_token& stop)
    {
        const auto wait = [](void* token, int attempts) {
            const auto& cancellation = *static_cast<const std::stop_token*>(token);
            if (attempts >= 3000 || cancellation.stop_requested())
                return 0;
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            return 1;
        };
        checkSql(db.get(), sqlite3_busy_handler(db.get(), wait, const_cast<std::stop_token*>(&stop)));
        exec("PRAGMA journal_mode=WAL;");
        exec("BEGIN IMMEDIATE;");
    }

    void create()
    {
        exec(R"sql(
            CREATE TABLE IF NOT EXISTS music_sources(
                id TEXT PRIMARY KEY, root TEXT NOT NULL UNIQUE, original_root TEXT NOT NULL,
                identifier TEXT NOT NULL, id_namespace TEXT NOT NULL, sync_url TEXT,
                enabled INTEGER NOT NULL DEFAULT 1
            );
            CREATE TABLE IF NOT EXISTS music_entries(
                source TEXT NOT NULL, relative TEXT NOT NULL,
                title TEXT NOT NULL, artist TEXT NOT NULL, album TEXT NOT NULL, album_artist TEXT NOT NULL,
                format TEXT NOT NULL, rate TEXT NOT NULL, bitrate TEXT NOT NULL,
                modified TEXT NOT NULL, added TEXT NOT NULL,
                disc INTEGER NOT NULL, number INTEGER NOT NULL, year INTEGER NOT NULL,
                duration INTEGER NOT NULL, channels INTEGER NOT NULL, bit_depth INTEGER NOT NULL,
                bytes INTEGER NOT NULL, mtime INTEGER NOT NULL, lossless INTEGER NOT NULL, missing INTEGER NOT NULL,
                identity_artist TEXT NOT NULL DEFAULT '', identity_album TEXT NOT NULL DEFAULT '',
                identity_album_artist TEXT NOT NULL DEFAULT '',
                PRIMARY KEY(source, relative)
            );
            CREATE TABLE IF NOT EXISTS music_id_aliases(
                old_id TEXT PRIMARY KEY, source TEXT NOT NULL, relative TEXT NOT NULL
            );
            CREATE TABLE IF NOT EXISTS music_namespaces(
                namespace TEXT PRIMARY KEY, source TEXT NOT NULL
            );
        )sql");
        if (!query("SELECT 1 FROM pragma_table_info('music_sources') WHERE name='sync_url';").next())
            exec("ALTER TABLE music_sources ADD COLUMN sync_url TEXT;");
        if (!query("SELECT 1 FROM pragma_table_info('music_sources') WHERE name='enabled';").next())
            exec("ALTER TABLE music_sources ADD COLUMN enabled INTEGER NOT NULL DEFAULT 1;");
    }

    std::vector<MusicFolder> folders()
    {
        auto rows = query("SELECT id,root,original_root,identifier,id_namespace,sync_url,enabled FROM music_sources ORDER BY rowid;");
        std::vector<MusicFolder> result;
        while (rows.next())
            result.push_back({rows.text(0), rows.text(1), rows.text(2), rows.text(3), rows.text(4), rows.text(5), rows.number(6) != 0});
        return result;
    }

    void backup(const std::filesystem::path& destination)
    {
        MusicIndex copy(destination, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE);
        auto* job = sqlite3_backup_init(copy.db.get(), "main", db.get(), "main");
        if (!job)
            throw std::runtime_error(sqlite3_errmsg(copy.db.get()));
        const int result = sqlite3_backup_step(job, -1);
        const int finished = sqlite3_backup_finish(job);
        if (result != SQLITE_DONE)
            throw std::runtime_error(sqlite3_errstr(result));
        checkSql(copy.db.get(), finished);
    }

    MediaLibrary load(const MusicFolder& folder, std::vector<size_t>* staleDates = nullptr)
    {
        auto files = query(R"sql(
            SELECT relative, title, artist, album, album_artist, format, rate, bitrate, modified, added,
                   disc, number, year, duration, channels, bit_depth, bytes, mtime, lossless, missing,
                   identity_artist, identity_album, identity_album_artist
            FROM music_entries WHERE source = ? ORDER BY rowid;
        )sql");
        files.bind(1, folder.id);
        MediaLibrary library;
        while (files.next()) {
            auto& file = library.files.emplace_back();
            const auto relative = files.text(0);
            file.path = (std::filesystem::path(folder.root) / relative).string();
            file.title = files.text(1);
            file.artist = files.text(2);
            file.album = files.text(3);
            file.albumArtist = files.text(4);
            file.format = files.text(5);
            file.rate = files.text(6);
            file.bitrate = files.text(7);
            file.disc = static_cast<int>(files.number(10));
            file.number = static_cast<int>(files.number(11));
            file.year = static_cast<int>(files.number(12));
            file.duration = static_cast<int>(files.number(13));
            file.channels = static_cast<int>(files.number(14));
            file.bitDepth = static_cast<int>(files.number(15));
            file.bytes = static_cast<uintmax_t>(files.number(16));
            file.modificationStamp = files.number(17);
            using FileTime = std::filesystem::file_time_type;
            file.added = modificationTime(FileTime{FileTime::duration{file.modificationStamp}});
            file.modified = file.added.substr(0, 10);
            if (staleDates && (file.added != files.text(9) || file.modified != files.text(8)))
                staleDates->push_back(library.files.size() - 1);
            file.lossless = files.number(18) != 0;
            file.missing = files.number(19) != 0;
            file.identityArtist = files.text(20);
            file.identityAlbum = files.text(21);
            file.identityAlbumArtist = files.text(22);
            assignIds(file, folder, relative);
            library.missing += folder.enabled && file.missing;
        }
        return library;
    }

    static void assignIds(MediaFile& file, const MusicFolder& folder, std::string_view relative)
    {
        file.libraryId = folder.id;
        file.disabled = !folder.enabled;
        if (folder.idNamespace.empty()) {
            file.id = mediaKey(relative);
            return;
        }
        file.id = musicSongId(folder.idNamespace, relative);
        file.artistId = musicArtistId(file.identityArtist.empty() ? file.artist : file.identityArtist);
        file.albumArtistId = musicArtistId(file.identityAlbumArtist.empty() ? file.albumArtist : file.identityAlbumArtist);
        file.albumId = musicAlbumId(file.albumArtistId, file.identityArtist.empty() ? file.album : file.identityAlbum);
    }

private:
    std::unique_ptr<sqlite3, decltype(&sqlite3_close)> db{nullptr, sqlite3_close};
};

void saveMediaFile(Statement& write, std::string_view library, std::string_view relative, const MediaFile& file)
{
    int index = 1;
    for (const std::string_view text : {library, relative, std::string_view(file.title), std::string_view(file.artist),
                                       std::string_view(file.album), std::string_view(file.albumArtist),
                                       std::string_view(file.format), std::string_view(file.rate),
                                       std::string_view(file.bitrate), std::string_view(file.modified),
                                       std::string_view(file.added)})
        write.bind(index++, text);
    for (const int value : {file.disc, file.number, file.year, file.duration, file.channels, file.bitDepth})
        write.bind(index++, value);
    write.bind(index++, static_cast<sqlite3_int64>(file.bytes));
    write.bind(index++, file.modificationStamp);
    write.bind(index++, file.lossless);
    write.bind(index++, file.missing);
    write.bind(index++, file.identityArtist);
    write.bind(index++, file.identityAlbum);
    write.bind(index, file.identityAlbumArtist);
    write.write();
}
}

namespace {
std::filesystem::path canonicalMusicFolder(const std::filesystem::path& path)
{
    if (!path.is_absolute())
        throw std::runtime_error("Enter an absolute path on DeckDS.");
    std::error_code error;
    auto root = std::filesystem::canonical(path, error);
    if (error || !std::filesystem::is_directory(root, error))
        throw std::runtime_error("The music folder is unavailable.");
    std::filesystem::directory_iterator probe(root, error);
    if (error)
        throw std::runtime_error("The music folder could not be read.");
    return root;
}

bool under(const std::filesystem::path& path, const std::filesystem::path& root)
{
    const auto relative = path.lexically_relative(root);
    return !relative.empty() && *relative.begin() != "..";
}

void validateRoot(const std::vector<MusicFolder>& folders, const std::filesystem::path& root, std::string_view except)
{
    for (const auto& folder : folders) {
        if (folder.id != except && (under(root, folder.root) || under(folder.root, root)))
            throw std::runtime_error("Music folders cannot overlap.");
    }
}

void validateIdentifier(MusicIndex& index, const std::vector<MusicFolder>& folders, std::string_view identifier,
                        std::string_view except)
{
    const auto normal = musicNamespace(identifier);
    if (normal.empty() || identifier.size() > 128)
        throw std::runtime_error("Enter a library identifier of up to 128 bytes.");
    for (const auto& folder : folders) {
        if (folder.id != except && musicNamespace(folder.identifier) == normal)
            throw std::runtime_error("That library identifier is already in use.");
    }
    auto reserved = index.query("SELECT 1 FROM music_namespaces WHERE namespace=? AND source<>?;");
    reserved.bind(1, normal);
    reserved.bind(2, except);
    if (reserved.next())
        throw std::runtime_error("That ID namespace belongs to another library.");
}

void reserveNamespace(MusicIndex& index, std::string_view nameSpace, std::string_view library)
{
    auto write = index.query("INSERT OR IGNORE INTO music_namespaces(namespace,source) VALUES(?,?);");
    write.bind(1, nameSpace);
    write.bind(2, library);
    write.write();
}

void insertFolder(MusicIndex& index, const MusicFolder& folder)
{
    auto insert = index.query("INSERT INTO music_sources(id,root,original_root,identifier,id_namespace,sync_url) VALUES(?,?,?,?,?,NULLIF(?,''));");
    insert.bind(1, folder.id);
    insert.bind(2, folder.root);
    insert.bind(3, folder.originalRoot);
    insert.bind(4, folder.identifier);
    insert.bind(5, folder.idNamespace);
    insert.bind(6, folder.syncUrl);
    insert.write();
    if (!folder.idNamespace.empty())
        reserveNamespace(index, folder.idNamespace, folder.id);
}

void migrateMusicSyncUrl(MusicIndex& index, std::string_view previousUrl)
{
    if (previousUrl.empty())
        return;
    // NULL means the source has never had a setting. An explicit empty URL stays cleared.
    auto update = index.query(R"sql(
        UPDATE music_sources SET sync_url=?
        WHERE id=(SELECT id FROM music_sources ORDER BY rowid LIMIT 1) AND sync_url IS NULL;
    )sql");
    update.bind(1, previousUrl);
    update.write();
}

const MusicFolder& requireFolder(const std::vector<MusicFolder>& folders, std::string_view id)
{
    const auto folder = std::ranges::find(folders, id, &MusicFolder::id);
    if (folder == folders.end())
        throw std::runtime_error("That library is unavailable.");
    return *folder;
}

void writeLibrarySettings(MusicIndex& index, std::string_view library, std::string_view identifier,
                          std::optional<std::string_view> syncUrl)
{
    auto update = index.query("UPDATE music_sources SET identifier=?, sync_url=CASE WHEN ? THEN ? ELSE sync_url END WHERE id=?;");
    update.bind(1, identifier);
    update.bind(2, syncUrl.has_value());
    update.bind(3, syncUrl.value_or(std::string_view{}));
    update.bind(4, library);
    update.write();
}

std::string indexedSongId(const MusicFolder& folder, std::string_view relative)
{
    return folder.idNamespace.empty() ? mediaKey(relative) : musicSongId(folder.idNamespace, relative);
}

constexpr auto writeSongAlias = R"sql(
    INSERT INTO music_id_aliases(old_id,source,relative) VALUES(?,?,?)
    ON CONFLICT(old_id) DO UPDATE SET relative=excluded.relative
    WHERE music_id_aliases.source=excluded.source AND music_id_aliases.relative=excluded.relative;
)sql";

void saveSongAlias(MusicIndex& index, Statement& alias, std::string_view oldId,
                   std::string_view library, std::string_view relative)
{
    alias.bind(1, oldId);
    alias.bind(2, library);
    alias.bind(3, relative);
    alias.write();
    if (!index.changes())
        throw std::runtime_error("A previous song ID belongs to a different file.");
}

void saveLibraryNamespace(MusicIndex& index, Statement& update, std::string_view library, std::string_view nameSpace)
{
    reserveNamespace(index, nameSpace, library);
    update.bind(1, nameSpace);
    update.bind(2, library);
    update.write();
}

bool initializeMissingNamespaces(MusicIndex& index)
{
    const auto folders = index.folders();
    if (std::ranges::none_of(folders, [](const auto& folder) { return folder.idNamespace.empty(); }))
        return false;

    auto alias = index.query(writeSongAlias);
    auto update = index.query("UPDATE music_sources SET id_namespace=? WHERE id=?;");
    std::unordered_set<std::string> songIds;
    for (const auto& folder : folders) {
        const bool migrate = folder.idNamespace.empty();
        const auto nameSpace = migrate ? musicNamespace(folder.identifier) : folder.idNamespace;
        if (migrate)
            validateIdentifier(index, folders, folder.identifier, folder.id);
        auto files = index.query("SELECT relative FROM music_entries WHERE source=?;");
        files.bind(1, folder.id);
        while (files.next()) {
            const auto relative = files.text(0);
            if (!songIds.insert(musicSongId(nameSpace, relative)).second)
                throw std::runtime_error("Two files have the same song ID. Check for matching normalized paths.");
            if (migrate)
                saveSongAlias(index, alias, mediaKey(relative), folder.id, relative);
        }
        if (migrate)
            saveLibraryNamespace(index, update, folder.id, nameSpace);
    }
    return true;
}

void appendLibrary(MediaLibrary& catalog, MediaLibrary library)
{
    catalog.files.insert(catalog.files.end(), std::make_move_iterator(library.files.begin()),
                         std::make_move_iterator(library.files.end()));
    catalog.missingPaths.insert(catalog.missingPaths.end(), std::make_move_iterator(library.missingPaths.begin()),
                                std::make_move_iterator(library.missingPaths.end()));
    catalog.skipped += library.skipped;
    catalog.reused += library.reused;
    catalog.probed += library.probed;
    catalog.missing += library.missing;
}

void validateSongIds(const MediaLibrary& library)
{
    std::unordered_set<std::string_view> ids;
    ids.reserve(library.files.size());
    for (const auto& file : library.files) {
        if (!ids.insert(file.id).second)
            throw std::runtime_error("Two files have the same song ID. Check for matching normalized paths.");
    }
}
}

bool mediaSupported() { return true; }

bool initializeMusicIndex(const std::filesystem::path& database, const std::filesystem::path& previousRoot,
                          std::string_view previousSyncUrl)
{
    if (database.empty())
        throw std::runtime_error("DeckDS could not open its library settings directory.");
    MusicIndex index(database, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE);
    const std::stop_token stop;
    index.begin(stop);
    const bool migrate = !index.hasTable("music_sources");
    index.create();
    if (migrate) {
        std::string oldRoot;
        if (index.hasTable("music_folder")) {
            auto query = index.query("SELECT root FROM music_folder WHERE singleton=1;");
            if (query.next())
                oldRoot = query.text(0);
        }
        const auto root = oldRoot.empty() ? previousRoot.string() : oldRoot;
        if (!root.empty()) {
            const auto id = musicLibraryId(root);
            insertFolder(index, {id, root, root, "library1", {}});
            if (!oldRoot.empty() && index.hasTable("music_files")) {
                auto copy = index.query(R"sql(
                    INSERT INTO music_entries(source,relative,title,artist,album,album_artist,format,rate,bitrate,
                                              modified,added,disc,number,year,duration,channels,bit_depth,bytes,mtime,lossless,missing)
                    SELECT ?,relative,title,artist,album,album_artist,format,rate,bitrate,
                             modified,added,disc,number,year,duration,channels,bit_depth,bytes,mtime,lossless,missing
                    FROM music_files;
                )sql");
                copy.bind(1, id);
                copy.write();
            }
        }
    }
    const bool initializedNamespaces = initializeMissingNamespaces(index);
    migrateMusicSyncUrl(index, previousSyncUrl);
    index.exec("COMMIT;");
    return initializedNamespaces;
}

std::vector<MusicFolder> musicFolders(const std::filesystem::path& database)
{
    MusicIndex index(database, SQLITE_OPEN_READONLY);
    return index.folders();
}

MediaLibrary loadMusicIndex(const std::filesystem::path& database)
{
    MusicIndex index(database, SQLITE_OPEN_READONLY);
    index.exec("BEGIN;");
    MediaLibrary catalog;
    for (const auto& folder : index.folders())
        appendLibrary(catalog, index.load(folder));
    validateSongIds(catalog);
    return catalog;
}

MusicIdAliases musicIdAliases(const std::filesystem::path& database)
{
    MusicIndex index(database, SQLITE_OPEN_READONLY);
    auto rows = index.query(R"sql(
        SELECT aliases.old_id, aliases.relative, sources.id_namespace
        FROM music_id_aliases AS aliases
        JOIN music_entries AS entries ON entries.source=aliases.source AND entries.relative=aliases.relative
        JOIN music_sources AS sources ON sources.id=aliases.source;
    )sql");
    MusicIdAliases aliases;
    while (rows.next())
        aliases.emplace(rows.text(0), musicSongId(rows.text(2), rows.text(1)));
    return aliases;
}

void addMusicFolder(const std::filesystem::path& database, const std::filesystem::path& path, std::string_view identifier,
                    std::string_view previousSyncUrl, std::string_view syncUrl)
{
    const auto root = canonicalMusicFolder(path).string();
    MusicIndex index(database, SQLITE_OPEN_READWRITE);
    const std::stop_token stop;
    index.begin(stop);
    const auto folders = index.folders();
    const auto existing = std::ranges::find(folders, root, &MusicFolder::root);
    auto name = musicIdentifier(identifier);
    if (existing != folders.end()) {
        if ((name.empty() || musicNamespace(name) == musicNamespace(existing->identifier)) &&
            (syncUrl.empty() || syncUrl == existing->syncUrl))
            return;
        throw std::runtime_error("That music folder already belongs to a library.");
    }
    validateRoot(folders, root, {});
    if (name.empty()) {
        for (size_t number = 1;; ++number) {
            name = "library" + std::to_string(number);
            auto used = index.query("SELECT 1 FROM music_namespaces WHERE namespace=?;");
            used.bind(1, name);
            if (!used.next() && std::ranges::none_of(folders, [&](const auto& folder) {
                    return musicNamespace(folder.identifier) == name;
                }))
                break;
        }
    }
    const auto id = musicLibraryId(root);
    if (std::ranges::find(folders, id, &MusicFolder::id) != folders.end())
        throw std::runtime_error("That folder created an existing library. Use Relocate to update its location.");
    validateIdentifier(index, folders, name, {});
    insertFolder(index, {id, root, root, name, musicNamespace(name), std::string(syncUrl)});
    migrateMusicSyncUrl(index, previousSyncUrl);
    index.exec("COMMIT;");
}

void renameMusicFolder(const std::filesystem::path& database, std::string_view library, std::string_view identifier,
                       std::optional<std::string_view> syncUrl)
{
    MusicIndex index(database, SQLITE_OPEN_READWRITE);
    const std::stop_token stop;
    index.begin(stop);
    const auto folders = index.folders();
    requireFolder(folders, library);
    const auto name = musicIdentifier(identifier);
    validateIdentifier(index, folders, name, library);
    writeLibrarySettings(index, library, name, syncUrl);
    index.exec("COMMIT;");
}

void setMusicSyncUrl(const std::filesystem::path& database, std::string_view library, std::string_view url)
{
    MusicIndex index(database, SQLITE_OPEN_READWRITE);
    auto update = index.query("UPDATE music_sources SET sync_url=? WHERE id=?;");
    update.bind(1, url);
    update.bind(2, library);
    update.write();
    if (!index.changes())
        throw std::runtime_error("That library is unavailable.");
}

void setMusicFolderEnabled(const std::filesystem::path& database, std::string_view library, bool enabled)
{
    MusicIndex index(database, SQLITE_OPEN_READWRITE);
    auto update = index.query("UPDATE music_sources SET enabled=? WHERE id=?;");
    update.bind(1, enabled);
    update.bind(2, library);
    update.write();
    if (!index.changes())
        throw std::runtime_error("That library is unavailable.");
}

namespace {
constexpr std::array<std::string_view, 13> musicExtensions{
    ".m4a", ".mp3", ".alac", ".aac", ".mp4", ".flac", ".wav", ".aif",
    ".aiff", ".aifc", ".ogg", ".opus", ".wma",
};

std::vector<std::filesystem::directory_entry> collectMusic(const std::filesystem::path& root, std::stop_token stop)
{
    std::vector<std::filesystem::directory_entry> files;
    std::error_code walkError;
    std::filesystem::recursive_directory_iterator it(root, std::filesystem::directory_options::skip_permission_denied, walkError);
    for (std::filesystem::recursive_directory_iterator end; it != end && !walkError && !stop.stop_requested(); it.increment(walkError)) {
        const auto& entry = *it;
        std::error_code error;
        if (entry.symlink_status(error).type() != std::filesystem::file_type::regular)
            continue;
        const auto filename = entry.path().filename().string();
        if (filename.starts_with('.') || std::ranges::find(musicExtensions, lowercase(entry.path().extension().string())) == musicExtensions.end())
            continue;
        files.push_back(entry);
    }
    if (walkError)
        throw std::runtime_error(walkError.message());
    return files;
}

MediaLibrary scanFolder(MusicIndex& index, const MusicFolder& folder, const std::vector<std::filesystem::directory_entry>& candidates,
                        MediaScanProgress& progress, size_t offset, std::stop_token stop)
{
    const std::filesystem::path root(folder.root);
    std::vector<size_t> staleDates;
    auto result = index.load(folder, &staleDates);
    auto update = index.query("UPDATE music_entries SET added=?, modified=? WHERE source=? AND relative=?;");
    for (const auto i : staleDates) {
        if (stop.stop_requested())
            return {};
        const auto& file = result.files[i];
        update.bind(1, file.added);
        update.bind(2, file.modified);
        update.bind(3, folder.id);
        update.bind(4, std::filesystem::path(file.path).lexically_relative(root).generic_string());
        update.write();
    }

    std::unordered_map<std::string, size_t> known;
    known.reserve(result.files.size());
    for (size_t i = 0; i < result.files.size(); ++i)
        known.emplace(result.files[i].path, i);
    std::vector<bool> seen(result.files.size(), false);
    auto upsert = index.query(R"sql(
        INSERT INTO music_entries(source, relative, title, artist, album, album_artist, format, rate, bitrate,
                                modified, added, disc, number, year, duration, channels, bit_depth, bytes, mtime, lossless, missing,
                                identity_artist, identity_album, identity_album_artist)
        VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)
        ON CONFLICT(source, relative) DO UPDATE SET
            title=excluded.title, artist=excluded.artist, album=excluded.album, album_artist=excluded.album_artist,
            format=excluded.format, rate=excluded.rate, bitrate=excluded.bitrate, modified=excluded.modified, added=excluded.added,
            disc=excluded.disc, number=excluded.number, year=excluded.year, duration=excluded.duration,
            channels=excluded.channels, bit_depth=excluded.bit_depth, bytes=excluded.bytes, mtime=excluded.mtime,
            lossless=excluded.lossless, missing=excluded.missing, identity_artist=excluded.identity_artist,
            identity_album=excluded.identity_album, identity_album_artist=excluded.identity_album_artist;
    )sql");
    auto mark = index.query("UPDATE music_entries SET missing=? WHERE source=? AND relative=?;");
    const auto markMissing = [&](MediaFile& file, bool missing) {
        if (file.missing == missing)
            return;
        file.missing = missing;
        mark.bind(1, file.missing);
        mark.bind(2, folder.id);
        mark.bind(3, std::filesystem::path(file.path).lexically_relative(root).generic_string());
        mark.write();
    };
    for (size_t processed = 0; processed < candidates.size() && !stop.stop_requested(); ++processed) {
        progress.checked.store(offset + processed, std::memory_order_release);
        const auto& entry = candidates[processed];
        std::error_code error;
        const auto stamp = entry.last_write_time(error);
        if (error) {
            ++result.skipped;
            continue;
        }
        const auto old = known.find(entry.path().string());
        MediaFile* previous = nullptr;
        if (old != known.end()) {
            seen[old->second] = true;
            previous = &result.files[old->second];
        }
        const auto size = entry.file_size(error);
        if (error) {
            ++result.skipped;
            continue;
        }
        const auto ticks = static_cast<int64_t>(stamp.time_since_epoch().count());
        if (previous && previous->bytes == size && previous->modificationStamp == ticks &&
            (folder.idNamespace.empty() || !previous->identityArtist.empty())) {
            markMissing(*previous, false);
            ++result.reused;
            continue;
        }
        ++result.probed;
        MediaFile media;
        if (!readMetadata(entry.path(), media, stop)) {
            ++result.skipped;
            continue;
        }
        const std::string relative = entry.path().lexically_relative(root).generic_string();
        MusicIndex::assignIds(media, folder, relative);
        media.bytes = size;
        media.modificationStamp = ticks;
        media.added = modificationTime(stamp);
        media.modified = media.added.substr(0, 10);
        saveMediaFile(upsert, folder.id, relative, media);
        if (previous)
            *previous = std::move(media);
        else
            result.files.push_back(std::move(media));
    }
    progress.checked.store(offset + candidates.size(), std::memory_order_release);
    for (size_t i = 0; i < seen.size() && !stop.stop_requested(); ++i) {
        if (seen[i])
            continue;
        auto& file = result.files[i];
        std::error_code error;
        const auto status = std::filesystem::status(file.path, error);
        if (status.type() == std::filesystem::file_type::not_found) {
            markMissing(file, true);
            result.missingPaths.push_back({folder.id, std::filesystem::path(file.path).lexically_relative(root).generic_string()});
        }
    }
    result.missing = std::ranges::count(result.files, true, &MediaFile::missing);
    return result;
}
}

MediaLibrary scanMusic(MediaScanProgress& progress, std::stop_token stop, const std::filesystem::path& database)
{
    MusicIndex index(database, SQLITE_OPEN_READWRITE);
    index.begin(stop);
    const auto folders = index.folders();
    std::vector<std::optional<std::vector<std::filesystem::directory_entry>>> candidates;
    size_t total = 0;
    for (const auto& folder : folders) {
        if (stop.stop_requested())
            return {};
        if (!folder.enabled) {
            candidates.emplace_back(std::nullopt);
            continue;
        }
        std::error_code error;
        std::filesystem::directory_iterator probe(folder.root, error);
        if (error) {
            candidates.emplace_back(std::nullopt);
        } else {
            candidates.emplace_back(collectMusic(folder.root, stop));
            total += candidates.back()->size();
        }
    }
    progress.total.store(total, std::memory_order_relaxed);
    MediaLibrary catalog;
    size_t offset = 0;
    for (size_t i = 0; i < folders.size() && !stop.stop_requested(); ++i) {
        if (candidates[i]) {
            appendLibrary(catalog, scanFolder(index, folders[i], *candidates[i], progress, offset, stop));
            offset += candidates[i]->size();
        } else {
            catalog.unavailable += folders[i].enabled;
            appendLibrary(catalog, index.load(folders[i]));
        }
    }
    if (stop.stop_requested())
        return {};
    for (size_t i = 0; i < folders.size(); ++i) {
        std::error_code error;
        if (candidates[i] && !std::filesystem::is_directory(folders[i].root, error))
            throw std::runtime_error("A music folder became unavailable during the scan.");
    }
    validateSongIds(catalog);
    index.exec("COMMIT;");
    return catalog;
}

std::vector<std::string> removeMissingMusic(std::span<const MusicPath> paths, std::stop_token stop,
                                            const std::filesystem::path& database)
{
    MusicIndex index(database, SQLITE_OPEN_READWRITE);
    index.begin(stop);
    const auto folders = index.folders();
    auto erase = index.query("DELETE FROM music_entries WHERE source=? AND relative=? AND missing=1;");
    auto eraseAlias = index.query("DELETE FROM music_id_aliases WHERE source=? AND relative=?;");
    std::vector<std::string> removed;
    std::unordered_set<std::string> checkedRoots;
    for (const auto& path : paths) {
        if (stop.stop_requested())
            return {};
        const auto& folder = requireFolder(folders, path.library);
        if (!folder.enabled)
            continue;
        std::error_code error;
        if (!std::filesystem::is_directory(folder.root, error))
            continue;
        if (std::filesystem::status(std::filesystem::path(folder.root) / path.relative, error).type() !=
            std::filesystem::file_type::not_found)
            continue;
        erase.bind(1, path.library);
        erase.bind(2, path.relative);
        erase.write();
        if (index.changes()) {
            removed.push_back(indexedSongId(folder, path.relative));
            checkedRoots.insert(folder.root);
            eraseAlias.bind(1, path.library);
            eraseAlias.bind(2, path.relative);
            eraseAlias.write();
        }
    }
    if (stop.stop_requested())
        return {};
    for (const auto& root : checkedRoots) {
        std::error_code error;
        if (!std::filesystem::is_directory(root, error))
            throw std::runtime_error("A music folder became unavailable while removing missing tracks.");
    }
    index.exec("COMMIT;");
    return removed;
}

namespace {
void verifyRunning(std::stop_token stop)
{
    if (stop.stop_requested())
        throw std::runtime_error("Library operation canceled.");
}

MusicRelocation planRelocation(MusicIndex& index, std::string_view library,
                               std::string_view destination, std::stop_token stop,
                               const std::optional<MusicLibrarySettings>& settings)
{
    MusicRelocation plan;
    plan.folders = index.folders();
    const auto& folder = requireFolder(plan.folders, library);
    if (settings) {
        auto identifier = musicIdentifier(settings->identifier);
        validateIdentifier(index, plan.folders, identifier, folder.id);
        plan.settings = MusicLibrarySettings{std::move(identifier), settings->syncUrl};
    }
    const auto root = canonicalMusicFolder(destination);
    if (root == folder.root)
        throw std::runtime_error("Choose a different music folder.");
    validateRoot(plan.folders, root, library);
    plan.library = library;
    plan.destination = root.string();
    auto files = index.query("SELECT relative FROM music_entries WHERE source=? ORDER BY relative;");
    files.bind(1, library);
    while (files.next()) {
        verifyRunning(stop);
        const auto relative = files.text(0);
        const auto candidate = root / relative;
        std::error_code error;
        const auto status = std::filesystem::symlink_status(candidate, error);
        if (error && error != std::errc::no_such_file_or_directory && error != std::errc::not_a_directory)
            throw std::runtime_error("A path in the new folder could not be checked.");
        if (status.type() == std::filesystem::file_type::regular) {
            const auto resolved = std::filesystem::canonical(candidate, error);
            if (error)
                throw std::runtime_error("A path in the new folder could not be checked.");
            if (resolved == candidate && under(resolved, root)) {
                ++plan.matched;
                continue;
            }
        }
        plan.missing.push_back(relative);
    }
    return plan;
}
}

MusicRelocation previewMusicRelocation(const std::filesystem::path& database, std::string_view library,
                                       std::string_view destination, std::stop_token stop,
                                       const std::optional<MusicLibrarySettings>& settings)
{
    MusicIndex index(database, SQLITE_OPEN_READONLY);
    index.exec("BEGIN;");
    return planRelocation(index, library, destination, stop, settings);
}

void relocateMusicLibrary(const std::filesystem::path& database, const MusicRelocation& plan,
                          const std::filesystem::path& backup, std::stop_token stop)
{
    verifyRunning(stop);
    MusicIndex index(database, SQLITE_OPEN_READWRITE);
    index.backup(backup);
    index.begin(stop);
    if (planRelocation(index, plan.library, plan.destination, stop, plan.settings) != plan)
        throw std::runtime_error("The library changed after preview. Open a new preview.");

    auto root = index.query("UPDATE music_sources SET root=? WHERE id=?;");
    root.bind(1, plan.destination);
    root.bind(2, plan.library);
    root.write();
    if (plan.settings)
        writeLibrarySettings(index, plan.library, plan.settings->identifier, plan.settings->syncUrl);
    auto reset = index.query("UPDATE music_entries SET missing=0 WHERE source=?;");
    reset.bind(1, plan.library);
    reset.write();
    auto mark = index.query("UPDATE music_entries SET missing=1 WHERE source=? AND relative=?;");
    for (const auto& path : plan.missing) {
        verifyRunning(stop);
        mark.bind(1, plan.library);
        mark.bind(2, path);
        mark.write();
    }
    verifyRunning(stop);
    index.exec("COMMIT;");
}

void rekeyMusicIndex(const std::filesystem::path& database, const std::filesystem::path& backup,
                     MediaScanProgress& progress, std::stop_token stop)
{
    verifyRunning(stop);
    MusicIndex index(database, SQLITE_OPEN_READWRITE);
    index.backup(backup);
    index.begin(stop);
    const auto folders = index.folders();
    if (folders.empty())
        throw std::runtime_error("Add a music folder first.");
    for (const auto& folder : folders)
        validateIdentifier(index, folders, folder.identifier, folder.id);
    {
        auto count = index.query("SELECT count(*) FROM music_entries;");
        if (count.next())
            progress.total.store(static_cast<size_t>(count.number(0)), std::memory_order_relaxed);
    }

    struct IdentityTags {
        std::string path, artist, album, albumArtist;
    };
    auto alias = index.query(writeSongAlias);
    auto tags = index.query(R"sql(
        UPDATE music_entries SET identity_artist=?, identity_album=?, identity_album_artist=? WHERE source=? AND relative=?;
    )sql");
    auto update = index.query("UPDATE music_sources SET id_namespace=? WHERE id=?;");
    std::unordered_set<std::string> newIds;
    for (const auto& folder : folders) {
        const auto nameSpace = musicNamespace(folder.identifier);
        std::vector<IdentityTags> uncached;
        {
            auto files = index.query("SELECT relative, identity_artist FROM music_entries WHERE source=?;");
            files.bind(1, folder.id);
            while (files.next()) {
                verifyRunning(stop);
                const auto relative = files.text(0);
                const auto before = indexedSongId(folder, relative);
                const auto after = musicSongId(nameSpace, relative);
                if (!newIds.insert(after).second)
                    throw std::runtime_error("Two files have the same song ID. Check for matching normalized paths.");
                if (before != after)
                    saveSongAlias(index, alias, before, folder.id, relative);
                // Indexes created before namespaced IDs can lack the original tag names.
                if (folder.enabled && files.text(1).empty()) {
                    std::error_code error;
                    const auto path = std::filesystem::path(folder.root) / relative;
                    MediaFile file;
                    if (std::filesystem::symlink_status(path, error).type() == std::filesystem::file_type::regular &&
                        readMetadata(path, file, stop))
                        uncached.push_back({relative, std::move(file.identityArtist), std::move(file.identityAlbum),
                                            std::move(file.identityAlbumArtist)});
                }
                progress.checked.fetch_add(1, std::memory_order_release);
            }
        }
        for (const auto& item : uncached) {
            verifyRunning(stop);
            tags.bind(1, item.artist);
            tags.bind(2, item.album);
            tags.bind(3, item.albumArtist);
            tags.bind(4, folder.id);
            tags.bind(5, item.path);
            tags.write();
        }
        saveLibraryNamespace(index, update, folder.id, nameSpace);
    }
    verifyRunning(stop);
    index.exec("COMMIT;");
}

void deleteMusicFolder(const std::filesystem::path& database, std::string_view library,
                       const std::filesystem::path& backup, std::stop_token stop)
{
    verifyRunning(stop);
    MusicIndex index(database, SQLITE_OPEN_READWRITE);
    if (requireFolder(index.folders(), library).enabled)
        throw std::runtime_error("Disable the library before deleting it.");
    index.backup(backup);
    index.begin(stop);
    if (requireFolder(index.folders(), library).enabled)
        throw std::runtime_error("Disable the library before deleting it.");
    for (const auto* sql : {
             "DELETE FROM music_id_aliases WHERE source=?;",
             "DELETE FROM music_entries WHERE source=?;",
             "DELETE FROM music_namespaces WHERE source=?;",
             "DELETE FROM music_sources WHERE id=?;",
         }) {
        verifyRunning(stop);
        auto erase = index.query(sql);
        erase.bind(1, library);
        erase.write();
    }
    verifyRunning(stop);
    index.exec("COMMIT;");
}

struct MediaDecoder::Impl {
    std::stop_token cancellation;
    Format file{nullptr, closeFormat};
    std::unique_ptr<AVCodecContext, decltype(&closeCodec)> codec{nullptr, closeCodec};
    std::unique_ptr<AVFrame, decltype(&closeFrame)> frame{av_frame_alloc(), closeFrame};
    std::unique_ptr<AVPacket, decltype(&closePacket)> packet{av_packet_alloc(), closePacket};
    std::unique_ptr<SwrContext, decltype(&closeResampler)> resampler{nullptr, closeResampler};
    std::vector<float> samples;
    size_t cursor = 0;
    int streamIndex = -1, outputRate;
    int64_t origin = 0;
    double discardUntil, decodedEnd = 0;
    bool ready = false, draining = false, finished = false;

    Impl(std::string_view path, double start, int rate, std::stop_token stop)
        : cancellation(stop), outputRate(rate), discardUntil(start)
    {
        file = openFile(std::string(path), cancellation.stop_possible() ? &cancellation : nullptr);
        if (!file || !frame || !packet)
            return;
        const AVCodec* decoder = nullptr;
        streamIndex = av_find_best_stream(file.get(), AVMEDIA_TYPE_AUDIO, -1, -1, &decoder, 0);
        if ((streamIndex < 0 || (rate <= 0 && file->streams[streamIndex]->codecpar->sample_rate <= 0)) &&
            avformat_find_stream_info(file.get(), nullptr) >= 0)
            streamIndex = av_find_best_stream(file.get(), AVMEDIA_TYPE_AUDIO, -1, -1, &decoder, 0);
        if (streamIndex < 0)
            return;
        const auto& stream = *file->streams[streamIndex];
        codec.reset(avcodec_alloc_context3(decoder));
        if (!codec || avcodec_parameters_to_context(codec.get(), stream.codecpar) < 0)
            return;
        codec->pkt_timebase = stream.time_base;
        if (avcodec_open2(codec.get(), decoder, nullptr) < 0)
            return;
        if (outputRate <= 0)
            outputRate = codec->sample_rate > 0 ? codec->sample_rate : 48000;
        origin = stream.start_time == AV_NOPTS_VALUE ? 0 : stream.start_time;
        if (start > 0 && av_q2d(stream.time_base) > 0) {
            const auto timestamp = origin + static_cast<int64_t>(start / av_q2d(stream.time_base));
            if (av_seek_frame(file.get(), streamIndex, timestamp, AVSEEK_FLAG_BACKWARD) >= 0) {
                avcodec_flush_buffers(codec.get());
                decodedEnd = start;
            }
        }
        ready = true;
    }

    bool convert(const AVFrame* input)
    {
        if (!resampler) {
            if (!input || input->sample_rate <= 0 || input->ch_layout.nb_channels <= 0)
                return false;
            AVChannelLayout stereo = AV_CHANNEL_LAYOUT_STEREO;
            AVChannelLayout layout{};
            if (input->ch_layout.order == AV_CHANNEL_ORDER_UNSPEC)
                av_channel_layout_default(&layout, input->ch_layout.nb_channels);
            else if (av_channel_layout_copy(&layout, &input->ch_layout) < 0)
                return false;
            SwrContext* raw = nullptr;
            const int configured = swr_alloc_set_opts2(&raw, &stereo, AV_SAMPLE_FMT_FLT, outputRate, &layout,
                                                       static_cast<AVSampleFormat>(input->format), input->sample_rate, 0, nullptr);
            av_channel_layout_uninit(&layout);
            resampler.reset(raw);
            if (configured < 0 || !resampler || swr_init(resampler.get()) < 0)
                return false;
        }
        const int inputFrames = input ? input->nb_samples : 0;
        const int capacity = swr_get_out_samples(resampler.get(), inputFrames);
        if (capacity <= 0)
            return false;
        samples.resize(static_cast<size_t>(capacity) * 2);
        auto* output = reinterpret_cast<uint8_t*>(samples.data());
        const int frames = swr_convert(resampler.get(), &output, capacity,
                                       input ? const_cast<const uint8_t**>(input->extended_data) : nullptr, inputFrames);
        if (frames < 0)
            return false;
        samples.resize(static_cast<size_t>(frames) * 2);
        double start = decodedEnd;
        if (input && input->best_effort_timestamp != AV_NOPTS_VALUE)
            start = (input->best_effort_timestamp - origin) * av_q2d(file->streams[streamIndex]->time_base);
        decodedEnd = start + static_cast<double>(frames) / outputRate;
        cursor = static_cast<size_t>(std::clamp(std::ceil((discardUntil - start) * outputRate),
                                                0.0, static_cast<double>(frames))) * 2;
        if (cursor < samples.size())
            discardUntil = -1.0;
        return true;
    }

    bool more()
    {
        while (cursor >= samples.size() && !finished && !cancellation.stop_requested()) {
            const int result = avcodec_receive_frame(codec.get(), frame.get());
            if (result >= 0) {
                finished = !convert(frame.get());
                av_frame_unref(frame.get());
                if (finished)
                    return false;
            } else if (result == AVERROR(EAGAIN) && !draining) {
                int read = av_read_frame(file.get(), packet.get());
                while (read >= 0 && packet->stream_index != streamIndex) {
                    av_packet_unref(packet.get());
                    read = av_read_frame(file.get(), packet.get());
                }
                draining = read < 0;
                const int sent = avcodec_send_packet(codec.get(), draining ? nullptr : packet.get());
                av_packet_unref(packet.get());
                if (sent < 0 && draining)
                    finished = true;
            } else {
                finished = true;
                return convert(nullptr) && cursor < samples.size();
            }
        }
        return cursor < samples.size();
    }
};

std::vector<uint8_t> readEmbeddedArtwork(std::string_view path, size_t maxBytes, std::stop_token stop)
{
    const auto file = openFile(std::string(path), &stop);
    if (file) {
        for (unsigned int i = 0; i < file->nb_streams; ++i) {
            const auto& stream = *file->streams[i];
            const auto& picture = stream.attached_pic;
            if ((stream.disposition & AV_DISPOSITION_ATTACHED_PIC) && picture.data &&
                picture.size > 0 && static_cast<size_t>(picture.size) <= maxBytes)
                return {picture.data, picture.data + picture.size};
        }
    }
    return {};
}

MediaDecoder::MediaDecoder(std::string_view path, double start, int outputRate, std::stop_token stop)
    : impl(std::make_unique<Impl>(path, start, outputRate, stop)) {}
MediaDecoder::~MediaDecoder() = default;
bool MediaDecoder::valid() const { return impl->ready; }
int MediaDecoder::sampleRate() const { return impl->outputRate; }

int MediaDecoder::read(std::span<float> stereo)
{
    size_t copied = 0;
    while (copied + 1 < stereo.size() && impl->more()) {
        const size_t size = std::min(stereo.size() - copied, impl->samples.size() - impl->cursor) & ~size_t(1);
        std::copy_n(impl->samples.data() + impl->cursor, size, stereo.data() + copied);
        impl->cursor += size;
        copied += size;
    }
    return static_cast<int>(copied / 2);
}
#else
bool mediaSupported() { return false; }
bool initializeMusicIndex(const std::filesystem::path&, const std::filesystem::path&, std::string_view) { return false; }
std::vector<MusicFolder> musicFolders(const std::filesystem::path&) { return {}; }
MediaLibrary loadMusicIndex(const std::filesystem::path&) { return {}; }
MusicIdAliases musicIdAliases(const std::filesystem::path&) { return {}; }
void addMusicFolder(const std::filesystem::path&, const std::filesystem::path&, std::string_view, std::string_view, std::string_view) {}
void renameMusicFolder(const std::filesystem::path&, std::string_view, std::string_view, std::optional<std::string_view>) {}
void setMusicSyncUrl(const std::filesystem::path&, std::string_view, std::string_view) {}
void setMusicFolderEnabled(const std::filesystem::path&, std::string_view, bool) {}
void deleteMusicFolder(const std::filesystem::path&, std::string_view, const std::filesystem::path&, std::stop_token) {}
MediaLibrary scanMusic(MediaScanProgress&, std::stop_token, const std::filesystem::path&) { return {}; }
std::vector<std::string> removeMissingMusic(std::span<const MusicPath>, std::stop_token, const std::filesystem::path&) { return {}; }
MusicRelocation previewMusicRelocation(const std::filesystem::path&, std::string_view, std::string_view, std::stop_token,
                                       const std::optional<MusicLibrarySettings>&)
{ return {}; }
void relocateMusicLibrary(const std::filesystem::path&, const MusicRelocation&, const std::filesystem::path&, std::stop_token) {}
void rekeyMusicIndex(const std::filesystem::path&, const std::filesystem::path&, MediaScanProgress&, std::stop_token) {}
std::vector<uint8_t> readEmbeddedArtwork(std::string_view, size_t, std::stop_token) { return {}; }
struct MediaDecoder::Impl {};
MediaDecoder::MediaDecoder(std::string_view, double, int, std::stop_token) {}
MediaDecoder::~MediaDecoder() = default;
bool MediaDecoder::valid() const { return false; }
int MediaDecoder::sampleRate() const { return 0; }
int MediaDecoder::read(std::span<float>) { return 0; }
#endif

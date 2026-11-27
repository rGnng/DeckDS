#include "media.hpp"

#include <algorithm>
#include <cstdint>
#include <cstdio>

#if defined(DECKDS_FFMPEG)
#include <array>
#include <cctype>
#include <charconv>
#include <chrono>
#include <cmath>
#include <ctime>
#include <mutex>
#include <sqlite3.h>
#include <stdexcept>
#include <thread>
#include <unordered_map>

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

std::string tag(const AVFormatContext& file, const AVStream& audio, const char* key)
{
    const auto* value = av_dict_get(audio.metadata, key, nullptr, 0);
    if (!value || !*value->value)
        value = av_dict_get(file.metadata, key, nullptr, 0);
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
        exec(R"sql(
            CREATE TABLE IF NOT EXISTS music_folder(
                singleton INTEGER PRIMARY KEY CHECK(singleton = 1),
                root TEXT NOT NULL
            );
            CREATE TABLE IF NOT EXISTS music_files(
                relative TEXT NOT NULL UNIQUE,
                title TEXT NOT NULL, artist TEXT NOT NULL, album TEXT NOT NULL, album_artist TEXT NOT NULL,
                format TEXT NOT NULL, rate TEXT NOT NULL, bitrate TEXT NOT NULL,
                modified TEXT NOT NULL, added TEXT NOT NULL,
                disc INTEGER NOT NULL, number INTEGER NOT NULL, year INTEGER NOT NULL,
                duration INTEGER NOT NULL, channels INTEGER NOT NULL, bit_depth INTEGER NOT NULL,
                bytes INTEGER NOT NULL, mtime INTEGER NOT NULL, lossless INTEGER NOT NULL, missing INTEGER NOT NULL
            );
        )sql");
    }

    std::optional<MediaLibrary> load(const std::filesystem::path& directory, std::vector<size_t>* staleDates = nullptr)
    {
        auto root = query("SELECT root FROM music_folder WHERE singleton = 1;");
        if (!root.next() || root.text(0) != directory.string())
            return std::nullopt;
        auto files = query(R"sql(
            SELECT relative, title, artist, album, album_artist, format, rate, bitrate, modified, added,
                   disc, number, year, duration, channels, bit_depth, bytes, mtime, lossless, missing
            FROM music_files ORDER BY rowid;
        )sql");
        MediaLibrary library;
        while (files.next()) {
            auto& file = library.files.emplace_back();
            const auto relative = files.text(0);
            file.path = (directory / relative).string();
            file.id = mediaKey(relative);
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
            library.missing += file.missing;
        }
        return library;
    }

private:
    std::unique_ptr<sqlite3, decltype(&sqlite3_close)> db{nullptr, sqlite3_close};
};

void saveMediaFile(Statement& write, std::string_view relative, const MediaFile& file)
{
    int index = 1;
    for (const std::string_view text : {relative, std::string_view(file.title), std::string_view(file.artist),
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
    write.bind(index, file.missing);
    write.write();
}
}

bool mediaSupported() { return true; }

std::optional<MediaLibrary> loadMusicIndex(const std::filesystem::path& directory, const std::filesystem::path& database)
{
    std::error_code error;
    if (database.empty() || !std::filesystem::is_regular_file(database, error))
        return std::nullopt;
    MusicIndex index(database, SQLITE_OPEN_READONLY);
    auto schema = index.query("SELECT 1 FROM sqlite_schema WHERE name = 'music_folder';");
    return schema.next() ? index.load(directory) : std::nullopt;
}

MediaLibrary scanMusic(const std::filesystem::path& directory, MediaScanProgress& progress, std::stop_token stop,
                       const std::filesystem::path& database)
{
    constexpr std::array<std::string_view, 13> extensions{
        ".m4a", ".mp3", ".alac", ".aac", ".mp4", ".flac", ".wav", ".aif",
        ".aiff", ".aifc", ".ogg", ".opus", ".wma",
    };
    std::vector<std::filesystem::directory_entry> candidates;
    std::error_code walkError;
    std::filesystem::recursive_directory_iterator it(directory, std::filesystem::directory_options::skip_permission_denied, walkError);
    const std::filesystem::recursive_directory_iterator end;
    for (; it != end && !stop.stop_requested(); it.increment(walkError)) {
        if (walkError)
            throw std::runtime_error(walkError.message());
        const auto& entry = *it;
        std::error_code error;
        if (entry.symlink_status(error).type() != std::filesystem::file_type::regular)
            continue;
        const auto filename = entry.path().filename().string();
        if (filename.starts_with('.') || std::ranges::find(extensions, lowercase(entry.path().extension().string())) == extensions.end())
            continue;
        candidates.push_back(entry);
    }
    if (stop.stop_requested())
        return {};
    if (walkError)
        throw std::runtime_error(walkError.message());
    progress.total.store(candidates.size(), std::memory_order_relaxed);

    MusicIndex index(database, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE);
    index.begin(stop);
    std::vector<size_t> staleDates;
    auto cached = index.load(directory, &staleDates);
    MediaLibrary result = cached ? std::move(*cached) : MediaLibrary{};
    if (!cached) {
        index.exec("DELETE FROM music_files;");
        auto root = index.query("INSERT OR REPLACE INTO music_folder(singleton, root) VALUES (1, ?);");
        root.bind(1, directory.string());
        root.write();
    }
    if (!staleDates.empty()) {
        auto update = index.query("UPDATE music_files SET added = ?, modified = ? WHERE relative = ?;");
        for (const auto i : staleDates) {
            if (stop.stop_requested())
                return {};
            const auto& file = result.files[i];
            update.bind(1, file.added);
            update.bind(2, file.modified);
            update.bind(3, std::filesystem::path(file.path).lexically_relative(directory).generic_string());
            update.write();
        }
    }

    std::unordered_map<std::string, size_t> known;
    known.reserve(result.files.size());
    for (size_t i = 0; i < result.files.size(); ++i)
        known.emplace(result.files[i].path, i);
    std::vector<bool> seen(result.files.size(), false);
    auto upsert = index.query(R"sql(
        INSERT INTO music_files(relative, title, artist, album, album_artist, format, rate, bitrate, modified,
                                added, disc, number, year, duration, channels, bit_depth, bytes, mtime, lossless, missing)
        VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)
        ON CONFLICT(relative) DO UPDATE SET
            title=excluded.title, artist=excluded.artist, album=excluded.album, album_artist=excluded.album_artist,
            format=excluded.format, rate=excluded.rate, bitrate=excluded.bitrate, modified=excluded.modified, added=excluded.added,
            disc=excluded.disc, number=excluded.number, year=excluded.year, duration=excluded.duration,
            channels=excluded.channels, bit_depth=excluded.bit_depth, bytes=excluded.bytes, mtime=excluded.mtime,
            lossless=excluded.lossless, missing=excluded.missing;
    )sql");
    auto mark = index.query("UPDATE music_files SET missing = ? WHERE relative = ?;");
    const auto markMissing = [&](MediaFile& file, bool missing) {
        if (file.missing == missing)
            return;
        file.missing = missing;
        mark.bind(1, file.missing);
        mark.bind(2, std::filesystem::path(file.path).lexically_relative(directory).generic_string());
        mark.write();
    };
    for (size_t processed = 0; processed < candidates.size() && !stop.stop_requested(); ++processed) {
        progress.checked.store(processed, std::memory_order_release);
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
        if (previous && previous->bytes == size && previous->modificationStamp == ticks) {
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
        const std::string relative = entry.path().lexically_relative(directory).generic_string();
        media.id = mediaKey(relative);
        media.bytes = size;
        media.modificationStamp = ticks;
        media.added = modificationTime(stamp);
        media.modified = media.added.substr(0, 10);
        saveMediaFile(upsert, relative, media);
        if (previous)
            *previous = std::move(media);
        else
            result.files.push_back(std::move(media));
    }
    if (stop.stop_requested())
        return {};
    progress.checked.store(candidates.size(), std::memory_order_release);
    for (size_t i = 0; i < seen.size() && !stop.stop_requested(); ++i) {
        if (seen[i])
            continue;
        std::error_code error;
        const auto status = std::filesystem::status(result.files[i].path, error);
        if (status.type() == std::filesystem::file_type::not_found) {
            markMissing(result.files[i], true);
            result.missingPaths.push_back(std::filesystem::path(result.files[i].path).lexically_relative(directory).generic_string());
        }
    }
    if (stop.stop_requested())
        return {};
    result.missing = std::ranges::count(result.files, true, &MediaFile::missing);
    index.exec("COMMIT;");
    return result;
}

std::vector<std::string> removeMissingMusic(const std::filesystem::path& directory, std::span<const std::string> paths,
                                            std::stop_token stop, const std::filesystem::path& database)
{
    std::error_code error;
    if (!std::filesystem::is_directory(directory, error))
        throw std::runtime_error("Music folder is unavailable");
    const auto stillMissing = [&](const std::string& path) {
        return std::filesystem::status(directory / path, error).type() == std::filesystem::file_type::not_found;
    };

    std::vector<std::string> removed;
    if (database.empty()) {
        for (const auto& path : paths) {
            if (stop.stop_requested())
                return {};
            if (stillMissing(path))
                removed.push_back(mediaKey(path));
        }
        return removed;
    }

    MusicIndex index(database, SQLITE_OPEN_READWRITE);
    index.begin(stop);
    auto root = index.query("SELECT 1 FROM music_folder WHERE singleton = 1 AND root = ?;");
    root.bind(1, directory.string());
    if (!root.next())
        throw std::runtime_error("The music folder has changed");

    auto erase = index.query("DELETE FROM music_files WHERE relative = ? AND missing = 1;");
    for (const auto& path : paths) {
        if (stop.stop_requested())
            return {};
        if (!stillMissing(path))
            continue;
        erase.bind(1, path);
        erase.write();
        if (index.changes())
            removed.push_back(mediaKey(path));
    }
    if (stop.stop_requested())
        return {};
    if (!std::filesystem::is_directory(directory, error))
        throw std::runtime_error("Music folder is unavailable");
    index.exec("COMMIT;");
    return removed;
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
std::optional<MediaLibrary> loadMusicIndex(const std::filesystem::path&, const std::filesystem::path&) { return {}; }
MediaLibrary scanMusic(const std::filesystem::path&, MediaScanProgress&, std::stop_token,
                       const std::filesystem::path&) { return {}; }
std::vector<std::string> removeMissingMusic(const std::filesystem::path&, std::span<const std::string>,
                                            std::stop_token, const std::filesystem::path&) { return {}; }
std::vector<uint8_t> readEmbeddedArtwork(std::string_view, size_t, std::stop_token) { return {}; }
struct MediaDecoder::Impl {};
MediaDecoder::MediaDecoder(std::string_view, double, int, std::stop_token) {}
MediaDecoder::~MediaDecoder() = default;
bool MediaDecoder::valid() const { return false; }
int MediaDecoder::sampleRate() const { return 0; }
int MediaDecoder::read(std::span<float>) { return 0; }
#endif

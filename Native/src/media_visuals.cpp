#include "media.hpp"

#include <SDL3/SDL.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <deque>
#include <fstream>
#include <map>
#include <mutex>
#include <thread>

namespace {
struct VisualRequest {
    std::string key, audioPath;
    bool prefetch;
};

template<class Value>
class VisualCache {
public:
    using Loader = Value (*)(const std::filesystem::path&, const VisualRequest&, std::stop_token);
    VisualCache(const char* folder, Loader load)
    {
        const std::unique_ptr<char, decltype(&SDL_free)> preferences(SDL_GetPrefPath("DeckDS", "DeckDS"), SDL_free);
        if (preferences) {
            directory = std::filesystem::path(preferences.get()) / folder;
            std::error_code error;
            std::filesystem::create_directories(directory, error);
            if (error)
                directory.clear();
        }
        worker = std::jthread([this, load](std::stop_token stop) { run(load, stop); });
    }

    std::optional<Value> get(std::string_view key, std::string_view audioPath)
    {
        std::lock_guard lock(mutex);
        const auto found = entries.find(key);
        if (found != entries.end()) {
            found->second.use = ++generation;
            if (found->second.result)
                return found->second.result;
        }
        if (active) {
            if (active->key == key)
                active->prefetch = false;
            else if (active->prefetch)
                active->stop.request_stop();
        }
        if (found == entries.end()) {
            enqueue(key, audioPath, false);
        } else {
            const auto queued = std::ranges::find_if(requests, [&](const auto& request) {
                return request.prefetch && request.key == key;
            });
            if (queued != requests.end()) {
                auto request = std::move(*queued);
                requests.erase(queued);
                request.prefetch = false;
                requests.push_front(std::move(request));
            }
        }
        return std::nullopt;
    }

    void prefetch(const std::array<WaveformSource, 2>& sources)
    {
        std::lock_guard lock(mutex);
        const auto wanted = [&](std::string_view key) {
            return std::ranges::any_of(sources, [&](const auto& source) { return source.key == key; });
        };
        std::erase_if(requests, [&](const auto& request) {
            if (!request.prefetch || wanted(request.key))
                return false;
            entries.erase(request.key);
            return true;
        });
        if (active && active->prefetch && !wanted(active->key))
            active->stop.request_stop();
        for (const auto& [key, path] : sources) {
            if (key.empty())
                continue;
            if (auto found = entries.find(key); found != entries.end())
                found->second.use = ++generation;
            else if (requests.size() < 64)
                enqueue(key, path, true);
        }
    }

private:
    struct Entry { std::optional<Value> result; uint64_t use; };
    struct ActiveRequest { std::string_view key; bool prefetch; std::stop_source stop; };
    std::filesystem::path directory;
    std::mutex mutex;
    std::condition_variable_any ready;
    std::deque<VisualRequest> requests;
    std::map<std::string, Entry, std::less<>> entries;
    std::optional<ActiveRequest> active;
    uint64_t generation = 0;
    std::jthread worker;

    void enqueue(std::string_view key, std::string_view audioPath, bool prefetch)
    {
        if (requests.size() >= 64) {
            entries.erase(requests.back().key);
            requests.pop_back();
        }
        if (entries.size() >= 256) {
            auto oldest = entries.end();
            for (auto it = entries.begin(); it != entries.end(); ++it)
                if (it->second.result && (oldest == entries.end() || it->second.use < oldest->second.use))
                    oldest = it;
            if (oldest != entries.end())
                entries.erase(oldest);
        }
        VisualRequest request{std::string(key), std::string(audioPath), prefetch};
        entries.emplace(request.key, Entry{std::nullopt, ++generation});
        if (prefetch)
            requests.push_back(std::move(request));
        else
            requests.push_front(std::move(request));
        ready.notify_one();
    }

    void run(Loader load, std::stop_token stop)
    {
        while (!stop.stop_requested()) {
            VisualRequest request;
            std::stop_source loading;
            {
                std::unique_lock lock(mutex);
                ready.wait(lock, stop, [&] { return !requests.empty(); });
                if (stop.stop_requested())
                    return;
                request = std::move(requests.front());
                requests.pop_front();
                active.emplace(request.key, request.prefetch, loading);
            }
            std::stop_callback cancelLoading(stop, [&] { loading.request_stop(); });
            Value value{};
            try {
                value = load(directory, request, loading.get_token());
            } catch (const std::exception&) {
                // Leave the placeholder available if the source or cache cannot be read.
            }
            std::lock_guard lock(mutex);
            if (loading.stop_requested())
                entries.erase(request.key);
            else
                entries.at(request.key).result = std::move(value);
            active.reset();
        }
    }
};

bool saveVisual(const std::filesystem::path& path, std::span<const std::byte> bytes)
{
    auto temp = path;
    temp += "." + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + ".tmp";
    std::ofstream output(temp, std::ios::binary | std::ios::trunc);
    output.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    output.close();
    std::error_code error;
    if (output) {
        std::filesystem::rename(temp, path, error);
        if (!error)
            return true;
    }
    std::filesystem::remove(temp, error);
    return false;
}

std::filesystem::path loadArtwork(const std::filesystem::path& directory, const VisualRequest& request,
                                  std::stop_token stop)
{
    constexpr size_t maxBytes = 12 * 1024 * 1024;
    std::error_code error;
    if (!directory.empty()) {
        const auto cached = directory / (request.key + ".cover");
        const auto bytes = std::filesystem::file_size(cached, error);
        if (!error && bytes > 0 && bytes <= maxBytes)
            return cached;
        const auto picture = readEmbeddedArtwork(request.audioPath, maxBytes, stop);
        if (!picture.empty() && !stop.stop_requested() && saveVisual(cached, std::as_bytes(std::span(picture))))
            return cached;
    }
    const auto album = std::filesystem::path(request.audioPath).parent_path();
    for (const auto name : {"cover.jpg", "folder.jpg", "front.jpg", "cover.png", "folder.png", "front.png"}) {
        const auto candidate = album / name;
        const auto bytes = std::filesystem::file_size(candidate, error);
        if (!error && bytes > 0 && bytes <= maxBytes)
            return candidate;
    }
    return {};
}

MediaWaveform loadWaveform(const std::filesystem::path& directory, const VisualRequest& request,
                           std::stop_token stop)
{
    MediaWaveform waveform{};
    const auto path = directory / (request.key + ".rms-v1");
    if (!directory.empty()) {
        std::ifstream file(path, std::ios::binary);
        if (file.read(reinterpret_cast<char*>(waveform.data()), sizeof(waveform)) &&
            file.peek() == std::char_traits<char>::eof() &&
            std::ranges::all_of(waveform, [](float value) { return std::isfinite(value) && value >= 0.f && value <= 1.f; }))
            return waveform;
    }

    // Bucket short windows after decoding so the bars span the actual track length.
    MediaDecoder decoder(request.audioPath, 0, 12000, stop);
    if (!decoder.valid())
        return {};
    struct Window {
        double energy = 0;
        int samples;
    };
    std::vector<Window> windows;
    std::array<float, 480> block{};
    while (!stop.stop_requested()) {
        const int frames = decoder.read(block);
        if (frames == 0)
            break;
        Window window{.samples = frames * 2};
        for (int i = 0; i < window.samples; ++i)
            if (std::isfinite(block[i]))
                window.energy += static_cast<double>(block[i]) * block[i];
        windows.push_back(window);
    }
    if (windows.empty() || stop.stop_requested())
        return {};
    for (size_t i = 0; i < waveform.size(); ++i) {
        const size_t begin = i * windows.size() / waveform.size();
        const size_t end = std::max(begin + 1, (i + 1) * windows.size() / waveform.size());
        double energy = 0;
        size_t samples = 0;
        for (size_t j = begin; j < end; ++j) {
            energy += windows[j].energy;
            samples += windows[j].samples;
        }
        waveform[i] = static_cast<float>(std::min(1.0, std::sqrt(energy / samples)));
    }
    if (!directory.empty())
        saveVisual(path, std::as_bytes(std::span(waveform)));
    return waveform;
}

VisualCache<MediaWaveform>& waveformCache()
{
    static VisualCache<MediaWaveform> waveforms("Waveforms", loadWaveform);
    return waveforms;
}
}

std::optional<std::filesystem::path> cachedArtwork(std::string_view key, std::string_view path)
{
    static VisualCache<std::filesystem::path> covers("Artwork", loadArtwork);
    return covers.get(key, path);
}

std::optional<MediaWaveform> cachedWaveform(std::string_view key, std::string_view path)
{
    return waveformCache().get(key, path);
}

void prefetchWaveforms(WaveformSource next, WaveformSource previous)
{
    waveformCache().prefetch({next, previous});
}

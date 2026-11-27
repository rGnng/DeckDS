#include "media_playback.hpp"
#include "media.hpp"

#include <SDL3/SDL.h>

#include <algorithm>
#include <bit>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <thread>

namespace {
constexpr int frameBytes = 2 * sizeof(float);
constexpr auto refillDelay = std::chrono::milliseconds(5);
}

struct MediaPlayback::Impl {
    struct Request {
        std::string file;
        double seconds;
        std::stop_token cancel;
    };

    mutable std::mutex mutex;
    std::condition_variable_any changed;
    std::optional<Request> pending;
    std::stop_source cancellation;
    Snapshot state;
    std::vector<Sample> history;
    int historyRate = 0;
    uint64_t playedFrames = 0, writtenFrames = 0;
    float gain = 1.f;
    SDL_AudioStream* active = nullptr;
    std::jthread worker{[this](std::stop_token stop) { run(stop); }};

    ~Impl()
    {
        stop();
        worker.request_stop();
    }

    void play(std::string_view file, double seconds, float level)
    {
        const std::lock_guard lock(mutex);
        cancellation.request_stop();
        if (active)
            SDL_PauseAudioStreamDevice(active);
        cancellation = std::stop_source{};
        gain = level;
        state = {Status::Loading, seconds, {}};
        pending = Request{std::string(file), seconds, cancellation.get_token()};
        changed.notify_one();
    }

    void stop()
    {
        const std::lock_guard lock(mutex);
        cancellation.request_stop();
        pending.reset();
        if (active)
            SDL_PauseAudioStreamDevice(active);
        state = {};
    }

    void setGain(float level)
    {
        const std::lock_guard lock(mutex);
        if (gain != level) {
            gain = level;
            if (active)
                SDL_SetAudioStreamGain(active, gain);
        }
    }

    void run(std::stop_token stop)
    {
        while (!stop.stop_requested()) {
            Request request;
            {
                std::unique_lock lock(mutex);
                changed.wait(lock, stop, [&] { return pending.has_value(); });
                if (stop.stop_requested())
                    return;
                request = std::move(*pending);
                pending.reset();
            }
            playFile(request);
        }
    }

    void publish(const Request& request, const Snapshot& next, uint64_t consumed = 0)
    {
        const std::lock_guard lock(mutex);
        if (!request.cancel.stop_requested()) {
            state = next;
            playedFrames = consumed;
        }
    }

    SpectrumInput readSpectrum(std::span<Sample> buffer) const
    {
        const std::lock_guard lock(mutex);
        if (state.status != Status::Playing)
            return {};

        // Use the longest power-of-two window up to 100 ms at the native sample rate.
        const size_t length = std::bit_floor(std::min(buffer.size(), static_cast<size_t>(historyRate / 10)));
        const auto samples = buffer.first(length);
        const uint64_t oldest = writtenFrames - std::min<uint64_t>(writtenFrames, history.size());
        const size_t count = std::min<uint64_t>(playedFrames - std::min(playedFrames, oldest), length);
        const size_t offset = (playedFrames - count) % history.size();
        const size_t first = std::min(count, history.size() - offset);
        const auto destination = samples.end() - count;
        std::fill(samples.begin(), destination, Sample{});
        std::copy_n(history.begin() + offset, first, destination);
        std::copy_n(history.begin(), count - first, destination + first);
        return {samples, historyRate};
    }

    void playFile(const Request& request)
    {
        if (request.cancel.stop_requested())
            return;
        MediaDecoder decoder(request.file, request.seconds, 0, request.cancel);
        if (!decoder.valid()) {
            publish(request, {Status::Failed, request.seconds, {}});
            return;
        }
        const int rate = decoder.sampleRate();
        const SDL_AudioSpec input{SDL_AUDIO_F32, 2, rate};
        std::unique_ptr<SDL_AudioStream, decltype(&SDL_DestroyAudioStream)> stream(
            SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &input, nullptr, nullptr), SDL_DestroyAudioStream);
        if (!stream) {
            SDL_Log("Unable to open the music output: %s", SDL_GetError());
            publish(request, {Status::Failed, request.seconds, {}});
            return;
        }

        SDL_AudioSpec device{};
        int deviceFrames = 0;
        SDL_GetAudioDeviceFormat(SDL_GetAudioStreamDevice(stream.get()), &device, &deviceFrames);
        const double devicePeriod = device.freq > 0 && deviceFrames > 0 ? static_cast<double>(deviceFrames) / device.freq : .02;
        const int targetFrames = static_cast<int>(rate * std::max(.25, 4 * devicePeriod));
        {
            const std::lock_guard lock(mutex);
            history.resize(static_cast<size_t>(targetFrames + rate / 10));
            historyRate = rate;
            writtenFrames = 0;
        }
        uint64_t submitted = 0;
        bool started = false, eof = false, failed = false;
        std::optional<std::chrono::steady_clock::time_point> drained;
        Snapshot next{Status::Loading, request.seconds, {}, {rate, device.freq, device.channels, deviceFrames}};
        std::array<float, 4096> block{};

        while (!request.cancel.stop_requested()) {
            int queued = SDL_GetAudioStreamQueued(stream.get());
            while (queued >= 0 && queued / frameBytes < targetFrames && !eof && !request.cancel.stop_requested()) {
                const size_t wanted = std::min<size_t>(block.size() / 2, targetFrames - queued / frameBytes);
                const int frames = decoder.read(std::span(block).first(wanted * 2));
                if (request.cancel.stop_requested())
                    break;
                if (frames > 0) {
                    if (!SDL_PutAudioStreamData(stream.get(), block.data(), frames * frameBytes)) {
                        failed = true;
                        break;
                    }
                    {
                        const std::lock_guard lock(mutex);
                        for (int i = 0; i < frames; ++i)
                            history[(submitted++) % history.size()] = {block[i * 2], block[i * 2 + 1]};
                        writtenFrames = submitted;
                    }
                    queued = SDL_GetAudioStreamQueued(stream.get());
                }
                if (frames < static_cast<int>(wanted)) {
                    eof = true;
                    failed = !SDL_FlushAudioStream(stream.get());
                }
            }
            if (request.cancel.stop_requested())
                break;
            if (queued < 0 || failed) {
                next.status = Status::Failed;
                break;
            }
            if (!started && submitted > 0) {
                const std::lock_guard lock(mutex);
                if (request.cancel.stop_requested())
                    break;
                active = stream.get();
                SDL_SetAudioStreamGain(active, gain);
                if (!SDL_ResumeAudioStreamDevice(active)) {
                    next.status = Status::Failed;
                    break;
                }
                started = true;
            }

            const auto remaining = std::min(submitted, static_cast<uint64_t>(queued / frameBytes));
            const auto consumed = submitted - remaining;
            next.status = Status::Playing;
            next.seconds = request.seconds + static_cast<double>(consumed) / rate;
            for (size_t i = 0; i < next.scope.size(); ++i) {
                const auto before = (next.scope.size() - i) * rate / scopeRate;
                next.scope[i] = consumed >= before ? history[(consumed - before) % history.size()] : Sample{};
            }
            if (eof && remaining == 0) {
                if (!drained)
                    drained = std::chrono::steady_clock::now();
                if (!started || std::chrono::steady_clock::now() - *drained >= std::chrono::duration<double>(devicePeriod)) {
                    next.status = Status::Finished;
                    break;
                }
            }
            publish(request, next, consumed);
            std::this_thread::sleep_for(refillDelay);
        }
        const std::lock_guard lock(mutex);
        if (active == stream.get())
            active = nullptr;
        if (!request.cancel.stop_requested())
            state = next;
    }
};

MediaPlayback::MediaPlayback() : impl(std::make_unique<Impl>()) {}
MediaPlayback::~MediaPlayback() = default;
void MediaPlayback::play(std::string_view file, double seconds, float gain) { impl->play(file, seconds, gain); }
void MediaPlayback::stop() { impl->stop(); }
void MediaPlayback::setGain(float gain) { impl->setGain(gain); }
MediaPlayback::Snapshot MediaPlayback::snapshot() const
{
    const std::lock_guard lock(impl->mutex);
    return impl->state;
}

MediaPlayback::SpectrumInput MediaPlayback::readSpectrum(std::span<Sample> buffer) const
{
    return impl->readSpectrum(buffer);
}

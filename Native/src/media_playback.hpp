#pragma once

#include <array>
#include <memory>
#include <span>
#include <string_view>

class MediaPlayback {
public:
    enum class Status { Idle, Loading, Playing, Finished, Failed };
    static constexpr int scopeRate = 8192;
    using Sample = std::array<float, 2>;
    struct SpectrumInput {
        std::span<const Sample> samples;
        int rate = 0;
    };
    struct Snapshot {
        Status status = Status::Idle;
        double seconds = 0;
        std::array<Sample, 256> scope{};
    };

    MediaPlayback();
    ~MediaPlayback();
    void play(std::string_view file, double seconds, float gain);
    void stop();
    void setGain(float gain);
    Snapshot snapshot() const;
    SpectrumInput readSpectrum(std::span<Sample> buffer) const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl;
};

#pragma once

#include <array>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

class WebPassword {
public:
    static constexpr size_t limit = 128;
    bool load(const std::filesystem::path& preferences);
    bool enabled() const { return verifier.has_value(); }
    bool matches(std::string_view password) const;
    bool save(std::string_view password);
    std::string newSession();
    bool acceptsSession(std::string_view token) const;

private:
    using Digest = std::array<unsigned char, 32>;
    using Salt = std::array<unsigned char, 16>;
    struct Verifier {
        Salt salt;
        Digest digest;
        std::optional<Digest> sessionKey{};
    };
    static std::optional<Digest> derive(std::string_view password, const Salt& salt);
    bool persist(const Verifier& record) const;

    std::filesystem::path path;
    std::optional<Verifier> verifier;
};

#include "web_password.hpp"

#include <openssl/crypto.h>
#include <openssl/evp.h>
#include <openssl/hmac.h>
#include <openssl/rand.h>
#include <unistd.h>

#include <cerrno>
#include <cstdio>
#include <cstdlib>

namespace {
constexpr unsigned char verifierVersion = 2;

template<size_t Size>
std::string hex(const std::array<unsigned char, Size>& bytes)
{
    constexpr std::string_view digits = "0123456789abcdef";
    std::string value;
    value.reserve(2 * Size);
    for (const auto byte : bytes) {
        value += digits[byte >> 4];
        value += digits[byte & 15];
    }
    return value;
}

std::string sessionSignature(const std::array<unsigned char, 32>& key, std::string_view nonce)
{
    std::array<unsigned char, 32> signature;
    if (!HMAC(EVP_sha256(), key.data(), static_cast<int>(key.size()),
              reinterpret_cast<const unsigned char*>(nonce.data()), nonce.size(), signature.data(), nullptr))
        return {};
    return hex(signature);
}
}

std::optional<WebPassword::Digest> WebPassword::derive(std::string_view password, const Salt& salt)
{
    Digest digest;
    if (PKCS5_PBKDF2_HMAC(password.data(), static_cast<int>(password.size()), salt.data(),
                          static_cast<int>(salt.size()), 600'000, EVP_sha256(),
                          static_cast<int>(digest.size()), digest.data()) != 1)
        return std::nullopt;
    return digest;
}

bool WebPassword::load(const std::filesystem::path& preferences)
{
    verifier.reset();
    path.clear();
    if (preferences.empty())
        return true;
    path = preferences / "web-password";
    std::FILE* file = std::fopen(path.c_str(), "rb");
    if (!file)
        return errno == ENOENT;
    Verifier record;
    const int version = std::fgetc(file);
    bool valid = (version == 1 || version == verifierVersion) &&
        std::fread(record.salt.data(), 1, record.salt.size(), file) == record.salt.size() &&
        std::fread(record.digest.data(), 1, record.digest.size(), file) == record.digest.size();
    if (valid && version == verifierVersion) {
        auto& key = record.sessionKey.emplace();
        valid = std::fread(key.data(), 1, key.size(), file) == key.size();
    }
    valid = valid && std::fgetc(file) == EOF && !std::ferror(file);
    std::fclose(file);
    if (valid)
        verifier = record;
    return valid;
}

bool WebPassword::matches(std::string_view password) const
{
    if (!verifier || password.empty() || password.size() > limit)
        return false;
    const auto digest = derive(password, verifier->salt);
    return digest && CRYPTO_memcmp(digest->data(), verifier->digest.data(), digest->size()) == 0;
}

bool WebPassword::save(std::string_view password)
{
    if (password.empty()) {
        if (!verifier)
            return true;
        if (unlink(path.c_str()) != 0 && errno != ENOENT)
            return false;
        verifier.reset();
        return true;
    }
    if (path.empty() || password.size() > limit)
        return false;
    Verifier record;
    auto& key = record.sessionKey.emplace();
    if (RAND_bytes(record.salt.data(), static_cast<int>(record.salt.size())) != 1 ||
        RAND_bytes(key.data(), static_cast<int>(key.size())) != 1)
        return false;
    const auto digest = derive(password, record.salt);
    if (!digest)
        return false;
    record.digest = *digest;
    if (!persist(record))
        return false;
    verifier = record;
    return true;
}

bool WebPassword::persist(const Verifier& record) const
{
    const auto& key = *record.sessionKey;
    auto temporary = path.string() + ".XXXXXX";
    const int fd = mkstemp(temporary.data());
    if (fd < 0)
        return false;
    auto* file = fdopen(fd, "wb");
    if (!file) {
        close(fd);
        unlink(temporary.c_str());
        return false;
    }
    const bool written = std::fputc(verifierVersion, file) != EOF &&
        std::fwrite(record.salt.data(), 1, record.salt.size(), file) == record.salt.size() &&
        std::fwrite(record.digest.data(), 1, record.digest.size(), file) == record.digest.size() &&
        std::fwrite(key.data(), 1, key.size(), file) == key.size() &&
        std::fflush(file) == 0 && fsync(fd) == 0;
    const bool closed = std::fclose(file) == 0;
    if (!written || !closed || std::rename(temporary.c_str(), path.c_str()) != 0) {
        unlink(temporary.c_str());
        return false;
    }
    return true;
}

std::string WebPassword::newSession()
{
    if (!verifier)
        return {};
    if (!verifier->sessionKey) {
        auto upgraded = *verifier;
        auto& key = upgraded.sessionKey.emplace();
        if (RAND_bytes(key.data(), static_cast<int>(key.size())) != 1 || !persist(upgraded))
            return {};
        verifier = upgraded;
    }
    Salt random;
    if (RAND_bytes(random.data(), static_cast<int>(random.size())) != 1)
        return {};
    const auto nonce = hex(random);
    const auto signature = sessionSignature(*verifier->sessionKey, nonce);
    return signature.empty() ? std::string{} : nonce + signature;
}

bool WebPassword::acceptsSession(std::string_view token) const
{
    constexpr size_t nonceSize = 2 * Salt{}.size();
    constexpr size_t signatureSize = 2 * Digest{}.size();
    if (!verifier || !verifier->sessionKey || token.size() != nonceSize + signatureSize)
        return false;
    const auto signature = sessionSignature(*verifier->sessionKey, token.substr(0, nonceSize));
    return signature.size() == signatureSize &&
           CRYPTO_memcmp(signature.data(), token.data() + nonceSize, signatureSize) == 0;
}

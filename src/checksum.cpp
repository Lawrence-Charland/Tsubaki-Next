// SPDX-License-Identifier: GPL-2.0-only
// Copyright (C) 2026 Lawrence Charland

#include "checksum.hpp"
#include "utilities.hpp"

#include <cerrno>
#include <cstddef>
#include <fcntl.h>
#if defined(_WIN32)
#include <io.h>
#else
#include <unistd.h>
#endif
#include <memory>
#include <openssl/core_names.h>
#include <openssl/evp.h>
#include <openssl/params.h>
#include <vector>

namespace
{
const EVP_MD *digest_for(tsubaki::algorithm algo)
{
    switch (algo)
    {
    case tsubaki::algorithm::md5:
        return EVP_md5();
    case tsubaki::algorithm::sha1:
        return EVP_sha1();
    case tsubaki::algorithm::sha224:
        return EVP_sha224();
    case tsubaki::algorithm::sha256:
        return EVP_sha256();
    case tsubaki::algorithm::sha384:
        return EVP_sha384();
    case tsubaki::algorithm::sha512:
        return EVP_sha512();
    case tsubaki::algorithm::sha3_256:
        return EVP_sha3_256();
    case tsubaki::algorithm::sha3_512:
        return EVP_sha3_512();
    case tsubaki::algorithm::blake2b512:
        return EVP_blake2b512();
    case tsubaki::algorithm::blake2s256:
        return EVP_blake2s256();
    default:
        return nullptr;
    }
}

// Keyed hashes all go through EVP_MAC: HMAC keeps its digest in a parameter,
// keyed BLAKE2 is a MAC algorithm of its own.
struct mac_spec
{
    const char *name;   // EVP_MAC algorithm name
    const char *digest; // digest parameter, nullptr if the MAC has none
};

mac_spec mac_for(tsubaki::algorithm algo)
{
    switch (algo)
    {
    case tsubaki::algorithm::hmac_md5:
        return {"HMAC", "MD5"};
    case tsubaki::algorithm::hmac_sha1:
        return {"HMAC", "SHA1"};
    case tsubaki::algorithm::hmac_sha224:
        return {"HMAC", "SHA224"};
    case tsubaki::algorithm::hmac_sha256:
        return {"HMAC", "SHA256"};
    case tsubaki::algorithm::hmac_sha384:
        return {"HMAC", "SHA384"};
    case tsubaki::algorithm::hmac_sha512:
        return {"HMAC", "SHA512"};
    case tsubaki::algorithm::hmac_sha3_256:
        return {"HMAC", "SHA3-256"};
    case tsubaki::algorithm::hmac_sha3_512:
        return {"HMAC", "SHA3-512"};
    case tsubaki::algorithm::blake2b512_mac:
        return {"BLAKE2BMAC", nullptr};
    case tsubaki::algorithm::blake2s256_mac:
        return {"BLAKE2SMAC", nullptr};
    default:
        return {nullptr, nullptr};
    }
}

std::unexpected<std::error_code> crypto_failure()
{
    return std::unexpected(
        std::make_error_code(std::errc::operation_not_supported));
}

std::unexpected<std::error_code> bad_algorithm()
{
    return std::unexpected(std::make_error_code(std::errc::invalid_argument));
}

constexpr std::size_t buffer_size = 64 * 1024;

// Streams the whole file through `update`, which reports false when the crypto
// context rejects a chunk. Shared by the digest and the MAC paths.
template <typename Update>
std::expected<void, std::error_code>
stream_file(const std::filesystem::path &path, Update &&update)
{

#if defined(_WIN32)
    // _O_BINARY keeps CRLF translation and the 0x1A EOF rule out of the read,
    // which would otherwise change the digest of any binary file.
    tsubaki::fd_guard fd(_open(path.wstring().c_str(),
                               _O_RDONLY | _O_NOINHERIT | _O_BINARY));
#else
    tsubaki::fd_guard fd(::open(path.c_str(), O_RDONLY | O_CLOEXEC));
#endif

    if (fd.get() < 0)
        return std::unexpected(std::error_code(errno, std::system_category()));

    thread_local static std::vector<unsigned char> buffer(buffer_size);

    for (;;)
    {

#if defined(_WIN32)
        const auto read_size =
            _read(fd.get(), buffer.data(), static_cast<unsigned int>(buffer_size));
#else
        const ssize_t read_size = ::read(fd.get(), buffer.data(), buffer_size);
#endif

        if (read_size < 0)
        {
            if (errno == EINTR)
                continue;
            return std::unexpected(
                std::error_code(errno, std::system_category()));
        }
        if (read_size == 0)
            return {};
        if (!update(buffer.data(), static_cast<std::size_t>(read_size)))
            return crypto_failure();
    }
}
} // namespace

std::expected<std::string, std::error_code>
tsubaki::checksum_file(const std::filesystem::path &path, algorithm algo)
{
    if (algo == algorithm::none)
        return std::string(none_checksum);

    const EVP_MD *md = digest_for(algo);
    if (!md)
        return bad_algorithm();

    using ctx_ptr = std::unique_ptr<EVP_MD_CTX, decltype(&EVP_MD_CTX_free)>;
    ctx_ptr ctx(EVP_MD_CTX_new(), &EVP_MD_CTX_free);
    if (!ctx)
        return std::unexpected(
            std::make_error_code(std::errc::not_enough_memory));

    if (EVP_DigestInit_ex(ctx.get(), md, nullptr) != 1)
        return crypto_failure();

    const auto pumped =
        stream_file(path, [&ctx](const unsigned char *data, std::size_t len)
                    { return EVP_DigestUpdate(ctx.get(), data, len) == 1; });
    if (!pumped)
        return std::unexpected(pumped.error());

    unsigned char digest[EVP_MAX_MD_SIZE];
    unsigned int digest_len = 0;
    if (EVP_DigestFinal_ex(ctx.get(), digest, &digest_len) != 1)
        return crypto_failure();

    return to_hex(digest, digest_len);
}

std::expected<std::string, std::error_code>
tsubaki::checksum_file_key(const std::filesystem::path &path,
                           const std::string &key, algorithm algo)
{
    if (algo == algorithm::none)
        return std::string(none_checksum);

    const mac_spec spec = mac_for(algo);
    if (!spec.name)
        return bad_algorithm();

    using mac_ptr = std::unique_ptr<EVP_MAC, decltype(&EVP_MAC_free)>;
    mac_ptr mac(EVP_MAC_fetch(nullptr, spec.name, nullptr), &EVP_MAC_free);
    if (!mac)
        return crypto_failure();

    using mac_ctx_ptr =
        std::unique_ptr<EVP_MAC_CTX, decltype(&EVP_MAC_CTX_free)>;
    mac_ctx_ptr ctx(EVP_MAC_CTX_new(mac.get()), &EVP_MAC_CTX_free);
    if (!ctx)
        return std::unexpected(
            std::make_error_code(std::errc::not_enough_memory));

    OSSL_PARAM params[2];
    std::size_t param_count = 0;
    if (spec.digest)
    {
        // OpenSSL only reads the name; the setter itself is not const-correct.
        params[param_count++] = OSSL_PARAM_construct_utf8_string(
            OSSL_MAC_PARAM_DIGEST, const_cast<char *>(spec.digest), 0);
    }
    params[param_count] = OSSL_PARAM_construct_end();

    if (EVP_MAC_init(ctx.get(),
                     reinterpret_cast<const unsigned char *>(key.data()),
                     key.size(), params) != 1)
        return crypto_failure();

    const auto pumped =
        stream_file(path, [&ctx](const unsigned char *data, std::size_t len)
                    { return EVP_MAC_update(ctx.get(), data, len) == 1; });
    if (!pumped)
        return std::unexpected(pumped.error());

    unsigned char out[EVP_MAX_MD_SIZE];
    std::size_t out_len = 0;
    if (EVP_MAC_final(ctx.get(), out, &out_len, sizeof out) != 1)
        return crypto_failure();

    return to_hex(out, out_len);
}

// The functions which interact with OpenSSL API were assisted by DeepSeek.

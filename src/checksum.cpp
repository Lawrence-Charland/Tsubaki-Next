// SPDX-License-Identifier: GPL-2.0-only
// Copyright (C) 2026 Lawrence Charland

#include "checksum.hpp"
#include "utilities.hpp"

#include <cerrno>
#include <cstddef>
#include <memory>
#include <stdexcept>
#include <vector>

#include <fcntl.h>
#include <unistd.h>

#include <openssl/evp.h>

namespace
{
const EVP_MD *to_evp(tsubaki::algorithm algo)
{
    switch (algo)
    {
    case tsubaki::algorithm::none:
        return nullptr;
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
        throw std::invalid_argument("to_evp: bad usage: unsupported algorithm");
    }
    return nullptr;
}

std::unexpected<std::error_code> crypto_failure()
{
    return std::unexpected(
        std::make_error_code(std::errc::operation_not_supported));
}

constexpr std::size_t buffer_size = 64 * 1024;
} // namespace

std::expected<std::string, std::error_code>
tsubaki::checksum_file(const std::filesystem::path &path, algorithm algo)
{
    if (algo == algorithm::none)
        return std::string(none_checksum);

    const EVP_MD *md = to_evp(algo);
    if (!md)
        return crypto_failure();

    fd_guard fd(::open(path.c_str(), O_RDONLY | O_CLOEXEC));
    if (fd.get() < 0)
        return std::unexpected(std::error_code(errno, std::system_category()));

    using ctx_ptr = std::unique_ptr<EVP_MD_CTX, decltype(&EVP_MD_CTX_free)>;
    ctx_ptr ctx(EVP_MD_CTX_new(), &EVP_MD_CTX_free);
    if (!ctx)
        return std::unexpected(
            std::make_error_code(std::errc::not_enough_memory));

    if (EVP_DigestInit_ex(ctx.get(), md, nullptr) != 1)
        return crypto_failure();

    thread_local static std::vector<unsigned char> buffer(buffer_size);

    for (;;)
    {
        const ssize_t read_size = ::read(fd.get(), buffer.data(), buffer_size);
        if (read_size < 0)
        {
            if (errno == EINTR)
                continue;
            return std::unexpected(
                std::error_code(errno, std::system_category()));
        }
        if (read_size == 0)
            break;
        if (EVP_DigestUpdate(ctx.get(), buffer.data(),
                             static_cast<std::size_t>(read_size)) != 1)
            return crypto_failure();
    }

    unsigned char digest[EVP_MAX_MD_SIZE];
    unsigned int digest_len = 0;
    if (EVP_DigestFinal_ex(ctx.get(), digest, &digest_len) != 1)
        return crypto_failure();

    return to_hex(digest, digest_len);
}

// SPDX-License-Identifier: GPL-2.0-only
// Copyright (C) 2026 Lawrence Charland

#ifndef LCPROJECTS_TSUBAKI_CHECKSUM_HPP
#define LCPROJECTS_TSUBAKI_CHECKSUM_HPP

#include <algorithm>
#include <array>
#include <cstddef>
#include <expected>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>

namespace tsubaki
{
#define ALGORITHM_LIST(X)                                                      \
    X(none)                                                                    \
    X(md5)                                                                     \
    X(sha1)                                                                    \
    X(sha224)                                                                  \
    X(sha256)                                                                  \
    X(sha384)                                                                  \
    X(sha512)                                                                  \
    X(sha3_256)                                                                \
    X(sha3_512)                                                                \
    X(blake2b512)                                                              \
    X(blake2s256)                                                              \
    X(hmac_md5)                                                                \
    X(hmac_sha1)                                                               \
    X(hmac_sha224)                                                             \
    X(hmac_sha256)                                                             \
    X(hmac_sha384)                                                             \
    X(hmac_sha512)                                                             \
    X(hmac_sha3_256)                                                           \
    X(hmac_sha3_512)                                                           \
    X(blake2b512_mac)                                                          \
    X(blake2s256_mac)

enum class algorithm
{
#define ALGORITHM_ENUM_ITEM(name) name,
    ALGORITHM_LIST(ALGORITHM_ENUM_ITEM)
#undef ALGORITHM_ENUM_ITEM
    dynamic,
    error,
};

const constexpr std::array mapper = {
#define ALGORITHM_STR_ITEM(name) std::string_view{#name},
    ALGORITHM_LIST(ALGORITHM_STR_ITEM)
#undef ALGORITHM_STR_ITEM
};

inline algorithm str_to_alg(const std::string_view &literal)
{
    auto i = std::find(mapper.begin(), mapper.end(), literal);
    if (i == mapper.end())
        return algorithm::error;
    return static_cast<algorithm>(i - mapper.begin());
}

inline std::string_view alg_to_str(const algorithm &alg)
{
    if (alg == algorithm::error)
        throw std::invalid_argument("tsubaki::alg_to_string: algorithm::error "
                                    "has no string representation.");
    return mapper.at(static_cast<size_t>(alg));
}

inline constexpr std::string_view none_checksum = "<NONE>";

std::expected<std::string, std::error_code>
checksum_file(const std::filesystem::path &path, algorithm algo);

std::expected<std::string, std::error_code>
checksum_file_key(const std::filesystem::path &path, const std::string &key,
                  algorithm algo);

inline bool is_keyed(const algorithm &alg)
{
    switch (alg)
    {
    case algorithm::hmac_md5:
    case algorithm::hmac_sha1:
    case algorithm::hmac_sha224:
    case algorithm::hmac_sha256:
    case algorithm::hmac_sha384:
    case algorithm::hmac_sha512:
    case algorithm::hmac_sha3_256:
    case algorithm::hmac_sha3_512:
    case algorithm::blake2b512_mac:
    case algorithm::blake2s256_mac:
        return true;
    default:
        return false;
    }
}

inline std::size_t max_key_length(const algorithm &alg)
{
    switch (alg)
    {
    case algorithm::blake2b512_mac:
        return 64;
    case algorithm::blake2s256_mac:
        return 32;
    default:
        return 0;
    }
}

inline bool check_key_validity(const std::string &key, const algorithm &alg)
{
    if (!is_keyed(alg))
        return true;
    const std::size_t limit = max_key_length(alg);
    return !key.empty() && (limit == 0 || key.size() <= limit);
}
} // namespace tsubaki

#endif

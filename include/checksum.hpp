// SPDX-License-Identifier: GPL-2.0-only
// Copyright (C) 2026 Lawrence Charland

#ifndef LCPROJECTS_TSUBAKI_CHECKSUM_HPP
#define LCPROJECTS_TSUBAKI_CHECKSUM_HPP

#include <algorithm>
#include <cstddef>
#include <expected>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

namespace tsubaki
{
#define ALGORITHM_LIST(X)                                                      \
    X(none, "none")                                                            \
    X(md5, "md5")                                                              \
    X(sha1, "sha1")                                                            \
    X(sha224, "sha224")                                                        \
    X(sha256, "sha256")                                                        \
    X(sha384, "sha384")                                                        \
    X(sha512, "sha512")                                                        \
    X(sha3_256, "sha3_256")                                                    \
    X(sha3_512, "sha3_512")                                                    \
    X(blake2b512, "blake2b512")                                                \
    X(blake2s256, "blake2s256")                                                \
    X(hmac_md5, "hmac_md5")                                                    \
    X(hmac_sha1, "hmac_sha1")                                                  \
    X(hmac_sha224, "hmac_sha224")                                              \
    X(hmac_sha256, "hmac_sha256")                                              \
    X(hmac_sha384, "hmac_sha384")                                              \
    X(hmac_sha512, "hmac_sha512")                                              \
    X(hmac_sha3_256, "hmac_sha3_256")                                          \
    X(hmac_sha3_512, "hmac_sha3_512")                                          \
    X(blake2b512_mac, "blake2b512_mac")                                        \
    X(blake2s256_mac, "blake2s256_mac")

enum class algorithm
{
#define ALGORITHM_ENUM_ITEM(name, str) name,
    ALGORITHM_LIST(ALGORITHM_ENUM_ITEM)
#undef ALGORITHM_ENUM_ITEM
    dynamic,
    error,
};

const std::vector<std::string> mapper = {
#define ALGORITHM_STR_ITEM(name, str) str,
    ALGORITHM_LIST(ALGORITHM_STR_ITEM)
#undef ALGORITHM_STR_ITEM
};

inline algorithm str_to_alg(const std::string &literal)
{
    auto i = std::find(mapper.begin(), mapper.end(), literal);
    if (i == mapper.end())
        return algorithm::error;
    return static_cast<algorithm>(i - mapper.begin());
}

inline std::string alg_to_str(const algorithm &alg)
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

inline constexpr bool is_keyed(const algorithm &alg)
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

inline constexpr std::size_t max_key_length(const algorithm &alg)
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

inline constexpr bool check_key_validity(const std::string &key,
                                         const algorithm &alg)
{
    if (!is_keyed(alg))
        return true;
    const std::size_t limit = max_key_length(alg);
    return !key.empty() && (limit == 0 || key.size() <= limit);
}
} // namespace tsubaki

#endif

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
    X(blake2s256, "blake2s256")

enum class algorithm
{
#define ALGORITHM_ENUM_ITEM(name, str) name,
    ALGORITHM_LIST(ALGORITHM_ENUM_ITEM)
#undef ALGORITHM_ENUM_ITEM
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
        throw std::invalid_argument("tsubaki::alg_to_string: bad usage: alg "
                                    "can not be algorithm::error.");
    return mapper.at(static_cast<size_t>(alg));
}

inline constexpr std::string_view none_checksum = "<NONE>";

std::expected<std::string, std::error_code>
checksum_file(const std::filesystem::path &path, algorithm algo);
} // namespace tsubaki

#endif

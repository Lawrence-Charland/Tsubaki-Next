// SPDX-License-Identifier: GPL-2.0-only
// Copyright (C) 2026 Lawrence Charland

#ifndef LCPROJECTS_TSUBAKI_CHECKSUM_API_HPP
#define LCPROJECTS_TSUBAKI_CHECKSUM_API_HPP

#include "checksum.hpp"
#include "dpframework.hpp"

#include <expected>
#include <filesystem>
#include <string>
#include <system_error>

namespace tsubaki
{
// Why a checksum_api is unusable; none once it is ready to hash.
enum class checksum_error
{
    none,
    unknown_algorithm,
    missing_key,
    invalid_key,
};

class checksum_api
{
  private:
    algorithm algo = algorithm::none;
    std::string display_algo_name;
    std::string key;
    checksum_error err = checksum_error::none;
    inline static dpf_instance dpf;
    provider pro = dpf.not_found;

  public:
    checksum_api() = default;
    checksum_api(const checksum_api &) = delete;
    checksum_api &operator=(const checksum_api &) = delete;

    bool ok() const;
    checksum_error error() const;
    bool hashing() const;
    bool keyed() const;
    const std::string &name() const;
    bool set_algo(const std::string &name);
    bool set_key(std::string _key);

    std::expected<std::string, std::error_code>
    calc(const std::filesystem::path &target) const;
};
} // namespace tsubaki

#endif

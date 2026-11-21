// SPDX-License-Identifier: GPL-2.0-only
// Copyright (C) 2026 Lawrence Charland

#include "checksum_api.hpp"
#include "checksum.hpp"
#include "dpframework.hpp"
#include "dynamic_provider_header.hpp"

#include <expected>
#include <string>
#include <system_error>
#include <utility>

namespace tsubaki
{
bool checksum_api::ok() const
{
    return err == checksum_error::none;
}

checksum_error checksum_api::error() const
{
    return err;
}

bool checksum_api::hashing() const
{
    return algo != algorithm::none && algo != algorithm::error;
}

bool checksum_api::keyed() const
{
    if (algo == algorithm::dynamic)
        return pro.is_keyed != nullptr && pro.is_keyed();
    return is_keyed(algo);
}

const std::string &checksum_api::name() const
{
    return display_algo_name;
}

bool checksum_api::set_algo(const std::string &name)
{
    algo = str_to_alg(name);
    pro = dpf.not_found;
    if (algo != algorithm::error)
    {
        display_algo_name = alg_to_str(algo);
        err = checksum_error::none;
        return true;
    }
    pro = dpf.find_provider(name);
    if (pro.error != provider_msg::success)
    {
        err = checksum_error::unknown_algorithm;
        display_algo_name.clear();
        return false;
    }
    algo = algorithm::dynamic;
    // algorithm::dynamic has no row in mapper; the provider names itself.
    display_algo_name = pro.name();
    err = checksum_error::none;
    return true;
}

bool checksum_api::set_key(std::string _key)
{
    if (!ok())
        return false;
    if (!keyed())
        return true;
    if (_key.empty())
    {
        err = checksum_error::missing_key;
        return false;
    }
    if (algo == algorithm::dynamic)
    {
        if (pro.check_key == nullptr || !pro.check_key(_key.c_str()))
        {
            err = checksum_error::invalid_key;
            return false;
        }
    }
    else if (!check_key_validity(_key, algo))
    {
        err = checksum_error::invalid_key;
        return false;
    }
    key = std::move(_key);
    return true;
}

std::expected<std::string, std::error_code>
checksum_api::calc(const std::filesystem::path &target) const
{
    if (algo == algorithm::error)
        return std::unexpected(
            std::make_error_code(std::errc::invalid_argument));
    if (algo != algorithm::dynamic)
    {
        if (!keyed())
            return checksum_file(target, algo);
        return checksum_file_key(target, key, algo);
    }
    if (pro.implement == nullptr)
        return std::unexpected(
            std::make_error_code(std::errc::operation_not_supported));
    auto result = pro.implement(target.c_str(), key.c_str());
    auto ec = std::make_error_code(static_cast<std::errc>(result.errcode));
    if (ec)
        return std::unexpected(ec);
    return std::string(result.result);
}
} // namespace tsubaki

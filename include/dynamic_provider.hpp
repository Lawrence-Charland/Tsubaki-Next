// SPDX-License-Identifier: GPL-2.0-only
// Copyright (C) 2026 Lawrence Charland

#ifndef LCPROJECTS_TSUBAKI_DYNAMIC_PROVIDER_V1_HPP
#define LCPROJECTS_TSUBAKI_DYNAMIC_PROVIDER_V1_HPP

#include <expected>
#include <filesystem>
#include <string>
#include <system_error>

namespace tsubaki::provider
{
extern "C"
{
    const char *tsubaki_provider_name() noexcept;

    bool tsubaki_is_keyed() noexcept;

    bool tsubaki_check_key(const std::string &key);

    void tsubaki_checksum_file(
        const std::filesystem::path &path, const std::string &key,
        std::expected<std::string, std::error_code> &out) noexcept;
}
} // namespace tsubaki::provider
#endif

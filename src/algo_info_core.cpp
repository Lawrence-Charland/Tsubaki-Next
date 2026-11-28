// SPDX-License-Identifier: GPL-2.0-only
// Copyright (C) 2026 Lawrence Charland

#include "algo_info_core.hpp"
#include "checksum.hpp"
#include "cmdline.hpp"
#include "dpframework.hpp"
#include "exit_codes.hpp"
#include <iostream>
#include <string>
#include <string_view>

namespace tsubaki
{
std::string_view get_info_builtin(algorithm algo)
{
    switch (algo)
    {
    case algorithm::md5:
        return "Digest:     16 bytes\n"
               "Key:        none\n"
               "Note:       MD5 (RFC 1321). Collisions are practical and cheap "
               "to\n"
               "            build, so this is a legacy format: match old "
               "manifests,\n"
               "            nothing else.";

    case algorithm::sha1:
        return "Digest:     20 bytes\n"
               "Key:        none\n"
               "Note:       SHA-1 (FIPS 180-4). Chosen-prefix collisions are "
               "known\n"
               "            (SHAttered, 2017). Legacy only; prefer sha256.";

    case algorithm::sha224:
        return "Digest:     28 bytes\n"
               "Key:        none\n"
               "Note:       SHA-224 (FIPS 180-4). Same compression function "
               "as\n"
               "            SHA-256 with a different IV; no advantage over "
               "sha256\n"
               "            unless a standard names it.";

    case algorithm::sha256:
        return "Digest:     32 bytes\n"
               "Key:        none\n"
               "Note:       SHA-256 (FIPS 180-4). The default choice: "
               "hardware\n"
               "            acceleration is everywhere and every manifest "
               "format\n"
               "            understands it.";

    case algorithm::sha384:
        return "Digest:     48 bytes\n"
               "Key:        none\n"
               "Note:       SHA-384 (FIPS 180-4). Truncated SHA-512; pick it "
               "when a\n"
               "            384-bit output is required by a standard.";

    case algorithm::sha512:
        return "Digest:     64 bytes\n"
               "Key:        none\n"
               "Note:       SHA-512 (FIPS 180-4). Faster than SHA-256 on "
               "64-bit\n"
               "            CPUs and leaves a wider margin.";

    case algorithm::sha3_256:
        return "Digest:     32 bytes\n"
               "Key:        none\n"
               "Note:       SHA3-256 (FIPS 202). A sponge, unrelated to "
               "SHA-2's\n"
               "            Merkle-Damgard design, so it hedges against a "
               "break in\n"
               "            that construction.";

    case algorithm::sha3_512:
        return "Digest:     64 bytes\n"
               "Key:        none\n"
               "Note:       SHA3-512 (FIPS 202). The same sponge as SHA3-256 "
               "with a\n"
               "            larger capacity and a longer output.";

    case algorithm::blake2b512:
        return "Digest:     64 bytes\n"
               "Key:        none\n"
               "Note:       BLAKE2b (RFC 7693). Very fast in software, "
               "especially on\n"
               "            64-bit machines.";

    case algorithm::blake2s256:
        return "Digest:     32 bytes\n"
               "Key:        none\n"
               "Note:       BLAKE2s (RFC 7693). The 32-bit-word variant, tuned "
               "for\n"
               "            smaller platforms.";

    case algorithm::hmac_md5:
        return "Digest:     16 bytes\n"
               "Key:        any length\n"
               "Note:       HMAC-MD5 (RFC 2104). HMAC needs a PRF, not "
               "collision\n"
               "            resistance, so this has held up far better than "
               "plain MD5;\n"
               "            there is still no reason to pick it today.";

    case algorithm::hmac_sha1:
        return "Digest:     20 bytes\n"
               "Key:        any length\n"
               "Note:       HMAC-SHA-1 (RFC 2104). Like HMAC-MD5 it survives "
               "its\n"
               "            hash's collisions, but prefer hmac_sha256.";

    case algorithm::hmac_sha224:
        return "Digest:     28 bytes\n"
               "Key:        any length\n"
               "Note:       HMAC-SHA-224 (RFC 2104). Same trade-off as sha224: "
               "no\n"
               "            practical advantage over hmac_sha256.";

    case algorithm::hmac_sha256:
        return "Digest:     32 bytes\n"
               "Key:        any length\n"
               "Note:       HMAC-SHA-256 (RFC 2104). The usual choice when a\n"
               "            manifest needs a Key.";

    case algorithm::hmac_sha384:
        return "Digest:     48 bytes\n"
               "Key:        any length\n"
               "Note:       HMAC-SHA-384 (RFC 2104). Truncated HMAC-SHA-512.";

    case algorithm::hmac_sha512:
        return "Digest:     64 bytes\n"
               "Key:        any length\n"
               "Note:       HMAC-SHA-512 (RFC 2104). Fast on 64-bit CPUs and "
               "gives\n"
               "            the widest margin.";

    case algorithm::hmac_sha3_256:
        return "Digest:     32 bytes\n"
               "Key:        any length\n"
               "Note:       HMAC-SHA3-256. HMAC (RFC 2104) over a FIPS 202 "
               "core; the\n"
               "            Keyed sponge that SHA-3 ships with is KMAC, not "
               "this.";

    case algorithm::hmac_sha3_512:
        return "Digest:     64 bytes\n"
               "Key:        any length\n"
               "Note:       HMAC-SHA3-512. HMAC (RFC 2104) over a FIPS 202 "
               "core,\n"
               "            with a 64-byte tag.";

    case algorithm::blake2b512_mac:
        return "Digest:     64 bytes\n"
               "Key:        1-64 bytes\n"
               "Note:       BLAKE2b Keyed mode (RFC 7693). The Key is mixed "
               "into the\n"
               "            hash instead of wrapped around it, so it is faster "
               "than\n"
               "            HMAC; the Key length is capped at 64 bytes.";

    case algorithm::blake2s256_mac:
        return "Digest:     32 bytes\n"
               "Key:        1-32 bytes\n"
               "Note:       BLAKE2s Keyed mode (RFC 7693). The same idea on "
               "32-bit\n"
               "            words; the Key is capped at 32 bytes.";

    case algorithm::none:
        return "Digest:     none\n"
               "Key:        none\n"
               "Note:       No algorithm: files are still listed, but not "
               "hashed.";
    case algorithm::dynamic:
        return "Digest:     varies\n"
               "Key:        varies\n"
               "Note:       Provided by a loaded .so; see that provider for "
               "details.";

    default:
        return "Digest:     none\n"
               "Key:        none\n"
               "Note:       No such algorithm.";
    }
}

int algo_info_core(argparser::ap_instance &cmdline)
{
    cmdline.evaluate(cmdline_group::algo_info);
    if (!cmdline.fine(false))
    {
        error_prompt(std::cerr, cmdline, false);
        return TSUBAKI_INVALID_OPERATION;
    }
    const std::string algo =
        cmdline.entry(cmdline_entry::algo_to_info).content.at(0);
    if (auto if_builtin = str_to_alg(algo); if_builtin != algorithm::error)
    {
        std::cout << "Source:     built-in" << std::endl;
        std::cout << get_info_builtin(if_builtin) << std::endl;
        return TSUBAKI_SUCCESS;
    }
    auto find_pro = dpf_instance::ins().find_provider(algo);
    if (find_pro.error != provider_msg::success)
    {
        std::cerr << "Error: invalid algorithm: \"" << algo << "\"."
                  << std::endl;
        return TSUBAKI_INVALID_OPERATION;
    }
    std::cout << "Source:     " << find_pro.display_path.string() << std::endl;
    std::cout << find_pro.algo_info() << std::endl;
    if (!find_pro.need_args())
        return TSUBAKI_SUCCESS;
    const char *defined = find_pro.arg_def();
    if (defined == nullptr)
    {
        std::cerr << "Fatal: the provider needs more args but didn't define "
                     "the ArgParser script."
                  << std::endl;
        return TSUBAKI_FATAL;
    }
    std::string argdef(defined);
    std::cout << "ArgParser script:" << std::endl << argdef;
    if (argdef.empty() || argdef.back() != '\n')
        std::cout << std::endl;
    return TSUBAKI_SUCCESS;
}
} // namespace tsubaki
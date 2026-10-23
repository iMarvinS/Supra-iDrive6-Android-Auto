// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// Required Notice: Copyright (c) 2026 nanaisu - NBT-AAD (https://github.com/Sq00ky/Supra-iDrive6-Android-Auto)
//
// license_text.hpp - embedded licensing notices for NBT-AAD.
//
// Header-only on purpose: this unit is not listed in SRCS / common.mk, so the
// functions are declared inline and carry no separate translation unit. It is
// built with GCC 4.4 (-Wc,-std=gnu++0x) on the QNX ARMv7LE toolchain, so it
// stays within C++03: no raw string literals, no <cstdint> dependency, plain
// printf only.
//
// printNbtAadLicense()      - full PolyForm Noncommercial License 1.0.0 text,
//                             emitted on `nbt-aad --license` / `--licence`.
// printNbtAadBannerNotice() - one-line Required Notice for the startup banner.

#ifndef NBT_LICENSE_TEXT_HPP
#define NBT_LICENSE_TEXT_HPP

#include <stdio.h>

namespace nbt {

// Short notice printed inside the startup banner (between the build line and
// the closing rule). Keep it to a single line so the banner box stays aligned.
inline void printNbtAadBannerNotice() {
    printf("  PolyForm Noncommercial License 1.0.0 - run with --license    \n");
    printf("  (c) 2026 nanaisu - Noncommercial use only                    \n");
}

// Full license text, reproduced verbatim from the PolyForm Noncommercial
// License 1.0.0 together with the project's Required Notice. Printed when the
// daemon is invoked with `--license` (or `--licence`).
inline void printNbtAadLicense() {
    printf(
        "NBT-AAD: native wireless Android Auto daemon for the BMW NBT Evo ID6\n"
        "head unit (Toyota GR Supra J29).\n"
        "Official source: https://github.com/Sq00ky/Supra-iDrive6-Android-Auto\n"
        "\n"
        "Required Notice: Copyright (c) 2026 nanaisu - NBT-AAD\n"
        "                 (https://github.com/Sq00ky/Supra-iDrive6-Android-Auto)\n"
        "\n"
        "This software is licensed under the following terms:\n"
        "\n"
        "# PolyForm Noncommercial License 1.0.0\n"
        "\n"
        "<https://polyformproject.org/licenses/noncommercial/1.0.0>\n"
        "\n"
        "## Acceptance\n"
        "\n"
        "In order to get any license under these terms, you must agree to them as\n"
        "both strict obligations and conditions to all your licenses.\n"
        "\n"
        "## Copyright License\n"
        "\n"
        "The licensor grants you a copyright license for the software to do\n"
        "everything you might do with the software that would otherwise infringe\n"
        "the licensor's copyright in it for any permitted purpose.  However, you\n"
        "may only distribute the software according to Distribution License and\n"
        "make changes or new works based on the software according to Changes and\n"
        "New Works License.\n"
        "\n"
        "## Distribution License\n"
        "\n"
        "The licensor grants you an additional copyright license to distribute\n"
        "copies of the software.  Your license to distribute covers distributing\n"
        "the software with changes and new works permitted by Changes and New\n"
        "Works License.\n"
        "\n"
        "## Notices\n"
        "\n"
        "You must ensure that anyone who gets a copy of any part of the software\n"
        "from you also gets a copy of these terms or the URL for them above, as\n"
        "well as copies of any plain-text lines beginning with `Required Notice:`\n"
        "that the licensor provided with the software.  For example:\n"
        "\n"
        "> Required Notice: Copyright Yoyodyne, Inc. (http://example.com)\n"
        "\n"
        "## Changes and New Works License\n"
        "\n"
        "The licensor grants you an additional copyright license to make changes\n"
        "and new works based on the software for any permitted purpose.\n"
        "\n"
        "## Patent License\n"
        "\n"
        "The licensor grants you a patent license for the software that covers\n"
        "patent claims the licensor can license, or becomes able to license, that\n"
        "you would infringe by using the software.\n"
        "\n"
        "## Noncommercial Purposes\n"
        "\n"
        "Any noncommercial purpose is a permitted purpose.\n"
        "\n"
        "## Personal Uses\n"
        "\n"
        "Personal use for research, experiment, and testing for the benefit of\n"
        "public knowledge, personal study, private entertainment, hobby projects,\n"
        "amateur pursuits, or religious observance, without any anticipated\n"
        "commercial application, is use for a permitted purpose.\n"
        "\n"
        "## Noncommercial Organizations\n"
        "\n"
        "Use by any charitable organization, educational institution, public\n"
        "research organization, public safety or health organization,\n"
        "environmental protection organization, or government institution is use\n"
        "for a permitted purpose regardless of the source of funding or\n"
        "obligations resulting from the funding.\n"
        "\n"
        "## Fair Use\n"
        "\n"
        "You may have \"fair use\" rights for the software under the law. These\n"
        "terms do not limit them.\n"
        "\n"
        "## No Other Rights\n"
        "\n"
        "These terms do not allow you to sublicense or transfer any of your\n"
        "licenses to anyone else, or prevent the licensor from granting licenses\n"
        "to anyone else.  These terms do not imply any other licenses.\n"
        "\n"
        "## Patent Defense\n"
        "\n"
        "If you make any written claim that the software infringes or contributes\n"
        "to infringement of any patent, your patent license for the software\n"
        "granted under these terms ends immediately. If your company makes such a\n"
        "claim, your patent license ends immediately for work on behalf of your\n"
        "company.\n"
        "\n"
        "## Violations\n"
        "\n"
        "The first time you are notified in writing that you have violated any of\n"
        "these terms, or done anything with the software not covered by your\n"
        "licenses, your licenses can nonetheless continue if you come into full\n"
        "compliance with these terms, and take practical steps to correct past\n"
        "violations, within 32 days of receiving notice.  Otherwise, all your\n"
        "licenses end immediately.\n"
        "\n"
        "## No Liability\n"
        "\n"
        "*** As far as the law allows, the software comes as is, without any\n"
        "warranty or condition, and the licensor will not be liable to you for any\n"
        "damages arising out of these terms or the use or nature of the software,\n"
        "under any kind of legal claim. ***\n"
        "\n"
        "## Definitions\n"
        "\n"
        "The **licensor** is the individual or entity offering these terms, and\n"
        "the **software** is the software the licensor makes available under these\n"
        "terms.\n"
        "\n"
        "**You** refers to the individual or entity agreeing to these terms.\n"
        "\n"
        "**Your company** is any legal entity, sole proprietorship, or other kind\n"
        "of organization that you work for, plus all organizations that have\n"
        "control over, are under the control of, or are under common control with\n"
        "that organization.  **Control** means ownership of substantially all the\n"
        "assets of an entity, or the power to direct its management and policies\n"
        "by vote, contract, or otherwise.  Control can be direct or indirect.\n"
        "\n"
        "**Your licenses** are all the licenses granted to you for the software\n"
        "under these terms.\n"
        "\n"
        "**Use** means anything you do with the software requiring one of your\n"
        "licenses.\n");
}

} // namespace nbt

#endif // NBT_LICENSE_TEXT_HPP

# SPDX-FileCopyrightText: 2026 Roman Leonov <jam_roma@yahoo.com>
#
# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.20.0)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(test_uhc_interrupt_loopback)

target_sources(app PRIVATE
    ../common/test_uhc_common.c
    ../common/test_uhc_hub_common.c
    src/test_uhc_interrupt_loopback.c
)

target_include_directories(app PRIVATE ../common)

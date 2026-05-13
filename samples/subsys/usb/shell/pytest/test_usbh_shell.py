# Copyright (c) 2026 Roman Leonov <jam_roma@yahoo.com>
#
# SPDX-License-Identifier: Apache-2.0

import logging
import re
import time

from twister_harness import Shell

logger = logging.getLogger(__name__)
ANSI = re.compile(r"\x1B\[[0-?]*[ -/]*[@-~]")


def _contains(lines, needle: str) -> bool:
    return any(needle in line for line in lines)


def _match(lines, pattern: str) -> bool:
    rx = re.compile(pattern)
    return any(rx.search(line) for line in lines)


def wait_for_device(shell, timeout_s=10.0, poll_s=0.5):
    end = time.time() + timeout_s
    last_resp = []
    while time.time() < end:
        last_resp = shell.exec_command("usbh device list")
        # There should be decimal number in the response
        if any(re.match(r"^\s*\d+\s*$", dev_number) for dev_number in last_resp):
            return last_resp
        time.sleep(poll_s)
    raise AssertionError(f"No USB device within {timeout_s}s. Last output:\n{last_resp}")


def test_usbh_init_enable_and_list(shell: Shell):
    # usbh init
    logger.info('run "usbh init"')
    lines = shell.exec_command("usbh init")
    assert _contains(lines, "host: USB host initialized"), f"Unexpected init output:\n{lines}"

    # usbh enable
    logger.info('run "usbh enable"')
    lines = shell.exec_command("usbh enable")
    assert _contains(lines, "host: USB host enabled"), f"Unexpected enable output:\n{lines}"

    # usbh device list -> poll the list and expect at least one device
    wait_for_device(shell, timeout_s=5.0, poll_s=0.5)

    logger.info('run "usbh device descriptor device 1"')
    lines = shell.exec_command("usbh device descriptor device 1")

    # Remove ANSI symbols from the lines
    clean_lines = [ANSI.sub("", symbol) for symbol in lines]

    # Check a few stable anchors of device descriptor
    assert _match(clean_lines, r"^\s*bLength\s+18\s*$"), (
        f"Missing Device bLength 18:\n{clean_lines}"
    )
    assert _match(clean_lines, r"^\s*bDescriptorType\s+1\s*$"), (
        f"Missing Device bDescriptorType 1:\n{clean_lines}"
    )

    logger.info('run "usbh device descriptor configuration 1 0"')
    lines = shell.exec_command("usbh device descriptor configuration 1 0")

    # Remove ANSI symbols from the lines
    clean_lines = [ANSI.sub("", symbol) for symbol in lines]

    # Check a few stable anchors of configuration descriptor
    assert _match(clean_lines, r"^\s*bLength\s+9\s*$"), f"Missing Config bLength 9:\n{clean_lines}"
    assert _match(clean_lines, r"^\s*bDescriptorType\s+2\s*$"), (
        f"Missing Config bDescriptorType 2:\n{clean_lines}"
    )

    # String 1: Manufacturer
    logger.info('run "usbh device descriptor string 1 1 1"')
    lines = shell.exec_command("usbh device descriptor string 1 1 1")
    assert _match(lines, r"^00000000:"), f"Expected hexdump for string 1, got:\n{lines}"

    # String 2: Product
    logger.info('run "usbh device descriptor string 1 1 2"')
    lines = shell.exec_command("usbh device descriptor string 1 1 2")
    assert _match(lines, r"^00000000:"), f"Expected hexdump for string 2, got:\n{lines}"

    # String 3: Serial
    logger.info('run "usbh device descriptor string 1 1 3"')
    lines = shell.exec_command("usbh device descriptor string 1 1 3")
    assert _match(lines, r"^00000000:"), f"Expected hexdump for string 3, got:\n{lines}"

    # String 4: Product
    logger.info('run "usbh device descriptor string 1 1 4"')
    lines = shell.exec_command("usbh device descriptor string 1 1 4")
    assert _match(lines, r"^00000000:"), f"Expected hexdump for string 4, got:\n{lines}"

    # String 5: Product
    logger.info('run "usbh device descriptor string 1 1 5"')
    lines = shell.exec_command("usbh device descriptor string 1 1 5")
    assert _match(lines, r"^00000000:"), f"Expected hexdump for string 5, got:\n{lines}"

    # String 6: STALL string request
    logger.info('run "usbh device descriptor string 1 1 6"')
    lines = shell.exec_command("usbh device descriptor string 1 1 6")
    assert _contains(lines, "host: Failed to request string descriptor"), (
        f"Expected failure message for string 6, got:\n{lines}"
    )

    # String 1: Manufacturer
    logger.info('run "usbh device descriptor string 1 1 1"')
    lines = shell.exec_command("usbh device descriptor string 1 1 1")
    assert _match(lines, r"^00000000:"), f"Expected hexdump for string 1, got:\n{lines}"

    # String 2: Product
    logger.info('run "usbh device descriptor string 1 1 2"')
    lines = shell.exec_command("usbh device descriptor string 1 1 2")
    assert _match(lines, r"^00000000:"), f"Expected hexdump for string 2, got:\n{lines}"

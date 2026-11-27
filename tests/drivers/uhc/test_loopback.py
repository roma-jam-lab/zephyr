#!/usr/bin/env python3
# Copyright (c) 2026 Roman Leonov <jam_roma@yahoo.com>
# SPDX-License-Identifier: Apache-2.0

import struct

import usb.core
import usb.util

VID = 0x2FE3
PID = 0x0100

SET_PAYLOAD = 0x01
GET_PAYLOAD = 0x02
ARM_BULK = 0x03
ARM_INTR = 0x04

BULK_OUT_EP = 0x01
BULK_IN_EP = 0x81

INTR_OUT_EP = 0x02
INTR_IN_EP = 0x82

TIMEOUT = 2000

CONTROL_SIZES = [1, 8, 63, 64, 65, 128, 129, 256]
BULK_SIZES = [1, 8, 63, 64, 65, 128, 129, 256, 512, 1024]
INTR_SIZES = [1, 8, 16, 32, 63, 64]


def make_payload(size):
    return bytes(i % 256 for i in range(size))


def arm_echo(dev, request, req_len, resp_len, host_in_len):
    params = struct.pack("<HHH", req_len, resp_len, host_in_len)

    written = dev.ctrl_transfer(
        0x41,
        request,
        0,
        0,
        params,
        timeout=TIMEOUT,
    )

    assert written == len(params), (
        f"ECHO preparation failed: expected {len(params)}, wrote {written}"
    )


def test_control(dev):
    print("\nTesting CONTROL Endpoint loopback")

    for size in CONTROL_SIZES:
        expected = make_payload(size)

        written = dev.ctrl_transfer(0x41, SET_PAYLOAD, 0, 0, expected, timeout=TIMEOUT)
        assert written == size, f"CONTROL OUT: expected {size}, wrote {written}"

        received = dev.ctrl_transfer(0xC1, GET_PAYLOAD, 0, 0, size, timeout=TIMEOUT)

        actual = bytes(received)

        assert actual == expected, (
            f"CONTROL mismatch at {size} bytes:\n"
            f"Expected: {expected.hex()}\n"
            f"Received: {actual.hex()}"
        )

        print(f"PASS: CONTROL {size} bytes")


def test_bulk(dev):
    print("\nTesting BULK Endpoint loopback")

    for size in BULK_SIZES:
        expected = make_payload(size)

        # Prepare the device to receive exactly 'size' bytes.
        arm_echo(dev, ARM_BULK, size, size, size)

        # BULK OUT
        written = dev.write(BULK_OUT_EP, expected, timeout=TIMEOUT)

        assert written == size, f"BULK OUT: expected {size}, wrote {written}"

        # BULK IN
        received = dev.read(BULK_IN_EP, size, timeout=TIMEOUT)

        actual = bytes(received)

        assert actual == expected, (
            f"BULK mismatch at {size} bytes:\nExpected: {expected.hex()}\nReceived: {actual.hex()}"
        )

        print(f"PASS: BULK {size} bytes")


def test_interrupt(dev):
    print("\nTesting INTERRUPT Endpoint loopback")

    for size in INTR_SIZES:
        expected = make_payload(size)

        # Prepare INTERRUPT ECHO.
        arm_echo(dev, ARM_INTR, size, size, size)

        # INTERRUPT OUT
        written = dev.write(INTR_OUT_EP, expected, timeout=TIMEOUT)

        assert written == size, f"INTERRUPT OUT: expected {size}, wrote {written}"

        # INTERRUPT IN
        received = dev.read(INTR_IN_EP, size, timeout=TIMEOUT)
        actual = bytes(received)

        assert actual == expected, (
            f"INTERRUPT mismatch at {size} bytes:\n"
            f"Expected: {expected.hex()}\n"
            f"Received: {actual.hex()}"
        )

        print(f"PASS: INTERRUPT {size} bytes")


def main():
    dev = usb.core.find(idVendor=VID, idProduct=PID)

    if dev is None:
        raise RuntimeError("USB loopback device not found")

    print(f"Found USB loopback device: {VID:04x}:{PID:04x}")

    try:
        # Avoid resetting an already configured device.
        try:
            cfg = dev.get_active_configuration()
        except usb.core.USBError:
            cfg = None

        if cfg is None:
            dev.set_configuration()

        test_control(dev)
        test_bulk(dev)
        test_interrupt(dev)

        print("\nAll CONTROL, BULK and INTR loopback tests passed")

    finally:
        usb.util.dispose_resources(dev)


if __name__ == "__main__":
    main()

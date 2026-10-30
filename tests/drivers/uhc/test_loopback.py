
import usb.core
import usb.util

# Zephyr Project VID
VID = 0x2FE3
PID = 0x0100

# Vendor Specific Commands
SET_PAYLOAD = 0x01
GET_PAYLOAD = 0x02

dev = usb.core.find(idVendor=VID, idProduct=PID)
if dev is None:
    raise RuntimeError("USB loopback device not found")

dev.set_configuration()

# Test sizes that cross EP0 packet boundaries.
for size in [1, 8, 63, 64, 65, 128, 129, 256]:
    expected = bytes(i % 256 for i in range(size))

    written = dev.ctrl_transfer(
        0x41, SET_PAYLOAD, 0, 0, expected, timeout=2000
    )
    assert written == size, f"OUT size {size}: wrote {written}"

    received = dev.ctrl_transfer(
        0xC1, GET_PAYLOAD, 0, 0, size, timeout=2000
    )

    actual = bytes(received)

    assert actual == expected, (
        f"Mismatch at size {size}:\n"
        f"Expected: {expected.hex()}\n"
        f"Received: {actual.hex()}"
    )

    print(f"PASS: {size} bytes")

usb.util.dispose_resources(dev)

print("All CONTROL loopback tests passed")

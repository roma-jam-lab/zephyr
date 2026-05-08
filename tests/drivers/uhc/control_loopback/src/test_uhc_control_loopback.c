/*
 * SPDX-FileCopyrightText: 2026 Roman Leonov <jam_roma@yahoo.com>
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <string.h>

#include <zephyr/ztest.h>
#include <zephyr/drivers/usb/uhc.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/logging/log.h>
#include <zephyr/random/random.h>

#include "test_uhc_common.h"
#include "test_uhc_hub_common.h"

LOG_MODULE_REGISTER(uhc_control_loopback, LOG_LEVEL_INF);

#define TEST_HUB_ADDR		1
#define TEST_HUB_IFACE		0
#define TEST_HUB_PORT		2
#define TEST_CHILD_ADDR		2

#define LOOPBACK_VID		0x2FE3
#define LOOPBACK_PID		0x0100
#define LOOPBACK_SET_DATA	0x01
#define LOOPBACK_GET_DATA	0x02
#define LOOPBACK_TEST_MAX_LEN	1024

uint8_t test_expected_buf[LOOPBACK_TEST_MAX_LEN];
uint8_t test_actual_buf[LOOPBACK_TEST_MAX_LEN];

static void loopback_set_payload(struct usb_device *udev, const uint8_t *data, size_t len)
{
	const struct device *dev = test_uhc_get_dev();
	struct usb_setup_packet setup = {
		.bmRequestType = 0x41, /* Vendor, interface, host-to-device */
		.bRequest = LOOPBACK_SET_DATA,
		.wValue = 0,
		.wIndex = 0,
		.wLength = sys_cpu_to_le16(len),
	};
	struct uhc_transfer *xfer;
	int ret;

	xfer = uhc_xfer_alloc_with_buf(dev, 0x00, udev, NULL, NULL, len);
	zassert_not_null(xfer, "Unable to allocate CONTROL OUT transfer");
	zassert_not_null(xfer->buf, "Missing CONTROL OUT buffer");
	memcpy(xfer->setup_pkt, &setup, sizeof(setup));
	net_buf_add_mem(xfer->buf, data, len);

	ret = uhc_ep_enqueue(dev, xfer);
	zassert_equal(ret, 0, "CONTROL OUT enqueue failed: %d", ret);
	test_uhc_wait_ep_request();
	zassert_equal(xfer->err, 0, "CONTROL OUT failed: %d", xfer->err);

	uhc_xfer_buf_free(dev, xfer->buf);
	uhc_xfer_free(dev, xfer);
}

static void loopback_get_payload(struct usb_device *udev, uint8_t *data, size_t req_len, size_t resp_len)
{
	const struct device *dev = test_uhc_get_dev();
	struct usb_setup_packet setup = {
		.bmRequestType = 0xC1, /* Vendor, interface, device-to-host */
		.bRequest = LOOPBACK_GET_DATA,
		.wValue = 0,
		.wIndex = 0,
		.wLength = sys_cpu_to_le16(req_len),
	};
	struct uhc_transfer *xfer;
	int ret;

	xfer = uhc_xfer_alloc_with_buf(dev, 0x80, udev, NULL, NULL, req_len);
	zassert_not_null(xfer, "Unable to allocate CONTROL IN transfer");
	zassert_not_null(xfer->buf, "Missing CONTROL IN buffer");
	memcpy(xfer->setup_pkt, &setup, sizeof(setup));

	ret = uhc_ep_enqueue(dev, xfer);
	zassert_equal(ret, 0, "CONTROL IN enqueue failed: %d", ret);
	test_uhc_wait_ep_request();
	zassert_equal(xfer->err, 0, "CONTROL IN failed: %d", xfer->err);
	zassert_equal(xfer->buf->len, resp_len,
				  "CONTROL IN length mismatch: got %u, expected %zu",
				  xfer->buf->len, resp_len);
	memcpy(data, xfer->buf->data, resp_len);

	uhc_xfer_buf_free(dev, xfer->buf);
	uhc_xfer_free(dev, xfer);
}

static void loopback_verify_control_with_random_data(struct usb_device *udev, size_t req_len, size_t resp_len)
{
	/* Prepare the data buffer */
	memset(test_expected_buf, 0x00, LOOPBACK_TEST_MAX_LEN);
	memset(test_actual_buf, 0x00, LOOPBACK_TEST_MAX_LEN);

	sys_rand_get(test_expected_buf, resp_len);

	/* Set payload */
	loopback_set_payload(udev, test_expected_buf, resp_len);

	/* Get payload */
	loopback_get_payload(udev, test_actual_buf, req_len, resp_len);

	zassert_mem_equal(test_actual_buf, test_expected_buf, resp_len,
			  "CONTROL loopback payload mismatch");

	LOG_INF("CONTROL loopback: req_len=%zu, resp_len=%zu", req_len, resp_len);
}

ZTEST(uhc_control_loopback, test_control_loopback_via_hub)
{
	struct usb_device hub_udev;
	struct usb_device child_udev;
	struct test_uhc_hub_info hub;
	enum usb_device_speed speed;
	struct usb_cfg_descriptor cfg;

	test_uhc_init();
	test_uhc_prepare_addressed_device(&hub_udev, &speed, TEST_HUB_ADDR);
	hub_udev.speed = speed;
	hub_udev.level = 1;
	zassert_equal(speed, USB_SPEED_SPEED_HS, "Expected high-speed hub");

	test_uhc_hub_init(&hub_udev, TEST_HUB_IFACE, &hub);
	test_uhc_hub_power_ports(&hub_udev, &hub);
	test_uhc_hub_prepare_child(&hub_udev, TEST_HUB_PORT,
							   TEST_CHILD_ADDR, &child_udev);

	zassert_equal(child_udev.speed, USB_SPEED_SPEED_FS,
				  "Expected full-speed loopback device");
	zassert_equal(sys_le16_to_cpu(child_udev.dev_desc.idVendor),
				  LOOPBACK_VID, "Unexpected loopback VID");
	zassert_equal(sys_le16_to_cpu(child_udev.dev_desc.idProduct),
				  LOOPBACK_PID, "Unexpected loopback PID");

	test_uhc_dev_get_cfg_desc(&child_udev, &cfg, sizeof(cfg));
	zassert_equal(cfg.bDescriptorType, USB_DESC_CONFIGURATION,
				  "Invalid configuration descriptor");
	zassert_equal(cfg.bNumInterfaces, 1, "Expected one interface");
	test_uhc_dev_set_config(&child_udev, cfg.bConfigurationValue);

	loopback_verify_control_with_random_data(&child_udev, 1, 1);
	loopback_verify_control_with_random_data(&child_udev, 6, 6);
	loopback_verify_control_with_random_data(&child_udev, 8, 8);
	loopback_verify_control_with_random_data(&child_udev, 64, 64);
	loopback_verify_control_with_random_data(&child_udev, 65, 65);
	loopback_verify_control_with_random_data(&child_udev, 128, 128);
	loopback_verify_control_with_random_data(&child_udev, 129, 129);
	loopback_verify_control_with_random_data(&child_udev, 256, 256);
	loopback_verify_control_with_random_data(&child_udev, 257, 257);
	loopback_verify_control_with_random_data(&child_udev, 511, 511);
	loopback_verify_control_with_random_data(&child_udev, 512, 512);
	loopback_verify_control_with_random_data(&child_udev, 513, 513);

	/* Clean up */
	test_uhc_device_cleanup();
}

ZTEST(uhc_control_loopback, test_control_loopback_via_hub_short_response)
{
	struct usb_device hub_udev;
	struct usb_device child_udev;
	struct test_uhc_hub_info hub;
	enum usb_device_speed speed;
	struct usb_cfg_descriptor cfg;

	test_uhc_init();
	test_uhc_prepare_addressed_device(&hub_udev, &speed, TEST_HUB_ADDR);
	hub_udev.speed = speed;
	hub_udev.level = 1;
	zassert_equal(speed, USB_SPEED_SPEED_HS, "Expected high-speed hub");

	test_uhc_hub_init(&hub_udev, TEST_HUB_IFACE, &hub);
	test_uhc_hub_power_ports(&hub_udev, &hub);
	test_uhc_hub_prepare_child(&hub_udev, TEST_HUB_PORT,
							   TEST_CHILD_ADDR, &child_udev);

	zassert_equal(child_udev.speed, USB_SPEED_SPEED_FS,
				  "Expected full-speed loopback device");
	zassert_equal(sys_le16_to_cpu(child_udev.dev_desc.idVendor),
				  LOOPBACK_VID, "Unexpected loopback VID");
	zassert_equal(sys_le16_to_cpu(child_udev.dev_desc.idProduct),
				  LOOPBACK_PID, "Unexpected loopback PID");

	test_uhc_dev_get_cfg_desc(&child_udev, &cfg, sizeof(cfg));
	zassert_equal(cfg.bDescriptorType, USB_DESC_CONFIGURATION,
				  "Invalid configuration descriptor");
	zassert_equal(cfg.bNumInterfaces, 1, "Expected one interface");
	test_uhc_dev_set_config(&child_udev, cfg.bConfigurationValue);

	loopback_verify_control_with_random_data(&child_udev, 64, 6);
	loopback_verify_control_with_random_data(&child_udev, 128, 63);
	loopback_verify_control_with_random_data(&child_udev, 128, 64);
	loopback_verify_control_with_random_data(&child_udev, 128, 65);

	/* Clean up */
	test_uhc_device_cleanup();
}

ZTEST_SUITE(uhc_control_loopback, NULL, NULL, NULL, NULL, NULL);

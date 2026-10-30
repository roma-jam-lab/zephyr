/*
 * Copyright (c) 2026 Roman Leonov <jam_roma@yahoo.com>
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/ztest.h>
#include <zephyr/drivers/usb/uhc.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/logging/log.h>

#include "test_uhc_common.h"
#include "test_uhc_hub_common.h"

LOG_MODULE_REGISTER(uhc_control_child, LOG_LEVEL_INF);

#define TEST_HUB_ADDR          1
#define TEST_HUB_IFACE         0
#define TEST_HUB_PORT          2
#define TEST_CHILD_ADDR        2

#define CP210X_REQ_SET_BAUDRATE  0x1E
#define CP210X_REQ_GET_BAUDRATE  0x1D
#define CP210X_TEST_BAUDRATE     115200U


static void test_uhc_select_ifc(struct usb_device *udev)
{
	const struct device *uhc_dev = test_uhc_get_dev();
	struct usb_setup_packet setup = {
		.bmRequestType = 0x41,
		.bRequest = 0x00, /* IFC_ENABLE */
		.wValue = sys_cpu_to_le16(1),
		.wIndex = 0,
		.wLength = 0,
	};
	struct uhc_transfer *xfer;
	int ret;

	xfer = uhc_xfer_alloc(uhc_dev, 0x80, udev, NULL, NULL);

	zassert_not_null(xfer, "Failed to allocate CONTROL IN transfer");

	memcpy(xfer->setup_pkt, &setup, sizeof(setup));

	ret = uhc_ep_enqueue(uhc_dev, xfer);
	zassert_equal(ret, 0, "SET IFC enqueue failed: %d", ret);

	test_uhc_wait_ep_request();

	zassert_equal(xfer->err, 0,
		      "SET IFC failed: %d", xfer->err);

	uhc_xfer_free(uhc_dev, xfer);
}

static void test_uhc_child_set_baudrate(struct usb_device *udev, uint32_t baudrate_to_set)
{
	const struct device *uhc_dev = test_uhc_get_dev();
	struct usb_setup_packet setup = {
		.bmRequestType = 0x41, /* Host-to-device, vendor, interface */
		.bRequest = CP210X_REQ_SET_BAUDRATE,
		.wValue = 0,
		.wIndex = 0,
		.wLength = sys_cpu_to_le16(sizeof(uint32_t)),
	};
	struct uhc_transfer *xfer;
	uint8_t baudrate_buf[4];
	int ret;

	/* Baud rate is transferred as a little-endian 32-bit value. */
	sys_put_le32(baudrate_to_set, baudrate_buf);

	xfer = uhc_xfer_alloc_with_buf(uhc_dev, 0x00, udev,
				      NULL, NULL, sizeof(baudrate_buf));

	zassert_not_null(xfer, "Failed to allocate CONTROL OUT transfer");
	zassert_not_null(xfer->buf, "Missing CONTROL OUT data buffer");

	memcpy(xfer->setup_pkt, &setup, sizeof(setup));
	net_buf_add_mem(xfer->buf, baudrate_buf, sizeof(baudrate_buf));

	LOG_INF("CP210x SET_BAUDRATE: %u", baudrate_to_set);

	ret = uhc_ep_enqueue(uhc_dev, xfer);
	zassert_equal(ret, 0, "CONTROL OUT enqueue failed: %d", ret);

	test_uhc_wait_ep_request();

	LOG_INF("CONTROL OUT result: %d", xfer->err);

	/*
	 * For this initial test, require successful completion.
	 * We want to detect problems in the SPLIT handling.
	 */
	zassert_equal(xfer->err, 0,
		      "CONTROL OUT SPLIT failed: %d", xfer->err);

	uhc_xfer_buf_free(uhc_dev, xfer->buf);
	uhc_xfer_free(uhc_dev, xfer);
}

static uint32_t test_uhc_child_get_baudrate(struct usb_device *udev)
{
	const struct device *uhc_dev = test_uhc_get_dev();
	struct usb_setup_packet setup = {
		.bmRequestType = 0xC1, /* Device-to-host, vendor, interface */
		.bRequest = CP210X_REQ_GET_BAUDRATE,
		.wValue = 0,
		.wIndex = 0,
		.wLength = sys_cpu_to_le16(sizeof(uint32_t)),
	};
	struct uhc_transfer *xfer;
	uint32_t baudrate;
	int ret;

	xfer = uhc_xfer_alloc_with_buf(uhc_dev, 0x80, udev,
				      NULL, NULL, sizeof(uint32_t));

	zassert_not_null(xfer, "Failed to allocate CONTROL IN transfer");
	zassert_not_null(xfer->buf, "Missing CONTROL IN data buffer");

	memcpy(xfer->setup_pkt, &setup, sizeof(setup));

	ret = uhc_ep_enqueue(uhc_dev, xfer);
	zassert_equal(ret, 0, "GET_BAUDRATE enqueue failed: %d", ret);

	test_uhc_wait_ep_request();

	zassert_equal(xfer->err, 0,
		      "GET_BAUDRATE failed: %d", xfer->err);

	zassert_equal(xfer->buf->len, sizeof(uint32_t),
		      "Unexpected GET_BAUDRATE response length: %u",
		      xfer->buf->len);

	baudrate = sys_get_le32(xfer->buf->data);

	LOG_INF("CP210x GET_BAUDRATE: %u", baudrate);

	uhc_xfer_buf_free(uhc_dev, xfer->buf);
	uhc_xfer_free(uhc_dev, xfer);

	return baudrate;
}

ZTEST(uhc_control_child, test_control_child_cp21xx_set_and_get_baudrate)
{
	struct usb_device hub_udev;
	struct usb_device child_udev;
	struct test_uhc_hub_info hub;
	enum usb_device_speed speed;
	uint32_t actual_baudrate;

	test_uhc_init();

	/* Enumerate the high-speed hub. */
	test_uhc_prepare_addressed_device(
	&hub_udev, &speed, TEST_HUB_ADDR);

	hub_udev.speed = speed;
	hub_udev.level = 1;

	zassert_equal(speed, USB_SPEED_SPEED_HS,
			"Expected a high-speed hub");

	test_uhc_hub_init(&hub_udev, TEST_HUB_IFACE, &hub);
	test_uhc_hub_power_ports(&hub_udev, &hub);

	/* Reset and enumerate the downstream device. */
	test_uhc_hub_prepare_child(
	&hub_udev,
	TEST_HUB_PORT,
	TEST_CHILD_ADDR,
	&child_udev);

	zassert_equal(child_udev.speed, USB_SPEED_SPEED_FS,
			"Expected a full-speed child");

	LOG_INF("Child VID=%04x PID=%04x",
		sys_le16_to_cpu(child_udev.dev_desc.idVendor),
		sys_le16_to_cpu(child_udev.dev_desc.idProduct));

	zassert_equal(sys_le16_to_cpu(child_udev.dev_desc.idVendor),
	      0x10C4, "Unexpected VID");
	zassert_equal(sys_le16_to_cpu(child_udev.dev_desc.idProduct),
	      0xEA60, "Unexpected PID");


	test_uhc_select_ifc(&child_udev);

	test_uhc_child_set_baudrate(&child_udev, CP210X_TEST_BAUDRATE);

	actual_baudrate = test_uhc_child_get_baudrate(&child_udev);

	zassert_equal(actual_baudrate, CP210X_TEST_BAUDRATE,
		"Baud rate mismatch: expected %u, got %u",
		CP210X_TEST_BAUDRATE, actual_baudrate);

	test_uhc_device_cleanup();
}

ZTEST(uhc_control_child, test_control_child_get_configuration_descriptor)
{
	struct usb_device hub_udev;
	struct usb_device child_udev;
	struct test_uhc_hub_info hub;
	struct usb_cfg_descriptor cfg;
	enum usb_device_speed speed;
	uint8_t cfg_buf[TEST_UHC_MAX_XFER_DATA_BUF_SIZE];
	uint16_t total_len;

	test_uhc_init();

	/* Enumerate the high-speed hub. */
	test_uhc_prepare_addressed_device(
	&hub_udev, &speed, TEST_HUB_ADDR);

	hub_udev.speed = speed;
	hub_udev.level = 1;

	zassert_equal(speed, USB_SPEED_SPEED_HS,
			"Expected a high-speed hub");

	test_uhc_hub_init(&hub_udev, TEST_HUB_IFACE, &hub);
	test_uhc_hub_power_ports(&hub_udev, &hub);

	/* Reset and enumerate the downstream device. */
	test_uhc_hub_prepare_child(
	&hub_udev,
	TEST_HUB_PORT,
	TEST_CHILD_ADDR,
	&child_udev);

	zassert_equal(child_udev.speed, USB_SPEED_SPEED_FS,
			"Expected a full-speed child");

	LOG_INF("Child VID=%04x PID=%04x",
		sys_le16_to_cpu(child_udev.dev_desc.idVendor),
		sys_le16_to_cpu(child_udev.dev_desc.idProduct));

	/* CONTROL IN */

	/* Get first 9 bytes of Configuration Descriptor */
	test_uhc_dev_get_cfg_desc(
	&child_udev, &cfg, sizeof(cfg));

	zassert_equal(cfg.bDescriptorType,
			USB_DESC_CONFIGURATION,
			"Invalid configuration descriptor");

	zassert_true(cfg.bNumInterfaces > 0,
			"No interfaces in downstream device");

	/* Get the while Configuration Descriptor */
	total_len = sys_le16_to_cpu(cfg.wTotalLength);

	zassert_true(total_len >= sizeof(cfg),
		"Invalid configuration descriptor length: %u", total_len);

	zassert_true(total_len <= sizeof(cfg_buf),
		"Configuration descriptor too large: %u", total_len);

	/* Read the complete configuration descriptor set. */
	test_uhc_dev_get_cfg_desc(&child_udev, cfg_buf, total_len);

	/* CONTROL OUT*/

	/* Simple Set Configuration, with no DATA stage. */
	test_uhc_dev_set_config(&child_udev, cfg.bConfigurationValue);

	test_uhc_device_cleanup();
}

ZTEST_SUITE(uhc_control_child, NULL, NULL, NULL, NULL, NULL);

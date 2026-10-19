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

	/* CONTROL OUT with no DATA stage. */
	test_uhc_dev_set_config(
	&child_udev, cfg.bConfigurationValue);

	LOG_INF("Downstream CONTROL IN/OUT completed");

	test_uhc_device_cleanup();
}

ZTEST_SUITE(uhc_control_child, NULL, NULL, NULL, NULL, NULL);

/*
 * SPDX-FileCopyrightText: 2026 Roman Leonov <jam_roma@yahoo.com>
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <string.h>

#include <zephyr/ztest.h>
#include <zephyr/drivers/usb/uhc.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/random/random.h>
#include <zephyr/logging/log.h>

#include "test_uhc_common.h"
#include "test_uhc_hub_common.h"

LOG_MODULE_REGISTER(uhc_interrupt_loopback, LOG_LEVEL_INF);

#define TEST_HUB_ADDR               1
#define TEST_HUB_IFACE              0
#define TEST_HUB_PORT               2
#define TEST_CHILD_ADDR             2

#define LOOPBACK_VID                0x2FE3
#define LOOPBACK_PID                0x0100
#define LOOPBACK_INTERFACE_NUMBER   0

#define LOOPBACK_INTR_REQ_ECHO      0x04
#define LOOPBACK_INTR_OUT           0x02
#define LOOPBACK_INTR_IN            0x82
#define LOOPBACK_INTR_MPS           64U

static struct usb_ep_descriptor intr_out_desc;
static struct usb_ep_descriptor intr_in_desc;

static uint8_t test_expected_buf[LOOPBACK_INTR_MPS];
static uint8_t test_actual_buf[LOOPBACK_INTR_MPS];

static void loopback_prepare_interrupt_echo(struct usb_device *udev,
					    size_t req_len,
					    size_t resp_len,
					    size_t host_in_len)
{
	const struct device *dev = test_uhc_get_dev();
	struct usb_setup_packet setup = {
		.bmRequestType = 0x41,
		.bRequest = LOOPBACK_INTR_REQ_ECHO,
		.wValue = 0,
		.wIndex = sys_cpu_to_le16(LOOPBACK_INTERFACE_NUMBER),
		.wLength = sys_cpu_to_le16(6),
	};
	struct uhc_transfer *xfer;
	int ret;

	xfer = uhc_xfer_alloc_with_buf(dev, 0x00, udev, NULL, NULL, 6);

	zassert_not_null(xfer, "INTR ECHO allocation failed");
	zassert_not_null(xfer->buf, "INTR ECHO buffer missing");

	memcpy(xfer->setup_pkt, &setup, sizeof(setup));

	net_buf_add_le16(xfer->buf, (uint16_t)req_len);
	net_buf_add_le16(xfer->buf, (uint16_t)resp_len);
	net_buf_add_le16(xfer->buf, (uint16_t)host_in_len);

	ret = uhc_ep_enqueue(dev, xfer);
	zassert_equal(ret, 0, "INTR ECHO enqueue failed: %d", ret);

	test_uhc_wait_ep_request();
	zassert_equal(xfer->err, 0, "INTR ECHO failed: %d", xfer->err);

	uhc_xfer_buf_free(dev, xfer->buf);
	uhc_xfer_free(dev, xfer);
}

static void loopback_interrupt_out(struct usb_device *udev,
				   const uint8_t *data,
				   size_t len)
{
	const struct device *dev = test_uhc_get_dev();
	struct uhc_transfer *xfer;
	int ret;

	xfer = uhc_xfer_alloc_with_buf(dev, LOOPBACK_INTR_OUT,
				       udev, NULL, NULL, len);

	zassert_not_null(xfer, "INTR OUT allocation failed");
	zassert_not_null(xfer->buf, "INTR OUT buffer missing");

	net_buf_add_mem(xfer->buf, data, len);

	ret = uhc_ep_enqueue(dev, xfer);
	zassert_equal(ret, 0, "INTR OUT enqueue failed: %d", ret);

	test_uhc_wait_ep_request();
	zassert_equal(xfer->err, 0, "INTR OUT failed: %d", xfer->err);

	uhc_xfer_buf_free(dev, xfer->buf);
	uhc_xfer_free(dev, xfer);
}

static void loopback_interrupt_in(struct usb_device *udev,
				  uint8_t *data,
				  size_t host_in_len,
				  size_t expected_len)
{
	const struct device *dev = test_uhc_get_dev();
	struct uhc_transfer *xfer;
	int ret;

	xfer = uhc_xfer_alloc_with_buf(dev, LOOPBACK_INTR_IN,
				       udev, NULL, NULL, host_in_len);

	zassert_not_null(xfer, "INTR IN allocation failed");
	zassert_not_null(xfer->buf, "INTR IN buffer missing");

	ret = uhc_ep_enqueue(dev, xfer);
	zassert_equal(ret, 0, "INTR IN enqueue failed: %d", ret);

	test_uhc_wait_ep_request();
	zassert_equal(xfer->err, 0, "INTR IN failed: %d", xfer->err);

	zassert_equal(xfer->buf->len, expected_len,
		      "INTR IN length mismatch: got %u, expected %zu",
		      xfer->buf->len, expected_len);

	memcpy(data, xfer->buf->data, expected_len);

	uhc_xfer_buf_free(dev, xfer->buf);
	uhc_xfer_free(dev, xfer);
}

static void loopback_find_interrupt_endpoints(struct usb_device *udev)
{
	struct usb_cfg_descriptor cfg;
	uint8_t raw[TEST_UHC_HUB_CFG_BUF_SIZE];
	uint16_t total;
	bool have_out = false;
	bool have_in = false;

	test_uhc_dev_get_cfg_desc(udev, &cfg, sizeof(cfg));
	total = sys_le16_to_cpu(cfg.wTotalLength);

	zassert_true(total <= sizeof(raw),
		     "Configuration descriptor too large: %u", total);
	zassert_true(total >= sizeof(cfg),
		     "Configuration descriptor too small");

	test_uhc_dev_get_cfg_desc(udev, raw, total);

	for (size_t off = 0; off + 2 <= total;) {
		uint8_t length = raw[off];
		uint8_t type = raw[off + 1];

		zassert_true(length >= 2 && off + length <= total,
			     "Malformed USB descriptor at offset %zu", off);

		if (type == USB_DESC_ENDPOINT &&
		    length >= sizeof(struct usb_ep_descriptor)) {
			struct usb_ep_descriptor desc;

			memcpy(&desc, raw + off, sizeof(desc));

			if (desc.bEndpointAddress == LOOPBACK_INTR_OUT &&
			    (desc.bmAttributes & 0x03) == USB_EP_TYPE_INTERRUPT) {
				intr_out_desc = desc;
				have_out = true;
			} else if (desc.bEndpointAddress == LOOPBACK_INTR_IN &&
				   (desc.bmAttributes & 0x03) == USB_EP_TYPE_INTERRUPT) {
				intr_in_desc = desc;
				have_in = true;
			}
		}

		off += length;
	}

	zassert_true(have_out && have_in,
		     "INTERRUPT endpoint descriptors missing");

	zassert_equal(sys_le16_to_cpu(intr_out_desc.wMaxPacketSize),
		      LOOPBACK_INTR_MPS, "Unexpected INTR OUT MPS");
	zassert_equal(sys_le16_to_cpu(intr_in_desc.wMaxPacketSize),
		      LOOPBACK_INTR_MPS, "Unexpected INTR IN MPS");

	zassert_not_equal(intr_out_desc.bInterval, 0,
			  "INTR OUT interval is zero");
	zassert_not_equal(intr_in_desc.bInterval, 0,
			  "INTR IN interval is zero");

	test_uhc_assign_ep_desc_ptr(udev, LOOPBACK_INTR_OUT, &intr_out_desc);
	test_uhc_assign_ep_desc_ptr(udev, LOOPBACK_INTR_IN, &intr_in_desc);
}

static void loopback_verify_interrupt_with_random_data(struct usb_device *udev,
							size_t req_len,
							size_t resp_len,
							size_t host_in_len)
{
	zassert_true(req_len > 0 && req_len <= LOOPBACK_INTR_MPS);
	zassert_true(resp_len > 0 && resp_len <= req_len);
	zassert_true(resp_len <= host_in_len);
	zassert_true(host_in_len <= LOOPBACK_INTR_MPS);

	memset(test_expected_buf, 0, sizeof(test_expected_buf));
	memset(test_actual_buf, 0, sizeof(test_actual_buf));

	sys_rand_get(test_expected_buf, req_len);

	/* CONTROL: prepare INTERRUPT ECHO */
	loopback_prepare_interrupt_echo(udev, req_len, resp_len, host_in_len);

	/* INTERRUPT OUT: send payload */
	loopback_interrupt_out(udev, test_expected_buf, req_len);

	/* INTERRUPT IN: receive echoed payload */
	loopback_interrupt_in(udev, test_actual_buf, host_in_len, resp_len);

	zassert_mem_equal(test_actual_buf, test_expected_buf,
			  resp_len, "INTR loopback mismatch");

	LOG_INF("INTR loopback: OUT=%zu, IN=%zu, requested=%zu",
		req_len, resp_len, host_in_len);
}

#if (0)
ZTEST(uhc_interrupt_loopback, test_interrupt_loopback_direct)
{
	struct usb_device udev;
	enum usb_device_speed speed;
	struct usb_cfg_descriptor cfg;

	test_uhc_init();

	test_uhc_prepare_addressed_device(&udev, &speed, 1);

	zassert_equal(speed, USB_SPEED_SPEED_FS,
		      "Expected full-speed loopback device");

	udev.speed = speed;
	udev.level = 1;

	zassert_equal(sys_le16_to_cpu(udev.dev_desc.idVendor),
		      LOOPBACK_VID, "Unexpected VID");
	zassert_equal(sys_le16_to_cpu(udev.dev_desc.idProduct),
		      LOOPBACK_PID, "Unexpected PID");

	loopback_find_interrupt_endpoints(&udev);

	test_uhc_dev_get_cfg_desc(&udev, &cfg, sizeof(cfg));
	test_uhc_dev_set_config(&udev, cfg.bConfigurationValue);

	/* Single-packet INTERRUPT ECHO tests */
	loopback_verify_interrupt_with_random_data(&udev, 1, 1, 1);
	loopback_verify_interrupt_with_random_data(&udev, 8, 8, 8);
	loopback_verify_interrupt_with_random_data(&udev, 16, 16, 16);
	loopback_verify_interrupt_with_random_data(&udev, 32, 32, 32);
	loopback_verify_interrupt_with_random_data(&udev, 63, 63, 63);
	loopback_verify_interrupt_with_random_data(&udev, 64, 64, 64);

	test_uhc_device_cleanup();
}
#endif

ZTEST(uhc_interrupt_loopback, test_interrupt_loopback_via_hub)
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
		      LOOPBACK_VID, "Unexpected VID");
	zassert_equal(sys_le16_to_cpu(child_udev.dev_desc.idProduct),
		      LOOPBACK_PID, "Unexpected PID");

	loopback_find_interrupt_endpoints(&child_udev);

	test_uhc_dev_get_cfg_desc(&child_udev, &cfg, sizeof(cfg));
	test_uhc_dev_set_config(&child_udev, cfg.bConfigurationValue);

	/* Single-packet INTERRUPT ECHO tests */
	loopback_verify_interrupt_with_random_data(&child_udev, 1, 1, 1);
	loopback_verify_interrupt_with_random_data(&child_udev, 8, 8, 8);
	loopback_verify_interrupt_with_random_data(&child_udev, 16, 16, 16);
	loopback_verify_interrupt_with_random_data(&child_udev, 32, 32, 32);
	loopback_verify_interrupt_with_random_data(&child_udev, 63, 63, 63);
	loopback_verify_interrupt_with_random_data(&child_udev, 64, 64, 64);

	test_uhc_device_cleanup();
}

ZTEST_SUITE(uhc_interrupt_loopback, NULL, NULL, NULL, NULL, NULL);

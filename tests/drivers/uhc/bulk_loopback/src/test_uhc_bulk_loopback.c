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

LOG_MODULE_REGISTER(uhc_bulk_loopback, LOG_LEVEL_INF);

#define TEST_HUB_ADDR       		1
#define TEST_HUB_IFACE      		0
#define TEST_HUB_PORT       		2
#define TEST_CHILD_ADDR     		2
#define LOOPBACK_VID        		0x2FE3
#define LOOPBACK_PID        		0x0100
#define LOOPBACK_INTERFACE_NUMBER	0
#define LOOPBACK_BULK_REQ_ECHO   	0x03
#define LOOPBACK_BULK_OUT   		0x01
#define LOOPBACK_BULK_IN    		0x81
#define LOOPBACK_MAX_PAYLOAD_LEN  	1024U

static struct usb_ep_descriptor bulk_out_desc;
static struct usb_ep_descriptor bulk_in_desc;
static uint8_t test_expected_buf[LOOPBACK_MAX_PAYLOAD_LEN];
static uint8_t test_actual_buf[LOOPBACK_MAX_PAYLOAD_LEN];

static void loopback_prepare_bulk_echo(struct usb_device *udev, size_t req_len, size_t resp_len, size_t host_in_len)
{
	const struct device *dev = test_uhc_get_dev();
	struct usb_setup_packet setup = {
		.bmRequestType = 0x41,
		.bRequest = LOOPBACK_BULK_REQ_ECHO,
		.wValue = 0,
		.wIndex = sys_cpu_to_le16(LOOPBACK_INTERFACE_NUMBER),
		.wLength = sys_cpu_to_le16(6),
	};

	struct uhc_transfer *xfer;
    	int ret;

	xfer = uhc_xfer_alloc_with_buf(dev, 0x00, udev, NULL, NULL, 6);
	
	zassert_not_null(xfer, "BULK ECHO allocation failed");
	zassert_not_null(xfer->buf, "BULK ECHO buffer missing");

	memcpy(xfer->setup_pkt, &setup, sizeof(setup));

	net_buf_add_le16(xfer->buf, (uint16_t)req_len);
	net_buf_add_le16(xfer->buf, (uint16_t)resp_len);
	net_buf_add_le16(xfer->buf, (uint16_t)host_in_len);

	ret = uhc_ep_enqueue(dev, xfer);
	zassert_equal(ret, 0, "BULK ECHO enqueue failed: %d", ret);

	test_uhc_wait_ep_request();
	zassert_equal(xfer->err, 0, "BULK ECHO failed: %d", xfer->err);

	uhc_xfer_buf_free(dev, xfer->buf);
	uhc_xfer_free(dev, xfer);
}

static void loopback_bulk_out(struct usb_device *udev, const uint8_t *data, size_t len)
{
	const struct device *dev = test_uhc_get_dev();
	struct uhc_transfer *xfer = uhc_xfer_alloc_with_buf(dev, LOOPBACK_BULK_OUT,
							 udev, NULL, NULL, len);
	int ret;

	zassert_not_null(xfer, "BULK OUT allocation failed");
	zassert_not_null(xfer->buf, "BULK OUT buffer missing");
	net_buf_add_mem(xfer->buf, data, len);
	ret = uhc_ep_enqueue(dev, xfer);
	zassert_equal(ret, 0, "BULK OUT enqueue failed: %d", ret);
	test_uhc_wait_ep_request();
	zassert_equal(xfer->err, 0, "BULK OUT failed: %d", xfer->err);
	uhc_xfer_buf_free(dev, xfer->buf);
	uhc_xfer_free(dev, xfer);
}

static void loopback_bulk_in(struct usb_device *udev, uint8_t *data, size_t host_in_len,
                             size_t expected_len)
{
	const struct device *dev = test_uhc_get_dev();
	struct uhc_transfer *xfer = uhc_xfer_alloc_with_buf(dev, LOOPBACK_BULK_IN,
							 udev, NULL, NULL, host_in_len);
	int ret;

	zassert_not_null(xfer, "BULK IN allocation failed");
	zassert_not_null(xfer->buf, "BULK IN buffer missing");

	ret = uhc_ep_enqueue(dev, xfer);
	zassert_equal(ret, 0, "BULK IN enqueue failed: %d", ret);

	test_uhc_wait_ep_request();
	zassert_equal(xfer->err, 0, "BULK IN failed: %d", xfer->err);

	zassert_equal(xfer->buf->len, expected_len,
              "BULK IN length mismatch: got %u, expected %zu",
              xfer->buf->len, expected_len);
	memcpy(data, xfer->buf->data, expected_len);
	uhc_xfer_buf_free(dev, xfer->buf);
	uhc_xfer_free(dev, xfer);
}

static void loopback_find_bulk_endpoints(struct usb_device *udev)
{
	struct usb_cfg_descriptor cfg;
	uint8_t raw[TEST_UHC_HUB_CFG_BUF_SIZE];
	uint16_t total;
	bool have_out = false;
	bool have_in = false;

	test_uhc_dev_get_cfg_desc(udev, &cfg, sizeof(cfg));
	total = sys_le16_to_cpu(cfg.wTotalLength);
	zassert_true(total <= sizeof(raw), "Configuration descriptor too large: %u", total);
	zassert_true(total >= sizeof(cfg), "Configuration descriptor too small");
	test_uhc_dev_get_cfg_desc(udev, raw, total);

	for (size_t off = 0; off + 2 <= total;) {
		uint8_t length = raw[off];
		uint8_t type = raw[off + 1];

		zassert_true(length >= 2 && off + length <= total,
			     "Malformed USB descriptor at offset %zu", off);
		if (type == USB_DESC_ENDPOINT && length >= sizeof(struct usb_ep_descriptor)) {
			struct usb_ep_descriptor desc;

			memcpy(&desc, raw + off, sizeof(desc));
			if (desc.bEndpointAddress == LOOPBACK_BULK_OUT &&
			    (desc.bmAttributes & 0x03) == USB_EP_TYPE_BULK) {
				bulk_out_desc = desc;
				have_out = true;
			} else if (desc.bEndpointAddress == LOOPBACK_BULK_IN &&
				   (desc.bmAttributes & 0x03) == USB_EP_TYPE_BULK) {
				bulk_in_desc = desc;
				have_in = true;
			}
		}
		off += length;
	}
	zassert_true(have_out && have_in, "BULK endpoint descriptors missing");
	test_uhc_assign_ep_desc_ptr(udev, LOOPBACK_BULK_OUT, &bulk_out_desc);
	test_uhc_assign_ep_desc_ptr(udev, LOOPBACK_BULK_IN, &bulk_in_desc);
}

#if (0)
static void loopback_verify_bulk_with_random_data(struct usb_device *udev, size_t req_len, size_t resp_len)
{
	/* Prepare the data buffer */
	memset(test_expected_buf, 0x00, LOOPBACK_MAX_PAYLOAD_LEN);
	memset(test_actual_buf, 0x00, LOOPBACK_MAX_PAYLOAD_LEN);

	/* Prepare buffer */
	sys_rand_get(test_expected_buf, len);

	/* Control */
	loopback_arm_bulk(udev, req_len, resp_len);
	/* Bulk OUT */
	loopback_bulk_out(udev, test_expected_buf, req_len);
	/* Bulk IN */
	loopback_bulk_in(udev, test_actual_buf, resp_len);

	zassert_mem_equal(test_actual_buf, test_expected_buf, len, "BULK loopback mismatch");
	LOG_INF("BULK loopback: %u bytes", len);
}
#endif // 

static void loopback_verify_bulk_with_random_data(struct usb_device *udev,
						size_t req_len,
						size_t resp_len,
						size_t host_in_len)
{
	/* Prepare the data buffers */
	memset(test_expected_buf, 0x00, LOOPBACK_MAX_PAYLOAD_LEN);
	memset(test_actual_buf, 0x00, LOOPBACK_MAX_PAYLOAD_LEN);

	zassert_true(resp_len <= req_len);
	zassert_true(resp_len <= host_in_len);

	/* Generate request payload */
	sys_rand_get(test_expected_buf, req_len);

	/* Control: configure ECHO */
	loopback_prepare_bulk_echo(udev, req_len, resp_len, host_in_len);

	/* BULK OUT: send request payload */
	loopback_bulk_out(udev, test_expected_buf, req_len);

	/* BULK IN: request host_in_len, expect resp_len */
	loopback_bulk_in(udev, test_actual_buf, host_in_len, resp_len);

	/* Compare only the response payload */
	zassert_mem_equal(test_actual_buf, test_expected_buf,
			  resp_len, "BULK loopback mismatch");

	LOG_INF("BULK loopback: OUT=%zu, IN=%zu, requested=%zu",
		req_len, resp_len, host_in_len);
}

ZTEST(uhc_bulk_loopback, test_bulk_loopback_via_hub)
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
	test_uhc_hub_prepare_child(&hub_udev, TEST_HUB_PORT, TEST_CHILD_ADDR,
				   &child_udev);
	zassert_equal(child_udev.speed, USB_SPEED_SPEED_FS, "Expected full-speed child");
	zassert_equal(sys_le16_to_cpu(child_udev.dev_desc.idVendor),
		      LOOPBACK_VID, "Unexpected VID");
	zassert_equal(sys_le16_to_cpu(child_udev.dev_desc.idProduct),
		      LOOPBACK_PID, "Unexpected PID");

	loopback_find_bulk_endpoints(&child_udev);
	test_uhc_dev_get_cfg_desc(&child_udev, &cfg, sizeof(cfg));
	test_uhc_dev_set_config(&child_udev, cfg.bConfigurationValue);

	/* Test Bulk */
	loopback_verify_bulk_with_random_data(&child_udev, 1, 1, 1);
	loopback_verify_bulk_with_random_data(&child_udev, 8, 8, 8);
	loopback_verify_bulk_with_random_data(&child_udev, 64, 64, 64);
	loopback_verify_bulk_with_random_data(&child_udev, 65, 65, 65);
	loopback_verify_bulk_with_random_data(&child_udev, 127, 127, 127);
	loopback_verify_bulk_with_random_data(&child_udev, 128, 128, 128);
	loopback_verify_bulk_with_random_data(&child_udev, 129, 129, 129);
	loopback_verify_bulk_with_random_data(&child_udev, 191, 191, 191);
	loopback_verify_bulk_with_random_data(&child_udev, 192, 192, 192);

	test_uhc_device_cleanup();
}

ZTEST(uhc_bulk_loopback, test_bulk_loopback_via_hub_short_packet_terminaion)
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
	test_uhc_hub_prepare_child(&hub_udev, TEST_HUB_PORT, TEST_CHILD_ADDR,
				   &child_udev);
	zassert_equal(child_udev.speed, USB_SPEED_SPEED_FS, "Expected full-speed child");
	zassert_equal(sys_le16_to_cpu(child_udev.dev_desc.idVendor),
		      LOOPBACK_VID, "Unexpected VID");
	zassert_equal(sys_le16_to_cpu(child_udev.dev_desc.idProduct),
		      LOOPBACK_PID, "Unexpected PID");

	loopback_find_bulk_endpoints(&child_udev);
	test_uhc_dev_get_cfg_desc(&child_udev, &cfg, sizeof(cfg));
	test_uhc_dev_set_config(&child_udev, cfg.bConfigurationValue);

	/* Test Bulk */
	loopback_verify_bulk_with_random_data(&child_udev, 64, 1, 64);
	loopback_verify_bulk_with_random_data(&child_udev, 128, 64, 64);
	loopback_verify_bulk_with_random_data(&child_udev, 128, 63, 128);
	loopback_verify_bulk_with_random_data(&child_udev, 128, 64, 128);
	loopback_verify_bulk_with_random_data(&child_udev, 128, 65, 192);
	loopback_verify_bulk_with_random_data(&child_udev, 192, 127, 192);
	loopback_verify_bulk_with_random_data(&child_udev, 192, 128, 192);
	loopback_verify_bulk_with_random_data(&child_udev, 192, 129, 192);

	test_uhc_device_cleanup();
}

ZTEST_SUITE(uhc_bulk_loopback, NULL, NULL, NULL, NULL, NULL);

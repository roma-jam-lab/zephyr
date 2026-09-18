/*
 * Copyright (c) 2026 Roman Leonov <jam_roma@yahoo.com>
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/ztest.h>
#include <zephyr/drivers/usb/uhc.h>
#include <zephyr/sys/byteorder.h>

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(test_uhc_control, LOG_LEVEL_INF);

#include "test_uhc_common.h"

#define UHC_TEST_ENUM_ADDR		1
#define UHC_TEST_ENUM_CONFIG		1
#define UHC_TEST_ENUM_ATTEMPTS          10

ZTEST(uhc_control, test_data_stage_without_buf_fails)
{
	const struct device *uhc_dev;
	struct usb_device udev;
	struct uhc_transfer *xfer;
	enum usb_device_speed speed;
	struct usb_setup_packet setup = {
		.bmRequestType = USB_REQTYPE_DIR_TO_HOST |
		USB_REQTYPE_TYPE_STANDARD |
		USB_REQTYPE_RECIPIENT_DEVICE,
		.bRequest = USB_SREQ_GET_DESCRIPTOR,
		.wValue = sys_cpu_to_le16((USB_DESC_DEVICE << 8) | 0),
		.wIndex = 0,
		.wLength = sys_cpu_to_le16(8),
	};
	int ret;

	uhc_dev = test_uhc_init();

	test_uhc_prepare_device(&udev, &speed);

	xfer = uhc_xfer_alloc(uhc_dev, 0x80, &udev, NULL, NULL);
	zassert_not_null(xfer, "Failed to allocate transfer");

	memcpy(xfer->setup_pkt, &setup, sizeof(setup));

	ret = uhc_ep_enqueue(uhc_dev, xfer);

	zassert_equal(ret, 0, "Enqueue expected to return 0, got %d", ret);

	test_uhc_wait_ep_request();

	uhc_xfer_free(uhc_dev, xfer);

	test_uhc_device_cleanup();
}

ZTEST(uhc_control, test_in_data_stage_unaligned_tail_fails)
{
	const struct device *uhc_dev;
	enum usb_device_speed speed;
	struct usb_setup_packet setup = {
		.bmRequestType = USB_REQTYPE_DIR_TO_HOST |
				 USB_REQTYPE_TYPE_STANDARD |
				 USB_REQTYPE_RECIPIENT_DEVICE,
		.bRequest = USB_SREQ_GET_DESCRIPTOR,
		.wValue = sys_cpu_to_le16((USB_DESC_DEVICE << 8) | 0),
		.wIndex = 0,
		.wLength = sys_cpu_to_le16(8),
	};
	struct uhc_transfer *xfer;
	struct usb_device udev;
	int ret;

	uhc_dev = test_uhc_init();
	test_uhc_prepare_device(&udev, &speed);

	xfer = uhc_xfer_alloc_with_buf(uhc_dev, 0x80, &udev, NULL, NULL, 8);
	zassert_not_null(xfer, "Failed to allocate transfer");
	zassert_not_null(xfer->buf, "Transfer buffer is NULL");

	memcpy(xfer->setup_pkt, &setup, sizeof(setup));

	net_buf_add_u8(xfer->buf, 0);

	zassert_not_equal((uintptr_t)net_buf_tail(xfer->buf) % 4, 0,
			  "Failed to create unaligned transfer buffer tail");

	ret = uhc_ep_enqueue(uhc_dev, xfer);

	zassert_equal(ret, 0, "Enqueue expected to return 0, got %d", ret);

	test_uhc_wait_ep_request();

	uhc_xfer_buf_free(uhc_dev, xfer->buf);
	uhc_xfer_free(uhc_dev, xfer);

	test_uhc_device_cleanup();
}

ZTEST(uhc_control, test_get_cfg_desc_header_and_full)
{
	struct usb_device udev;
	struct usb_cfg_descriptor cfg_desc;
	uint8_t cfg_buffer[TEST_UHC_MAX_XFER_DATA_BUF_SIZE] = { 0 };
	enum usb_device_speed speed;
	uint32_t total_len;

	test_uhc_init();

	test_uhc_prepare_addressed_device(&udev, &speed, 1);

	memset(&cfg_desc, 0, sizeof(cfg_desc));

	test_uhc_dev_get_cfg_desc(&udev, &cfg_desc, sizeof(cfg_desc));

	total_len = sys_le16_to_cpu(cfg_desc.wTotalLength);

	zassert_equal(cfg_desc.bLength, sizeof(struct usb_cfg_descriptor),
		      "Unexpected config descriptor length: %u",
		      cfg_desc.bLength);

	zassert_equal(cfg_desc.bDescriptorType, USB_DESC_CONFIGURATION,
		      "Unexpected descriptor type: %u",
		      cfg_desc.bDescriptorType);

	printf("cfg_desc.bLength = %u\n", total_len);

	zassert_true(total_len >= sizeof(struct usb_cfg_descriptor),
		     "Invalid config descriptor total length: %u",
		     total_len);

	zassert_true(cfg_desc.bNumInterfaces > 0,
		     "Configuration descriptor reports no interfaces");
		     

	zassert_true(total_len <= sizeof(cfg_buffer),
	     "Config descriptor too large for test buffer: %u",
	     total_len);

	test_uhc_dev_get_cfg_desc(&udev, cfg_buffer, total_len);

	zassert_equal(cfg_buffer[0], sizeof(struct usb_cfg_descriptor),
		      "Unexpected config descriptor bLength: %u",
		      cfg_buffer[0]);

	zassert_equal(cfg_buffer[1], USB_DESC_CONFIGURATION,
		      "Unexpected config descriptor type: %u",
		      cfg_buffer[1]);

	zassert_equal(sys_get_le16(&cfg_buffer[2]), total_len,
		      "Unexpected config descriptor total length");

	test_uhc_device_cleanup();
}

ZTEST(uhc_control, test_get_cfg_desc_buf_size_less_than_wLength_fails)
{
	const struct device *uhc_dev;
	struct usb_device udev;
	struct usb_cfg_descriptor cfg_desc;
	struct uhc_transfer *xfer;
	enum usb_device_speed speed;
	struct usb_setup_packet setup;
	uint16_t total_len;
	size_t small_len;
	int ret;

	uhc_dev = test_uhc_init();

	test_uhc_prepare_addressed_device(&udev, &speed, 1);

	memset(&cfg_desc, 0, sizeof(cfg_desc));

	test_uhc_dev_get_cfg_desc(&udev, &cfg_desc, sizeof(cfg_desc));

	total_len = sys_le16_to_cpu(cfg_desc.wTotalLength);

	zassert_true(total_len > sizeof(struct usb_cfg_descriptor),
		     "Config descriptor is too small for this test: %u",
		     total_len);

	small_len = sizeof(struct usb_cfg_descriptor);

	setup = (struct usb_setup_packet) {
		.bmRequestType = USB_REQTYPE_DIR_TO_HOST << 7,
		.bRequest = USB_SREQ_GET_DESCRIPTOR,
		.wValue = sys_cpu_to_le16((USB_DESC_CONFIGURATION << 8) | 0),
		.wIndex = 0,
		.wLength = sys_cpu_to_le16(total_len),
	};

	xfer = uhc_xfer_alloc_with_buf(uhc_dev, 0x80, &udev, NULL, NULL, small_len);

	zassert_not_null(xfer, "Failed to allocate transfer");
	zassert_not_null(xfer->buf, "Transfer buffer is NULL");

	memcpy(xfer->setup_pkt, &setup, sizeof(setup));

	ret = uhc_ep_enqueue(uhc_dev, xfer);

	zassert_equal(ret, 0, "Enqueue expected to return 0, got %d", ret);

	test_uhc_wait_ep_request();

	uhc_xfer_buf_free(uhc_dev, xfer->buf);
	uhc_xfer_free(uhc_dev, xfer);

	test_uhc_device_cleanup();
}

ZTEST(uhc_control, test_get_string_desc_zero_255)
{
	struct usb_device udev;
	enum usb_device_speed speed;
	uint8_t string_desc[255] = { 0 };
	size_t actual_len;

	test_uhc_init();

	test_uhc_prepare_addressed_device(&udev, &speed, 1);

	actual_len = test_uhc_dev_get_string_desc_short_allowed(&udev,
							       0,
							       0,
							       string_desc,
							       sizeof(string_desc));

	zassert_true(actual_len >= 4,
		     "String descriptor zero too short: %u",
		     actual_len);

	zassert_true(actual_len <= sizeof(string_desc),
		     "String descriptor zero too long: %u",
		     actual_len);

	zassert_equal(string_desc[1], USB_DESC_STRING,
		      "Unexpected descriptor type: %u",
		      string_desc[1]);

	zassert_equal(string_desc[0], actual_len,
		      "Unexpected descriptor bLength: got %u expected %u",
		      string_desc[0], actual_len);

	test_uhc_device_cleanup();
}

ZTEST(uhc_control,  test_stall_then_valid_request)
{
	const struct device *uhc_dev;
	struct usb_device udev;
	struct uhc_transfer *xfer;
	struct usb_cfg_descriptor cfg_desc;
	enum usb_device_speed speed;
	struct usb_setup_packet stall_setup = {
		.bmRequestType = USB_REQTYPE_DIR_TO_HOST << 7,
		.bRequest = USB_SREQ_GET_DESCRIPTOR,
		/*
		 * Descriptor type 0xFF is intentionally unsupported.
		 * A compliant device should STALL this request.
		 */
		.wValue = sys_cpu_to_le16((0xFF << 8) | 0),
		.wIndex = 0,
		.wLength = sys_cpu_to_le16(8),
	};
	int ret;

	uhc_dev = test_uhc_init();

	test_uhc_prepare_addressed_device(&udev, &speed, 1);

	xfer = uhc_xfer_alloc_with_buf(uhc_dev, 0x80, &udev, NULL, NULL, 8);
	zassert_not_null(xfer, "Failed to allocate UHC transfer");
	zassert_not_null(xfer->buf, "Transfer buffer is NULL");

	memcpy(xfer->setup_pkt, &stall_setup, sizeof(stall_setup));

	ret = uhc_ep_enqueue(uhc_dev, xfer);
	zassert_equal(ret, 0, "uhc_ep_enqueue failed: %d", ret);

	test_uhc_wait_ep_request();

	zassert_equal(xfer->err, -EPIPE,
		      "Expected STALL/-EPIPE, got %d",
		      xfer->err);

	uhc_xfer_buf_free(uhc_dev, xfer->buf);
	uhc_xfer_free(uhc_dev, xfer);

	memset(&cfg_desc, 0, sizeof(cfg_desc));

	/*
	 * A new SETUP packet on EP0 should recover from the previous STALL.
	 * Verify that the control pipe still works by issuing a valid request.
	 */
	test_uhc_dev_get_cfg_desc(&udev, &cfg_desc, sizeof(cfg_desc));

	zassert_equal(cfg_desc.bLength, sizeof(struct usb_cfg_descriptor),
		      "Unexpected config descriptor length: %u",
		      cfg_desc.bLength);

	zassert_equal(cfg_desc.bDescriptorType, USB_DESC_CONFIGURATION,
		      "Unexpected descriptor type: %u",
		      cfg_desc.bDescriptorType);

	zassert_true(sys_le16_to_cpu(cfg_desc.wTotalLength) >=
		     sizeof(struct usb_cfg_descriptor),
		     "Invalid config descriptor total length: %u",
		     sys_le16_to_cpu(cfg_desc.wTotalLength));

	test_uhc_device_cleanup();
}

#if (0) 
ZTEST(uhc_control, test_disable_during_pending_request)
{
	const struct device *uhc_dev;
	struct usb_device udev;
	struct uhc_transfer *xfer;
	enum usb_device_speed speed;
	struct usb_setup_packet setup = {
		.bmRequestType = USB_REQTYPE_DIR_TO_HOST << 7,
		.bRequest = USB_SREQ_GET_DESCRIPTOR,
		.wValue = sys_cpu_to_le16((USB_DESC_STRING << 8) | 0),
		.wIndex = 0,
		.wLength = sys_cpu_to_le16(255),
	};
	int ret;

	uhc_dev = test_uhc_init();

	test_uhc_prepare_addressed_device(&udev, &speed, 1);

	xfer = uhc_xfer_alloc_with_buf(uhc_dev, 0x80, &udev, NULL, NULL, 255);
	zassert_not_null(xfer, "Failed to allocate UHC transfer");
	zassert_not_null(xfer->buf, "Transfer buffer is NULL");

	memcpy(xfer->setup_pkt, &setup, sizeof(setup));

	ret = uhc_ep_enqueue(uhc_dev, xfer);
	zassert_equal(ret, 0, "uhc_ep_enqueue failed: %d", ret);

	ret = uhc_disable(uhc_dev);
	zassert_equal(ret, 0, "uhc_disable failed: %d", ret);

	test_uhc_wait_ep_request();

	zassert_equal(xfer->err, -ESHUTDOWN,
		      "Expected -ESHUTDOWN after disable, got %d",
		      xfer->err);

	test_uhc_wait_removed();

	uhc_xfer_buf_free(uhc_dev, xfer->buf);
	uhc_xfer_free(uhc_dev, xfer);
}
#endif // 

#define UHC_TEST_UVC_VS_IFACE		1
#define UHC_TEST_UVC_PROBE_LEN		26

ZTEST(uhc_control, test_data_out_26b_stalled)
{
	struct usb_device udev;
	enum usb_device_speed speed;
	uint8_t probe[UHC_TEST_UVC_PROBE_LEN];

	test_uhc_init();

	test_uhc_prepare_addressed_device(&udev, &speed, 1);

	memset(probe, 0, sizeof(probe));

	test_uhc_dev_uvc_get_probe_cur(&udev,
					  UHC_TEST_UVC_VS_IFACE,
					  probe,
					  sizeof(probe));

	test_uhc_dev_uvc_set_probe_cur(&udev,
					  UHC_TEST_UVC_VS_IFACE,
					  probe,
					  sizeof(probe));

	test_uhc_device_cleanup();
}

ZTEST(uhc_control, test_fast_enumeration)
{
	enum usb_device_speed speed = USB_SPEED_UNKNOWN;
	struct usb_device udev;
	uint8_t ep0_mps;

	test_uhc_print_current_stack_usage();

	test_uhc_init();

	for (int attempt = 0; attempt < UHC_TEST_ENUM_ATTEMPTS; attempt++) {

		test_uhc_enable();

		test_uhc_wait_connection(&speed);

		test_uhc_bus_reset();

		test_uhc_dev_init(&udev);

		test_uhc_dev_get_short_dev_desc(&udev, &ep0_mps);

		test_uhc_dev_get_full_dev_desc(&udev, ep0_mps);

		test_uhc_dev_set_address(&udev, UHC_TEST_ENUM_ADDR);

		test_uhc_dev_set_config(&udev, UHC_TEST_ENUM_CONFIG);

		test_uhc_disable_wait_removed();
	}

	test_uhc_shutdown();

	test_uhc_print_current_stack_usage();
}

#if (0)
ZTEST(uhc_control, test_suspend_wakeup)
{
	enum usb_device_speed speed = USB_SPEED_UNKNOWN;
	struct usb_device udev;
	uint8_t ep0_mps;

	test_uhc_print_current_stack_usage();

	test_uhc_init();

	test_uhc_enable();

	test_uhc_wait_connection(&speed);

	test_uhc_bus_reset();

	test_uhc_dev_init(&udev);

	test_uhc_dev_get_short_dev_desc(&udev, &ep0_mps);

	test_uhc_dev_get_full_dev_desc(&udev, ep0_mps);

	test_uhc_dev_set_address(&udev, UHC_TEST_ENUM_ADDR);

	test_uhc_dev_set_config(&udev, UHC_TEST_ENUM_CONFIG);

	test_uhc_bus_suspend();

	/* Keep suspended for 1 sec */
	k_msleep(1000);

	test_uhc_bus_resume();

	test_uhc_dev_get_full_dev_desc(&udev, ep0_mps);

	test_uhc_disable_wait_removed();

	test_uhc_shutdown();

	test_uhc_print_current_stack_usage();
}
#endif //


ZTEST_SUITE(uhc_control, NULL, NULL, NULL, NULL, NULL);

/*
 * Copyright (c) 2026 Roman Leonov <jam_roma@yahoo.com>
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <string.h>
#include <zephyr/kernel.h>
#include <zephyr/ztest.h>
#include <zephyr/device.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/drivers/usb/uhc.h>

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(uhc_driver_test, LOG_LEVEL_INF);

#define UHC_NODE DT_NODELABEL(zephyr_uhc0)

#define UHC_TEST_ADDR		1
#define UHC_TEST_CONFIG		1
#define UHC_TEST_EP0_INIT_MPS	8
#define UHC_TEST_SHORT_REQ_LEN	8
#define UHC_TEST_EVENT_TIMEOUT	K_SECONDS(5)

K_MSGQ_DEFINE(uhc_test_msgq, sizeof(struct uhc_event), 16, sizeof(uint32_t));

static const struct device *const uhc_dev = DEVICE_DT_GET(UHC_NODE);

static void test_uhc_print_current_stack_usage(void)
{
	size_t unused;
	int ret;

	ret = k_thread_stack_space_get(k_current_get(), &unused);
	zassert_equal(ret, 0, "k_thread_stack_space_get failed: %d", ret);

	printk("Current thread unused stack: %zu bytes\n", unused);
}

static int test_uhc_event_cb(const struct device *dev,
			     const struct uhc_event *const event)
{
	struct uhc_event copy = *event;

	ARG_UNUSED(dev);

	return k_msgq_put(&uhc_test_msgq, &copy, K_NO_WAIT);
}

static void test_uhc_wait_event(enum uhc_event_type type, k_timeout_t timeout)
{
	struct uhc_event event;
	int ret;

	ret = k_msgq_get(&uhc_test_msgq, &event, timeout);
	zassert_not_equal(ret, -ENOMSG, "Timeout waiting for UHC event");
	zassert_equal(ret, 0, "Unable to get message from queue: %d", ret);
	zassert_equal(event.type, type, "Unexpected event: %d. Expected: %d", event.type, type);
}

static void test_uhc_wait_connection_event(enum usb_device_speed *dev_speed)
{
	struct uhc_event event;
	int ret;

	ret = k_msgq_get(&uhc_test_msgq, &event, UHC_TEST_EVENT_TIMEOUT);
	zassert_not_equal(ret, -ENOMSG, "Timeout waiting for UHC event");
	zassert_equal(ret, 0, "Unable to get message from queue: %d", ret);

	switch (event.type) {
	case UHC_EVT_DEV_CONNECTED_HS:
		*dev_speed = USB_SPEED_SPEED_HS;
		break;
	case UHC_EVT_DEV_CONNECTED_FS:
		*dev_speed = USB_SPEED_SPEED_FS;
		break;
	case UHC_EVT_DEV_CONNECTED_LS:
		*dev_speed = USB_SPEED_SPEED_LS;
		break;
	default:
		zassert_true(false, "Unexpected connection event: %d.", event.type);
		break;
	}
}

static void test_uhc_control_request_data(struct usb_device *udev,
					  const struct usb_setup_packet *setup,
					  void *data,
					  size_t data_len)
{
	struct uhc_transfer *xfer;
	int ret;

	zassert_not_null(udev, "udev is NULL");
	zassert_not_null(setup, "setup is NULL");
	zassert_not_null(data, "data is NULL");
	zassert_true(data_len > 0, "data_len is zero");
	zassert_false(usb_reqtype_is_to_device(setup),
		      "Expected device-to-host control request");

	xfer = uhc_xfer_alloc_with_buf(uhc_dev, 0x80, udev, NULL, NULL, data_len);
	zassert_not_null(xfer, "Failed to allocate UHC transfer");
	zassert_not_null(xfer->buf, "Transfer buffer is NULL");

	memcpy(xfer->setup_pkt, setup, sizeof(*setup));

	ret = uhc_ep_enqueue(uhc_dev, xfer);
	zassert_equal(ret, 0, "uhc_ep_enqueue failed: %d", ret);

	test_uhc_wait_event(UHC_EVT_EP_REQUEST, UHC_TEST_EVENT_TIMEOUT);

	zassert_equal(xfer->err, 0, "Control transfer failed: %d", xfer->err);
	zassert_equal(xfer->buf->len, data_len,
		      "Unexpected IN data length: got %u expected %u",
		      xfer->buf->len, data_len);

	memcpy(data, xfer->buf->data, data_len);

	uhc_xfer_buf_free(uhc_dev, xfer->buf);
	uhc_xfer_free(uhc_dev, xfer);
}

static void test_uhc_control_request(struct usb_device *udev,
				     const struct usb_setup_packet *setup)
{
	struct uhc_transfer *xfer;
	uint8_t ep;
	int ret;

	zassert_not_null(udev, "udev is NULL");
	zassert_not_null(setup, "setup is NULL");
	zassert_equal(sys_le16_to_cpu(setup->wLength), 0,
		      "Expected zero-length control request");
	zassert_true(usb_reqtype_is_to_device(setup),
		     "Expected host-to-device control request");

	ep = usb_reqtype_is_to_device(setup) ? 0x00 : 0x80;

	xfer = uhc_xfer_alloc(uhc_dev, ep, udev, NULL, NULL);
	zassert_not_null(xfer, "Failed to allocate UHC transfer");

	memcpy(xfer->setup_pkt, setup, sizeof(*setup));

	ret = uhc_ep_enqueue(uhc_dev, xfer);
	zassert_equal(ret, 0, "uhc_ep_enqueue failed: %d", ret);

	test_uhc_wait_event(UHC_EVT_EP_REQUEST, UHC_TEST_EVENT_TIMEOUT);

	zassert_equal(xfer->err, 0, "Control request failed: %d", xfer->err);

	uhc_xfer_free(uhc_dev, xfer);
}

static void test_uhc_get_dev_desc(struct usb_device *udev, void *buf, size_t len)
{
	struct usb_setup_packet setup = {
		.bmRequestType = (USB_REQTYPE_DIR_TO_HOST << 7),
		.bRequest = USB_SREQ_GET_DESCRIPTOR,
		.wValue = sys_cpu_to_le16((USB_DESC_DEVICE << 8) | 0),
		.wIndex = 0,
		.wLength = sys_cpu_to_le16(len),
	};

	test_uhc_control_request_data(udev, &setup, buf, len);
}

static void test_uhc_set_address(struct usb_device *udev, uint8_t addr)
{
	struct usb_setup_packet setup = {
		.bmRequestType = USB_REQTYPE_DIR_TO_DEVICE |
				 USB_REQTYPE_TYPE_STANDARD |
				 USB_REQTYPE_RECIPIENT_DEVICE,
		.bRequest = USB_SREQ_SET_ADDRESS,
		.wValue = sys_cpu_to_le16(addr),
		.wIndex = 0,
		.wLength = 0,
	};

	test_uhc_control_request(udev, &setup);
}

static void test_uhc_set_config(struct usb_device *udev, uint8_t cfg)
{
	struct usb_setup_packet setup = {
		.bmRequestType = USB_REQTYPE_DIR_TO_DEVICE |
				 USB_REQTYPE_TYPE_STANDARD |
				 USB_REQTYPE_RECIPIENT_DEVICE,
		.bRequest = USB_SREQ_SET_CONFIGURATION,
		.wValue = sys_cpu_to_le16(cfg),
		.wIndex = 0,
		.wLength = 0,
	};

	test_uhc_control_request(udev, &setup);
}

ZTEST(uhc_driver_test, test_uhc_fast_enumeration)
{
	struct usb_device udev;
	enum usb_device_speed speed = USB_SPEED_UNKNOWN;
	uint8_t ep0_mps;
	int ret;

	test_uhc_print_current_stack_usage();

	/* Flush udev local object */
	memset(&udev, 0, sizeof(struct usb_device));
	k_mutex_init(&udev.mutex);

	zassert_true(device_is_ready(uhc_dev), "UHC is not ready");

	/* Init UHC */
	ret = uhc_init(uhc_dev, test_uhc_event_cb, NULL);
	zassert_true(ret == 0, "uhc_init failed: %d", ret);

	ret = uhc_enable(uhc_dev);
	zassert_true(ret == 0, "uhc_enable failed: %d", ret);

	zassert_true(uhc_is_initialized(uhc_dev), "UHC not initialized");
	zassert_true(uhc_is_enabled(uhc_dev), "UHC not enabled");

	/* Wait for connection event from the driver */
	test_uhc_wait_connection_event(&speed);

	/* First reset */
	ret = uhc_bus_reset(uhc_dev);
	zassert_equal(ret, 0, "uhc_bus_reset failed: %d", ret);

	test_uhc_wait_event(UHC_EVT_RESETED, UHC_TEST_EVENT_TIMEOUT);

	/* Get short Device Descriptor */
	memset(&udev.dev_desc, 0, sizeof(struct usb_device_descriptor));
	/* Limit the mps to 8 */
	udev.dev_desc.bMaxPacketSize0 = UHC_TEST_EP0_INIT_MPS;
	test_uhc_get_dev_desc(&udev, &udev.dev_desc, UHC_TEST_SHORT_REQ_LEN);

	zassert_equal(udev.dev_desc.bDescriptorType, USB_DESC_DEVICE,
		      "Unexpected descriptor type: %u", udev.dev_desc.bDescriptorType);

	ep0_mps = udev.dev_desc.bMaxPacketSize0;
	zassert_true(ep0_mps == 8 || ep0_mps == 16 ||
		     ep0_mps == 32 || ep0_mps == 64,
		     "Invalid EP0 MPS: %u", ep0_mps);

	/*
	 * Second reset.
	 * To support legacy USB devices that might be confused by the second
	 * request for the Device Descriptor the bus reset might be done between.
	 */

	/* Get Full Device Descriptor */
	memset(&udev.dev_desc, 0, sizeof(struct usb_device_descriptor));
	/* Restore ep0 mps */
	udev.dev_desc.bMaxPacketSize0 = ep0_mps;
	test_uhc_get_dev_desc(&udev, &udev.dev_desc, sizeof(struct usb_device_descriptor));

	zassert_equal(udev.dev_desc.bDescriptorType, USB_DESC_DEVICE,
		"Unexpected descriptor type: %u", udev.dev_desc.bDescriptorType);

	/* Set address */
	test_uhc_set_address(&udev, UHC_TEST_ADDR);

	udev.addr = UHC_TEST_ADDR;

	/* Set configuration */
	test_uhc_set_config(&udev, UHC_TEST_CONFIG);

	/* Disable and get device disconnection event. */
	ret = uhc_disable(uhc_dev);
	zassert_true(ret == 0, "uhc_disable failed: %d", ret);

	test_uhc_wait_event(UHC_EVT_DEV_REMOVED, UHC_TEST_EVENT_TIMEOUT);

	/* TODO: Shutdown */
	LOG_WRN("Shutdown not implemented yet");

	test_uhc_print_current_stack_usage();
}

ZTEST_SUITE(uhc_driver_test, NULL, NULL, NULL, NULL, NULL);

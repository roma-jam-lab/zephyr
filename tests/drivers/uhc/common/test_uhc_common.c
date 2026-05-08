/*
 * Copyright (c) 2026 Roman Leonov <jam_roma@yahoo.com>
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <string.h>
#include <zephyr/kernel.h>
#include <zephyr/ztest.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/drivers/usb/uhc.h>

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(test_uhc_common, LOG_LEVEL_INF);

#include "test_uhc_common.h"

#define UHC_NODE DT_NODELABEL(zephyr_uhc0)

#define UHC_TEST_EP0_INIT_MPS	8
#define UHC_TEST_SHORT_REQ_LEN	8
#define UHC_TEST_EVENT_TIMEOUT	K_SECONDS(5)

K_MSGQ_DEFINE(uhc_test_msgq, sizeof(struct uhc_event), 16, sizeof(uint32_t));

static const struct device *const uhc_dev = DEVICE_DT_GET(UHC_NODE);

static int test_uhc_event_cb(const struct device *dev, const struct uhc_event *const event)
{
	struct uhc_event copy = *event;

	ARG_UNUSED(dev);

	return k_msgq_put(&uhc_test_msgq, &copy, K_NO_WAIT);
}

const struct device *test_uhc_init(void)
{
	int ret;

	zassert_true(device_is_ready(uhc_dev), "UHC is not ready");
	zassert_false(uhc_is_initialized(uhc_dev), "UHC already initialized");

	ret = uhc_init(uhc_dev, test_uhc_event_cb, NULL);
	zassert_true(ret == 0, "uhc_init failed: %d", ret);
	zassert_true(uhc_is_initialized(uhc_dev), "UHC not initialized");

	return uhc_dev;
}

const struct device *test_uhc_get_dev(void)
{
	zassert_false(uhc_dev == NULL, "UHC not initialized");
	zassert_true(uhc_is_initialized(uhc_dev), "UHC not initialized");

	return uhc_dev;
}


void test_uhc_enable(void)
{
	int ret;

	zassert_true(device_is_ready(uhc_dev), "UHC is not ready");

	ret = uhc_enable(uhc_dev);
	zassert_true(ret == 0, "uhc_enable failed: %d", ret);

	zassert_true(uhc_is_enabled(uhc_dev), "UHC not enabled");
}

void test_uhc_print_current_stack_usage(void)
{
	size_t unused;
	int ret;

	ret = k_thread_stack_space_get(k_current_get(), &unused);
	zassert_equal(ret, 0, "k_thread_stack_space_get failed: %d", ret);

	printk("Current thread unused stack: %zu bytes\n", unused);
}

void test_uhc_dev_init(struct usb_device *const udev)
{
	zassert_not_null(udev, "udev is NULL");
	memset(udev, 0, sizeof(struct usb_device));
	k_mutex_init(&udev->mutex);
}

static void test_uhc_wait_event(enum uhc_event_type type)
{
	struct uhc_event event;
	int ret;

	ret = k_msgq_get(&uhc_test_msgq, &event, UHC_TEST_EVENT_TIMEOUT);
	zassert_not_equal(ret, -ENOMSG, "Timeout waiting for UHC event");
	zassert_equal(ret, 0, "Unable to get message from queue: %d", ret);
	zassert_equal(event.type, type, "Unexpected event: %d. Expected: %d", event.type, type);
}

void test_uhc_wait_connection(enum usb_device_speed *dev_speed)
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

void test_uhc_wait_ep_request(void)
{
	test_uhc_wait_event(UHC_EVT_EP_REQUEST);
}

void test_uhc_wait_removed(void) 
{
	test_uhc_wait_event(UHC_EVT_DEV_REMOVED);
}

void test_uhc_bus_reset(void) 
{
	int ret;

	ret = uhc_bus_reset(uhc_dev);
	zassert_equal(ret, 0, "uhc_bus_reset failed: %d", ret);

	test_uhc_wait_event(UHC_EVT_RESETED);
}

void test_uhc_bus_suspend(void)
{
	int ret;

	ret = uhc_bus_suspend(uhc_dev);
	zassert_equal(ret, 0, "uhc_bus_suspend failed: %d", ret);
}

void test_uhc_bus_resume(void)
{
	int ret;

	ret = uhc_bus_resume(uhc_dev);
	zassert_equal(ret, 0, "uhc_bus_resume failed: %d", ret);
}

void test_uhc_disable(void)
{
	int ret;
	
	ret = uhc_disable(uhc_dev);
	zassert_equal(ret, 0, "uhc_disable failed: %d", ret);
}

void test_uhc_disable_wait_removed(void)
{
	int ret;
	
	ret = uhc_disable(uhc_dev);
	zassert_equal(ret, 0, "uhc_disable failed: %d", ret);

	test_uhc_wait_event(UHC_EVT_DEV_REMOVED);
}

void test_uhc_shutdown(void)
{
	int ret;

	ret = uhc_shutdown(uhc_dev);
	zassert_equal(ret, 0, "uhc_shutdown failed: %d", ret);
}

void test_uhc_prepare_device(struct usb_device *udev,
				    enum usb_device_speed *speed)
{
	zassert_not_null(udev, "udev is NULL");
	zassert_not_null(speed, "speed is NULL");

	*speed = USB_SPEED_UNKNOWN;

	test_uhc_enable();
	test_uhc_wait_connection(speed);
	test_uhc_bus_reset();
	test_uhc_dev_init(udev);
	udev->dev_desc.bMaxPacketSize0 = 8;
}

void test_uhc_prepare_addressed_device(struct usb_device *udev,
				    	      enum usb_device_speed *speed, 
					      uint8_t dev_addr)
{
	uint8_t ep0_mps;

	zassert_not_null(udev, "udev is NULL");
	zassert_not_null(speed, "speed is NULL");

	*speed = USB_SPEED_UNKNOWN;

	test_uhc_enable();
	test_uhc_wait_connection(speed);
	test_uhc_bus_reset();

	test_uhc_dev_init(udev);

	test_uhc_dev_get_short_dev_desc(udev, &ep0_mps);
	test_uhc_dev_get_full_dev_desc(udev, ep0_mps);
	test_uhc_dev_set_address(udev, dev_addr);
}

void test_uhc_device_cleanup(void)
{
	test_uhc_disable_wait_removed();
	test_uhc_shutdown();
}

void test_uhc_control_request_in_data(struct usb_device *udev,
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

	test_uhc_wait_event(UHC_EVT_EP_REQUEST);

	zassert_equal(xfer->err, 0, "Control transfer failed: %d", xfer->err);
	zassert_equal(xfer->buf->len, data_len,
		      "Unexpected IN data length: got %u expected %u",
		      xfer->buf->len, data_len);

	memcpy(data, xfer->buf->data, data_len);

	uhc_xfer_buf_free(uhc_dev, xfer->buf);
	uhc_xfer_free(uhc_dev, xfer);
}

size_t test_uhc_control_request_in_data_short_allowed(struct usb_device *udev,
							     const struct usb_setup_packet *setup,
							     void *data,
							     size_t data_len)
{
	struct uhc_transfer *xfer;
	size_t actual_len;
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

	test_uhc_wait_event(UHC_EVT_EP_REQUEST);

	zassert_equal(xfer->err, 0, "Control transfer failed: %d", xfer->err);

	zassert_true(xfer->buf->len <= data_len,
		     "Received more data than requested: got %u expected max %u",
		     xfer->buf->len, data_len);

	actual_len = xfer->buf->len;

	memcpy(data, xfer->buf->data, actual_len);

	uhc_xfer_buf_free(uhc_dev, xfer->buf);
	uhc_xfer_free(uhc_dev, xfer);

	return actual_len;
}

static void test_uhc_control_request_out_data(struct usb_device *udev,
					      const struct usb_setup_packet *setup,
					      const void *data,
					      size_t data_len)
{
	struct uhc_transfer *xfer;
	int ret;

	zassert_not_null(udev, "udev is NULL");
	zassert_not_null(setup, "setup is NULL");
	zassert_not_null(data, "data is NULL");
	zassert_true(data_len > 0, "data_len is zero");
	zassert_true(usb_reqtype_is_to_device(setup),
		     "Expected host-to-device control request");
	zassert_equal(sys_le16_to_cpu(setup->wLength), data_len,
		      "wLength/data_len mismatch");

	xfer = uhc_xfer_alloc_with_buf(uhc_dev, 0x00, udev, NULL, NULL, data_len);

	zassert_not_null(xfer, "Failed to allocate UHC transfer");
	zassert_not_null(xfer->buf, "Transfer buffer is NULL");

	memcpy(xfer->setup_pkt, setup, sizeof(*setup));

	net_buf_add_mem(xfer->buf, data, data_len);

	ret = uhc_ep_enqueue(uhc_dev, xfer);
	zassert_equal(ret, 0, "uhc_ep_enqueue failed: %d", ret);

	test_uhc_wait_event(UHC_EVT_EP_REQUEST);

	/* Expect STALL as normal behavior */
	zassert_true(xfer->err == 0 || xfer->err == -EPIPE,
	     "Unexpected Control OUT transfer error: %d", xfer->err);

	if (xfer->err == -EPIPE) {
		printk("Request STALLed\n");
	}

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

	test_uhc_wait_event(UHC_EVT_EP_REQUEST);

	zassert_equal(xfer->err, 0, "Control request failed: %d", xfer->err);

	uhc_xfer_free(uhc_dev, xfer);
}

void test_uhc_dev_get_dev_desc(struct usb_device *udev, void *buf, size_t len)
{
	struct usb_setup_packet setup = {
		.bmRequestType = (USB_REQTYPE_DIR_TO_HOST << 7),
		.bRequest = USB_SREQ_GET_DESCRIPTOR,
		.wValue = sys_cpu_to_le16((USB_DESC_DEVICE << 8) | 0),
		.wIndex = 0,
		.wLength = sys_cpu_to_le16(len),
	};

	test_uhc_control_request_in_data(udev, &setup, buf, len);
}

void test_uhc_dev_get_cfg_desc(struct usb_device *udev, void *buf, size_t len)
{
	struct usb_setup_packet setup = {
		.bmRequestType = (USB_REQTYPE_DIR_TO_HOST << 7),
		.bRequest = USB_SREQ_GET_DESCRIPTOR,
		.wValue = sys_cpu_to_le16((USB_DESC_CONFIGURATION << 8) | 0),
		.wIndex = 0,
		.wLength = sys_cpu_to_le16(len),
	};

	test_uhc_control_request_in_data(udev, &setup, buf, len);
}

void test_uhc_dev_get_string_desc(struct usb_device *udev,
				  uint8_t index,
				  uint16_t lang_id,
				  void *buf,
				  size_t len)
{
	struct usb_setup_packet setup = {
		.bmRequestType = (USB_REQTYPE_DIR_TO_HOST << 7),
		.bRequest = USB_SREQ_GET_DESCRIPTOR,
		.wValue = sys_cpu_to_le16((USB_DESC_STRING << 8) | index),
		.wIndex = sys_cpu_to_le16(lang_id),
		.wLength = sys_cpu_to_le16(len),
	};

	test_uhc_control_request_in_data(udev, &setup, buf, len);
}

size_t test_uhc_dev_get_string_desc_short_allowed(struct usb_device *udev,
						  uint8_t index,
						  uint16_t lang_id,
						  void *buf,
						  size_t len)
{
	struct usb_setup_packet setup = {
		.bmRequestType = USB_REQTYPE_DIR_TO_HOST << 7,
		.bRequest = USB_SREQ_GET_DESCRIPTOR,
		.wValue = sys_cpu_to_le16((USB_DESC_STRING << 8) | index),
		.wIndex = sys_cpu_to_le16(lang_id),
		.wLength = sys_cpu_to_le16(len),
	};

	return test_uhc_control_request_in_data_short_allowed(udev, &setup, buf, len);
}

#define UVC_SET_CUR			0x01
#define UVC_GET_CUR			0x81
#define UVC_VS_PROBE_CONTROL		0x01

void test_uhc_dev_uvc_get_probe_cur(struct usb_device *udev,
					      uint8_t iface,
					      void *buf,
					      size_t len)
{
	struct usb_setup_packet setup = {
		.bmRequestType = USB_REQTYPE_DIR_TO_DEVICE << 7 |
				 USB_REQTYPE_TYPE_CLASS << 5 |
				 USB_REQTYPE_RECIPIENT_INTERFACE,
		.bRequest = UVC_GET_CUR,
		.wValue = sys_cpu_to_le16(UVC_VS_PROBE_CONTROL << 8),
		.wIndex = sys_cpu_to_le16(iface),
		.wLength = sys_cpu_to_le16(len),
	};

	test_uhc_control_request_out_data(udev, &setup, buf, len);
}

void test_uhc_dev_uvc_set_probe_cur(struct usb_device *udev,
					      uint8_t iface,
					      void *buf,
					      size_t len)
{
	struct usb_setup_packet setup = {
		.bmRequestType = USB_REQTYPE_DIR_TO_DEVICE << 7  |
				 USB_REQTYPE_TYPE_CLASS |
				 USB_REQTYPE_RECIPIENT_INTERFACE,
		.bRequest = UVC_SET_CUR,
		.wValue = sys_cpu_to_le16(UVC_VS_PROBE_CONTROL << 8),
		.wIndex = sys_cpu_to_le16(iface),
		.wLength = sys_cpu_to_le16(len),
	};

	test_uhc_control_request_out_data(udev, &setup, buf, len);
}

void test_uhc_dev_set_address(struct usb_device *udev, uint8_t addr)
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

	udev->addr = addr;
}

void test_uhc_dev_set_config(struct usb_device *udev, uint8_t cfg)
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

void test_uhc_dev_get_short_dev_desc(struct usb_device *udev, uint8_t *ep0_mps)
{
	uint8_t mps;

	zassert_not_null(udev, "udev is NULL");
	zassert_not_null(ep0_mps, "ep0_mps is NULL");

	udev->dev_desc.bMaxPacketSize0 = UHC_TEST_EP0_INIT_MPS;

	test_uhc_dev_get_dev_desc(udev, &udev->dev_desc, UHC_TEST_SHORT_REQ_LEN);

	zassert_equal(udev->dev_desc.bDescriptorType, USB_DESC_DEVICE,
		      "Unexpected descriptor type: %u", udev->dev_desc.bDescriptorType);

	mps = udev->dev_desc.bMaxPacketSize0;
	zassert_true(mps == 8 || mps == 16 || mps == 32 || mps == 64,
		     "Invalid EP0 MPS: %u", mps);

	*ep0_mps = mps;
}

void test_uhc_dev_get_full_dev_desc(struct usb_device *udev, uint8_t ep0_mps)
{
	zassert_not_null(udev, "udev is NULL");
	zassert_not_equal(udev->dev_desc.bMaxPacketSize0, 0, "Unknown EP0 MPS");

	udev->dev_desc.bDescriptorType = 0;
	udev->dev_desc.bMaxPacketSize0 = ep0_mps;

	test_uhc_dev_get_dev_desc(udev, &udev->dev_desc, sizeof(struct usb_device_descriptor));

	zassert_equal(udev->dev_desc.bDescriptorType, USB_DESC_DEVICE,
		"Unexpected descriptor type: %u", udev->dev_desc.bDescriptorType);
}

void test_uhc_assign_ep_desc_ptr(struct usb_device *const udev,
			       const uint8_t ep, void *const ptr)
{
	uint8_t idx = USB_EP_GET_IDX(ep) & 0xF;

	if (USB_EP_DIR_IS_IN(ep)) {
		udev->ep_in[idx].desc = ptr;
	} else {
		udev->ep_out[idx].desc = ptr;
	}
}
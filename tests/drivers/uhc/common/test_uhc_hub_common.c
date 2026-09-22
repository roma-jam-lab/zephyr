/*
 * Copyright (c) 2026 Roman Leonov <jam_roma@yahoo.com>
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/ztest.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/drivers/usb/uhc.h>
#include <zephyr/usb/usb_ch9.h>
#include <zephyr/usb/class/usb_hub.h>

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(test_uhc_hub_common, LOG_LEVEL_INF);

#include "test_uhc_common.h"
#include "test_uhc_hub_common.h"

static void test_uhc_hub_control_in(struct usb_device *udev,
				    const struct usb_setup_packet *setup,
				    void *data,
				    size_t len)
{
	const struct device *uhc_dev = test_uhc_get_dev();
	struct uhc_transfer *xfer;
	int ret;

	zassert_not_null(udev, "udev is NULL");
	zassert_not_null(setup, "setup is NULL");
	zassert_not_null(data, "data is NULL");
	zassert_not_equal(len, 0, "Control IN length is zero");

	xfer = uhc_xfer_alloc_with_buf(uhc_dev,
				       USB_EP_DIR_IN,
				       udev,
				       NULL,
				       NULL,
				       len);
	zassert_not_null(xfer, "Failed to allocate HUB control transfer");
	zassert_not_null(xfer->buf, "Failed to allocate HUB control buffer");

	memcpy(xfer->setup_pkt, setup, sizeof(*setup));

	ret = uhc_ep_enqueue(uhc_dev, xfer);
	zassert_equal(ret, 0, "HUB control IN enqueue failed: %d", ret);

	test_uhc_wait_ep_request();

	zassert_equal(xfer->err, 0,
		      "HUB control IN transfer failed: %d",
		      xfer->err);

	zassert_true(xfer->buf->len <= len,
		     "HUB control response too large: %u > %u",
		     xfer->buf->len, len);

	memcpy(data, xfer->buf->data, xfer->buf->len);

	uhc_xfer_buf_free(uhc_dev, xfer->buf);
	uhc_xfer_free(uhc_dev, xfer);
}

static void test_uhc_hub_control_no_data(struct usb_device *udev,
					 const struct usb_setup_packet *setup)
{
	const struct device *uhc_dev = test_uhc_get_dev();
	struct uhc_transfer *xfer;
	int ret;

	zassert_not_null(udev, "udev is NULL");
	zassert_not_null(setup, "setup is NULL");
	zassert_equal(sys_le16_to_cpu(setup->wLength), 0,
		      "Expected zero-length HUB control request");

	xfer = uhc_xfer_alloc(uhc_dev, 0x00, udev, NULL, NULL);
	zassert_not_null(xfer, "Failed to allocate HUB control transfer");

	memcpy(xfer->setup_pkt, setup, sizeof(*setup));

	ret = uhc_ep_enqueue(uhc_dev, xfer);
	zassert_equal(ret, 0, "HUB control request enqueue failed: %d", ret);

	test_uhc_wait_ep_request();

	zassert_equal(xfer->err, 0,
		      "HUB control request failed: %d",
		      xfer->err);

	uhc_xfer_free(uhc_dev, xfer);
}

static void test_uhc_hub_parse_cfg_desc(const uint8_t *buf,
					size_t len,
					uint8_t iface_num,
					struct test_uhc_hub_info *hub)
{
	const struct usb_cfg_descriptor *cfg;
	const struct usb_if_descriptor *iface = NULL;
	bool found_hub = false;
	bool found_ep = false;
	size_t off;

	zassert_not_null(buf, "Configuration descriptor buffer is NULL");
	zassert_not_null(hub, "HUB info is NULL");
	zassert_true(len >= sizeof(struct usb_cfg_descriptor),
		     "Configuration descriptor too small");

	memset(hub, 0, sizeof(*hub));

	cfg = (const struct usb_cfg_descriptor *)buf;
	hub->cfg_value = cfg->bConfigurationValue;

	off = cfg->bLength;

	while (off + sizeof(struct usb_desc_header) <= len) {
		const struct usb_desc_header *hdr =
			(const struct usb_desc_header *)&buf[off];

		zassert_not_equal(hdr->bLength, 0,
				  "Descriptor with zero bLength at offset %u",
				  off);

		zassert_true(off + hdr->bLength <= len,
			     "Descriptor overruns configuration buffer");

		if (hdr->bDescriptorType == USB_DESC_INTERFACE) {
			iface = (const struct usb_if_descriptor *)hdr;

			if (iface->bInterfaceClass == TEST_UHC_HUB_CLASS &&
			    iface->bInterfaceNumber == iface_num) {
				hub->iface = iface->bInterfaceNumber;
				found_hub = true;
			} else {
				iface = NULL;
			}
		} else if (hdr->bDescriptorType == USB_DESC_ENDPOINT &&
			   iface != NULL) {
			const struct usb_ep_descriptor *ep =
				(const struct usb_ep_descriptor *)hdr;

			if (((ep->bmAttributes &
			      USB_EP_TRANSFER_TYPE_MASK) ==
			     USB_EP_TYPE_INTERRUPT) &&
			    USB_EP_DIR_IS_IN(ep->bEndpointAddress)) {

				hub->ep_in.num = ep->bEndpointAddress;
				hub->ep_in.mps =
					sys_le16_to_cpu(ep->wMaxPacketSize);
				hub->ep_in.interval = ep->bInterval;

				memcpy(&hub->ep_in.desc,
				       ep,
				       sizeof(hub->ep_in.desc));

				found_ep = true;
			}
		}

		off += hdr->bLength;
	}

	zassert_true(found_hub, "No HUB interface found");
	zassert_true(found_ep, "No HUB interrupt IN endpoint found");
}

void test_uhc_hub_init(struct usb_device *udev,
		       const uint8_t iface_num,
		       struct test_uhc_hub_info *hub)
{
	struct usb_cfg_descriptor cfg_desc;
	uint8_t cfg_buf[TEST_UHC_HUB_CFG_BUF_SIZE];
	uint16_t total_len;

	zassert_not_null(udev, "udev is NULL");
	zassert_not_null(hub, "hub is NULL");

	memset(&cfg_desc, 0, sizeof(cfg_desc));
	memset(cfg_buf, 0, sizeof(cfg_buf));

	test_uhc_dev_get_cfg_desc(udev,
				  &cfg_desc,
				  sizeof(cfg_desc));

	zassert_equal(cfg_desc.bDescriptorType,
		      USB_DESC_CONFIGURATION,
		      "Unexpected configuration descriptor type: %u",
		      cfg_desc.bDescriptorType);

	total_len = sys_le16_to_cpu(cfg_desc.wTotalLength);

	zassert_true(total_len >= sizeof(struct usb_cfg_descriptor),
		     "Invalid configuration descriptor length: %u",
		     total_len);

	zassert_true(total_len <= sizeof(cfg_buf),
		     "HUB configuration descriptor too large: %u",
		     total_len);

	test_uhc_dev_get_cfg_desc(udev, cfg_buf, total_len);

	test_uhc_hub_parse_cfg_desc(cfg_buf,
				    total_len,
				    iface_num,
				    hub);

	/*
	 * Same workaround as HID test:
	 * uhc_xfer_alloc() requires endpoint metadata in udev.
	 */
	test_uhc_assign_ep_desc_ptr(udev,
				    hub->ep_in.num,
				    &hub->ep_in.desc);

	test_uhc_dev_set_config(udev, hub->cfg_value);

	test_uhc_hub_get_descriptor(udev, hub);

	LOG_INF("HUB interface %u, ports=%u, interrupt EP=0x%02x, "
		"MPS=%u, interval=%u",
		hub->iface,
		hub->desc.bNbrPorts,
		hub->ep_in.num,
		hub->ep_in.mps,
		hub->ep_in.interval);
}

void test_uhc_hub_get_descriptor(struct usb_device *udev,
				 struct test_uhc_hub_info *hub)
{
	struct usb_setup_packet setup = {
		.bmRequestType =
			(USB_REQTYPE_DIR_TO_HOST << 7) |
			(USB_REQTYPE_TYPE_CLASS << 5) |
			USB_REQTYPE_RECIPIENT_DEVICE,
		.bRequest = USB_HCREQ_GET_DESCRIPTOR,
		.wValue = sys_cpu_to_le16(TEST_UHC_HUB_DESC_TYPE << 8),
		.wIndex = 0,
		.wLength =
			sys_cpu_to_le16(sizeof(struct test_uhc_hub_descriptor)),
	};

	zassert_not_null(hub, "hub is NULL");

	memset(&hub->desc, 0, sizeof(hub->desc));

	test_uhc_hub_control_in(udev,
				&setup,
				&hub->desc,
				sizeof(hub->desc));

	zassert_equal(hub->desc.bDescriptorType,
		      TEST_UHC_HUB_DESC_TYPE,
		      "Unexpected HUB descriptor type: 0x%02x",
		      hub->desc.bDescriptorType);

	zassert_not_equal(hub->desc.bNbrPorts, 0,
			  "HUB reports zero ports");

	LOG_INF("HUB has %u ports, power-good delay=%u ms",
		hub->desc.bNbrPorts,
		hub->desc.bPwrOn2PwrGood * 2U);
}

void test_uhc_hub_get_port_status(struct usb_device *udev,
				  uint8_t port,
				  struct test_uhc_hub_port_status *status)
{
	uint8_t raw[4];
	struct usb_setup_packet setup = {
		.bmRequestType =
			(USB_REQTYPE_DIR_TO_HOST << 7) |
			(USB_REQTYPE_TYPE_CLASS << 5) |
			USB_REQTYPE_RECIPIENT_OTHER,
		.bRequest = USB_HCREQ_GET_STATUS,
		.wValue = 0,
		.wIndex = sys_cpu_to_le16(port),
		.wLength = sys_cpu_to_le16(sizeof(raw)),
	};

	zassert_not_null(status, "status is NULL");
	zassert_not_equal(port, 0, "Invalid HUB port 0");

	memset(raw, 0, sizeof(raw));

	test_uhc_hub_control_in(udev,
				&setup,
				raw,
				sizeof(raw));

	status->status = sys_get_le16(&raw[0]);
	status->change = sys_get_le16(&raw[2]);

	LOG_INF("HUB port %u status=%04x change=%04x",
		port,
		status->status,
		status->change);
}

void test_uhc_hub_set_port_feature(struct usb_device *udev,
				   uint8_t port,
				   uint16_t feature)
{
	struct usb_setup_packet setup = {
		.bmRequestType =
			(USB_REQTYPE_DIR_TO_DEVICE << 7) |
			(USB_REQTYPE_TYPE_CLASS << 5) |
			USB_REQTYPE_RECIPIENT_OTHER,
		.bRequest = USB_HCREQ_SET_FEATURE,
		.wValue = sys_cpu_to_le16(feature),
		.wIndex = sys_cpu_to_le16(port),
		.wLength = 0,
	};

	zassert_not_equal(port, 0, "Invalid HUB port 0");

	test_uhc_hub_control_no_data(udev, &setup);
}

void test_uhc_hub_clear_port_feature(struct usb_device *udev,
				     uint8_t port,
				     uint16_t feature)
{
	struct usb_setup_packet setup = {
		.bmRequestType =
			(USB_REQTYPE_DIR_TO_DEVICE << 7) |
			(USB_REQTYPE_TYPE_CLASS << 5) |
			USB_REQTYPE_RECIPIENT_OTHER,
		.bRequest = USB_HCREQ_CLEAR_FEATURE,
		.wValue = sys_cpu_to_le16(feature),
		.wIndex = sys_cpu_to_le16(port),
		.wLength = 0,
	};

	zassert_not_equal(port, 0, "Invalid HUB port 0");

	test_uhc_hub_control_no_data(udev, &setup);
}

static void test_uhc_hub_process_port(struct usb_device *udev,
				      struct test_uhc_hub_info *hub,
				      uint8_t port)
{
	struct test_uhc_hub_port_status status;

	ARG_UNUSED(hub);

	test_uhc_hub_get_port_status(udev, port, &status);

	if (status.change & TEST_UHC_HUB_PORT_CHANGE_CONNECTION) {
		LOG_INF("HUB port %u connection change", port);

		test_uhc_hub_clear_port_feature(
			udev,
			port,
			USB_HCFS_C_PORT_CONNECTION);
	}

	if (status.change & TEST_UHC_HUB_PORT_CHANGE_ENABLE) {
		LOG_INF("HUB port %u enable change", port);

		test_uhc_hub_clear_port_feature(
			udev,
			port,
			USB_HCFS_C_PORT_ENABLE);
	}

	if (status.change & TEST_UHC_HUB_PORT_CHANGE_SUSPEND) {
		LOG_INF("HUB port %u suspend change", port);

		test_uhc_hub_clear_port_feature(
			udev,
			port,
			USB_HCFS_C_PORT_SUSPEND);
	}

	if (status.change & TEST_UHC_HUB_PORT_CHANGE_OVER_CURRENT) {
		LOG_INF("HUB port %u over-current change", port);

		test_uhc_hub_clear_port_feature(
			udev,
			port,
			USB_HCFS_C_PORT_OVER_CURRENT);
	}

	if (status.change & TEST_UHC_HUB_PORT_CHANGE_RESET) {
		LOG_INF("HUB port %u reset change", port);

		test_uhc_hub_clear_port_feature(
			udev,
			port,
			USB_HCFS_C_PORT_RESET);
	}

	if (status.status & TEST_UHC_HUB_PORT_STATUS_CONNECTION) {
		LOG_INF("HUB port %u connected%s%s",
			port,
			status.status &
				TEST_UHC_HUB_PORT_STATUS_HIGH_SPEED ?
				" HS" : "",
			status.status &
				TEST_UHC_HUB_PORT_STATUS_LOW_SPEED ?
				" LS" : "");
	}
}

void test_uhc_hub_scan(struct usb_device *udev,
		       struct test_uhc_hub_info *hub)
{
	zassert_not_null(udev, "udev is NULL");
	zassert_not_null(hub, "hub is NULL");

	for (uint8_t port = 1;
	     port <= hub->desc.bNbrPorts;
	     port++) {
		test_uhc_hub_process_port(udev, hub, port);
	}
}

void test_uhc_hub_power_ports(struct usb_device *udev,
			      const struct test_uhc_hub_info *hub)
{
	zassert_not_null(udev, "udev is NULL");
	zassert_not_null(hub, "hub is NULL");

	for (uint8_t port = 1; port <= hub->desc.bNbrPorts; port++) {
		test_uhc_hub_set_port_feature(
			udev,
			port,
			USB_HCFS_PORT_POWER);
	}

	k_msleep(hub->desc.bPwrOn2PwrGood * 2U);
}

void test_uhc_hub_interrupt_in(struct usb_device *udev,
			       struct test_uhc_hub_info *hub)
{
	const struct device *uhc_dev = test_uhc_get_dev();
	struct uhc_transfer *xfer;
	size_t len;
	int ret;

	zassert_not_null(udev, "udev is NULL");
	zassert_not_null(hub, "hub is NULL");

	len = MIN((size_t)hub->ep_in.mps,
		  sizeof(hub->change_bitmap));

	memset(hub->change_bitmap, 0,
	       sizeof(hub->change_bitmap));

	hub->change_len = 0;

	xfer = uhc_xfer_alloc_with_buf(uhc_dev,
				       hub->ep_in.num,
				       udev,
				       NULL,
				       NULL,
				       len);
	zassert_not_null(xfer,
			 "Failed to allocate HUB interrupt transfer");

	ret = uhc_ep_enqueue(uhc_dev, xfer);
	zassert_equal(ret, 0,
		      "HUB interrupt IN enqueue failed: %d",
		      ret);

	test_uhc_wait_ep_request();

	zassert_equal(xfer->err, 0,
		      "HUB interrupt IN failed: %d",
		      xfer->err);

	zassert_true(xfer->buf->len <= len,
		     "HUB interrupt response too large: %u > %u",
		     xfer->buf->len,
		     len);

	hub->change_len = xfer->buf->len;

	if (hub->change_len > 0) {
		memcpy(hub->change_bitmap,
		       xfer->buf->data,
		       hub->change_len);

		LOG_HEXDUMP_INF(hub->change_bitmap,
				hub->change_len,
				"HUB change bitmap");
	}

	uhc_xfer_free(uhc_dev, xfer);
}

bool test_uhc_hub_process_change(struct usb_device *udev,
				 struct test_uhc_hub_info *hub,
				 struct test_uhc_hub_change *result)
{
	for (uint8_t index = 1;
	     index <= hub->desc.bNbrPorts;
	     index++) {
		uint8_t byte = index >> 3;
		uint8_t bit = index & 0x07U;
		struct test_uhc_hub_port_status status;

		if (byte >= hub->change_len) {
			break;
		}

		if ((hub->change_bitmap[byte] & BIT(bit)) == 0U) {
			continue;
		}

		test_uhc_hub_get_port_status(udev, index, &status);

		if (status.change &
		    TEST_UHC_HUB_PORT_CHANGE_CONNECTION) {
			test_uhc_hub_clear_port_feature(
				udev,
				index,
				USB_HCFS_C_PORT_CONNECTION);
		}

		if (result != NULL) {
			result->port = index;
			result->status = status.status;
			result->change = status.change;
			result->connected =
				(status.status &
				 TEST_UHC_HUB_PORT_STATUS_CONNECTION) != 0;
		}

		return true;
	}

	return false;
}
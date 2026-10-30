/*
 * Copyright (c) 2026 Roman Leonov <jam_roma@yahoo.com>
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <errno.h>
#include <string.h>
#include <zephyr/kernel.h>
#include <zephyr/net_buf.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/usb/usbd.h>
#include <zephyr/usb/usb_ch9.h>
#include <zephyr/logging/log.h>

#include "loopback.h"
#include "loopback_protocol.h"

LOG_MODULE_REGISTER(usb_loopback, LOG_LEVEL_INF);

static uint8_t payload[LOOPBACK_MAX_PAYLOAD];
static size_t payload_len;

/* This interface deliberately declares no non-control endpoints. */
static struct usb_if_descriptor loopback_if = {
	.bLength = sizeof(struct usb_if_descriptor),
	.bDescriptorType = USB_DESC_INTERFACE,
	.bInterfaceNumber = LOOPBACK_INTERFACE_NUMBER,
	.bAlternateSetting = 0,
	.bNumEndpoints = 0,
	.bInterfaceClass = 0xff,
	.bInterfaceSubClass = 0,
	.bInterfaceProtocol = 0,
	.iInterface = 0,
};

static const struct usb_desc_header *const loopback_desc[] = {
	(const struct usb_desc_header *)&loopback_if,
	NULL,
};

static const void *loopback_get_desc(struct usbd_class_data *const c_data,
				    const enum usbd_speed speed)
{
	ARG_UNUSED(c_data);
	ARG_UNUSED(speed);
	return loopback_desc;
}

static bool loopback_valid_setup(const struct usb_setup_packet *setup,
				uint8_t bm_request_type, uint8_t request)
{
	return setup->bmRequestType == bm_request_type &&
	       setup->bRequest == request &&
	       sys_le16_to_cpu(setup->wIndex) == LOOPBACK_INTERFACE_NUMBER &&
	       sys_le16_to_cpu(setup->wValue) == 0;
}

static int loopback_control_to_dev(struct usbd_class_data *const c_data,
				   const struct usb_setup_packet *const setup,
				   const struct net_buf *const buf)
{
	size_t len = sys_le16_to_cpu(setup->wLength);

	ARG_UNUSED(c_data);

	if (!loopback_valid_setup(setup, LOOPBACK_BMREQUEST_OUT,
				  LOOPBACK_REQ_SET_PAYLOAD) ||
	    len > sizeof(payload)) {
		return -ENOTSUP;
	}

	/* Newer USBD versions first call with NULL to validate the SETUP. */
	if (buf == NULL) {
		return 0;
	}

	if (buf->len != len) {
		return -EINVAL;
	}

	memcpy(payload, buf->data, len);
	payload_len = len;
	LOG_INF("SET_PAYLOAD len=%u", (unsigned int)payload_len);
	return 0;
}

static struct net_buf *loopback_control_to_host(struct usbd_class_data *const c_data,
					const struct usb_setup_packet *const setup)
{
	struct usbd_context *ctx = usbd_class_get_ctx(c_data);
	size_t requested = sys_le16_to_cpu(setup->wLength);
	struct net_buf *buf;
	size_t len;

	if (!loopback_valid_setup(setup, LOOPBACK_BMREQUEST_IN,
				  LOOPBACK_REQ_GET_PAYLOAD)) {
		return NULL;
	}

	len = MIN(requested, payload_len);
	buf = usbd_ep_ctrl_data_in_alloc(ctx, len);
	if (buf == NULL) {
		return NULL;
	}

	net_buf_add_mem(buf, payload, len);
	return buf;
}

static int loopback_init(struct usbd_class_data *const c_data)
{
	ARG_UNUSED(c_data);

	return 0;
}

static const struct usbd_class_api loopback_api = {
	.init = loopback_init,
	.control_to_dev = loopback_control_to_dev,
	.control_to_host = loopback_control_to_host,
	.get_desc = loopback_get_desc,
};

static const struct usbd_cctx_vendor_req loopback_vreqs =
	USBD_VENDOR_REQ(LOOPBACK_REQ_SET_PAYLOAD, LOOPBACK_REQ_GET_PAYLOAD);

USBD_DEFINE_CLASS(usb_loopback, &loopback_api, NULL, &loopback_vreqs);

int loopback_register(struct usbd_context *ctx, enum usbd_speed speed)
{
	return usbd_register_class(ctx, "usb_loopback", speed, 1);
}

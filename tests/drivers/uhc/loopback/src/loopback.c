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
#include <zephyr/drivers/usb/udc.h>

#include "loopback.h"
#include "loopback_protocol.h"

LOG_MODULE_REGISTER(usb_loopback, LOG_LEVEL_INF);

/*
 * USB Loopback Test Device - Proposed Endpoint Layout
 *
 * Interface 0: Vendor-specific (Class 0xFF, Subclass 0x00, Protocol 0x00)
 *
 * Endpoint  Direction  Type           Description
 * -----------------------------------------------------------------------
 * EP0 OUT   0x00      CONTROL        Receive vendor-specific commands
 * EP0 IN    0x80      CONTROL        Return control responses and payloads
 *
 * EP1 OUT   0x01      BULK           Receive bulk test payloads
 * EP1 IN    0x81      BULK           Return received bulk payloads
 *
 * EP2 OUT   0x02      INTERRUPT      Receive interrupt test payloads
 * EP2 IN    0x82      INTERRUPT      Return received interrupt payloads
 *
 * EP3 OUT   0x03      ISOCHRONOUS    Receive isochronous test data
 * EP3 IN    0x83      ISOCHRONOUS    Return isochronous test data
 *
 * Full-Speed Maximum Packet Sizes:
 *   CONTROL:       64 bytes (EP0)
 *   BULK:          64 bytes
 *   INTERRUPT:     Up to 64 bytes
 *   ISOCHRONOUS:   Up to 1023 bytes
 *
 * Loopback Behavior:
 *   CONTROL:       SET_PAYLOAD stores data; GET_PAYLOAD returns it.
 *   BULK:          OUT data is buffered and returned through BULK IN.
 *   INTERRUPT:     OUT data is buffered and returned through INTERRUPT IN.
 *   ISOCHRONOUS:   OUT data is returned through ISOCHRONOUS IN;
 *                  packet loss is permitted (no retransmissions).
 */

static uint8_t payload[LOOPBACK_MAX_PAYLOAD];
static size_t payload_len;

/* BULK small helper */
static struct {
	bool enabled;
	bool armed;
	size_t req_len;
	size_t resp_len;
	size_t host_in_len;
	bool zlp_required;
} bulk_state;

/* This interface deliberately declares no non-control endpoints. */
static struct usb_if_descriptor loopback_if = {
	.bLength = sizeof(struct usb_if_descriptor),
	.bDescriptorType = USB_DESC_INTERFACE,
	.bInterfaceNumber = LOOPBACK_INTERFACE_NUMBER,
	.bAlternateSetting = 0,
	.bNumEndpoints = 2,
	.bInterfaceClass = 0xff,
	.bInterfaceSubClass = 0,
	.bInterfaceProtocol = 0,
	.iInterface = 0,
};

static struct usb_ep_descriptor ep_desc_bulk_out = {
	.bLength = sizeof(struct usb_ep_descriptor),
	.bDescriptorType = USB_DESC_ENDPOINT,
	.bEndpointAddress = LOOPBACK_BULK_OUT_EP,
	.bmAttributes = USB_EP_TYPE_BULK,
	.wMaxPacketSize = sys_cpu_to_le16(64),
	.bInterval = 0,
};

static struct usb_ep_descriptor ep_desc_bulk_in = {
	.bLength = sizeof(struct usb_ep_descriptor),
	.bDescriptorType = USB_DESC_ENDPOINT,
	.bEndpointAddress = LOOPBACK_BULK_IN_EP,
	.bmAttributes = USB_EP_TYPE_BULK,
	.wMaxPacketSize = sys_cpu_to_le16(64),
	.bInterval = 0,
};

static const struct usb_desc_header *const loopback_desc[] = {
	(const struct usb_desc_header *)&loopback_if,
	(const struct usb_desc_header *)&ep_desc_bulk_out,
	(const struct usb_desc_header *)&ep_desc_bulk_in,
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

static int loopback_bulk_prepare_echo(struct usbd_class_data *const c_data, 
	                              size_t req_len, size_t resp_len, size_t host_in_len)
{
	struct net_buf *buf;
	int ret;

	if (!bulk_state.enabled) {
		return -EACCES;
	}

	if (req_len == 0 ||
		req_len > LOOPBACK_MAX_PAYLOAD ||
		resp_len > req_len ||
		resp_len > host_in_len ||
		host_in_len > LOOPBACK_MAX_PAYLOAD) {
		return -EINVAL;
	}

	if (bulk_state.armed) {
		return -EBUSY;
	}

	buf = usbd_ep_buf_alloc(c_data, LOOPBACK_BULK_OUT_EP, req_len);
	if (buf == NULL) {
		return -ENOMEM;
	}

	bulk_state.req_len = req_len;
	bulk_state.resp_len = resp_len;
	bulk_state.host_in_len = host_in_len;
	bulk_state.armed = true;

	bulk_state.zlp_required =
		(resp_len < host_in_len) &&
		(resp_len % LOOPBACK_FS_BULK_MPS == 0);

	LOG_INF("BULK req_len=%u, resp_len=%u, host_in_len=%u", 
		(unsigned int)req_len,
		(unsigned int)resp_len,
		(unsigned int)host_in_len);

	ret = usbd_ep_enqueue(c_data, buf);
	if (ret != 0) {
		bulk_state.armed = false;
		bulk_state.req_len = 0;
		bulk_state.resp_len = 0;
		bulk_state.host_in_len = 0;
		net_buf_unref(buf);
	}

	return ret;
}

static int loopback_control_to_dev(struct usbd_class_data *const c_data,
				   const struct usb_setup_packet *const setup,
				   const struct net_buf *const buf)
{
	uint16_t length = sys_le16_to_cpu(setup->wLength);

	/* Vendor-specific, host-to-device, interface recipient */
	if (setup->bmRequestType != LOOPBACK_BMREQUEST_OUT || 
		setup->wIndex != LOOPBACK_INTERFACE_NUMBER) {
		return -ENOTSUP;
	}

	switch (setup->bRequest) {
	case LOOPBACK_REQ_SET_PAYLOAD:
		if (length > LOOPBACK_MAX_PAYLOAD) {
			return -EINVAL;
		}

		/*
		 * For requests with a DATA OUT stage, Zephyr calls
		 * this handler twice:
		 *
		 * 1. buf == NULL: validate the request.
		 * 2. buf != NULL: process the received data.
		 */
		if (buf == NULL) {
			return 0;
		}

		if (buf->len != length) {
			return -EINVAL;
		}

		memcpy(payload, buf->data, length);
		payload_len = length;
		LOG_INF("SET_PAYLOAD len=%u", (unsigned int)payload_len);

		return 0;

	case LOOPBACK_BULK_REQ_ECHO: {
		uint16_t echo_req_len;
		uint16_t echo_resp_len;
		uint16_t echo_host_in_len;

		if (length != 6 ||
			sys_le16_to_cpu(setup->wIndex) != LOOPBACK_INTERFACE_NUMBER ||
			sys_le16_to_cpu(setup->wValue) != 0) {
			return -EINVAL;
		}

		/* First callback validates SETUP; second receives DATA OUT. */
		if (buf == NULL) {
			return 0;
		}

		if (buf->len != 6) {
			return -EINVAL;
		}

		echo_req_len = sys_get_le16(&buf->data[0]);
		echo_resp_len = sys_get_le16(&buf->data[2]);
		echo_host_in_len = sys_get_le16(&buf->data[4]);

		LOG_INF("ECHO BULK prepare, req_len=%u, resp_len=%u, host_in_len=%u",
			echo_req_len, echo_resp_len, echo_host_in_len);

		return loopback_bulk_prepare_echo(c_data,
						echo_req_len,
						echo_resp_len, 
						echo_host_in_len);
	}

	default:
		return -ENOTSUP;
	}
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

	LOG_INF("GET_PAYLOAD len=%u", (unsigned int)len);

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

static void loopback_enable(struct usbd_class_data *const c_data)
{
	ARG_UNUSED(c_data);

	bulk_state.enabled = true;
	LOG_INF("USB loopback configured");
}

static void loopback_disable(struct usbd_class_data *const c_data)
{
	ARG_UNUSED(c_data);

	bulk_state.enabled = false;
	bulk_state.armed = false;
	bulk_state.req_len = 0;
	bulk_state.resp_len = 0;

	LOG_INF("USB loopback disabled");
}

#if (0)
static int loopback_request(struct usbd_class_data *const c_data,
			    struct net_buf *buf, int err)
{
	struct usbd_context *ctx = usbd_class_get_ctx(c_data);
	uint8_t ep = udc_get_buf_info(buf)->ep;
	struct net_buf *reply;
	int ret;

	if (ep == LOOPBACK_BULK_OUT_EP) {
		if (err != 0 ||
		    !bulk_state.armed ||
		    buf->len != bulk_state.req_len) {
			LOG_ERR("BULK OUT failed: err=%d len=%u req_len=%zu",
				err, buf->len, bulk_state.req_len);

			bulk_state.armed = false;
			bulk_state.req_len = 0;
			bulk_state.resp_len = 0;
			return usbd_ep_buf_free(ctx, buf);
		}

		reply = usbd_ep_buf_alloc(c_data, LOOPBACK_BULK_IN_EP, bulk_state.resp_len);

		if (reply == NULL) {
			LOG_ERR("BULK IN buffer allocation failed");
			bulk_state.armed = false;
			bulk_state.req_len = 0;
			bulk_state.resp_len = 0;
			return usbd_ep_buf_free(ctx, buf);
		}

		net_buf_add_mem(reply, buf->data, bulk_state.resp_len);

		ret = usbd_ep_buf_free(ctx, buf);
		if (ret != 0) {
			LOG_ERR("BULK OUT buffer free failed: %d", ret);
		}

		ret = usbd_ep_enqueue(c_data, reply);
		if (ret != 0) {
			LOG_ERR("BULK IN enqueue failed: %d", ret);
			net_buf_unref(reply);
		}

		/* Keep armed until BULK IN completes. */
		return ret;
	}

	if (ep == LOOPBACK_BULK_IN_EP) {
		// if (err != 0) {
		// 	LOG_ERR("BULK IN failed: %d", err);
		// }

		// // TODO: add ZLP handling

		// bulk_state.armed = false;
		// bulk_state.req_len = 0;
		// bulk_state.resp_len = 0;

		// return usbd_ep_buf_free(ctx, buf);
		 bool send_zlp = bulk_state.zlp_required;

		ret = usbd_ep_buf_free(ctx, buf);

		if (err != 0) {
			LOG_ERR("BULK IN failed: %d", err);
			goto bulk_in_done;
		}

		if (ret != 0) {
			LOG_ERR("BULK IN buffer free failed: %d", ret);
			goto bulk_in_done;
		}

		if (send_zlp) {
			bulk_state.zlp_required = false;

			reply = usbd_ep_buf_alloc(c_data, LOOPBACK_BULK_IN_EP, 0);
			if (reply == NULL) {
			LOG_ERR("BULK IN ZLP allocation failed");
			ret = -ENOMEM;
			goto bulk_in_done;
			}

			ret = usbd_ep_enqueue(c_data, reply);
			if (ret != 0) {
			LOG_ERR("BULK IN ZLP enqueue failed: %d", ret);
			net_buf_unref(reply);
			goto bulk_in_done;
		}

		/* Keep armed until ZLP completion. */
		return 0;
	}

	return usbd_ep_buf_free(ctx, buf);
}
#endif // 
static int loopback_request(struct usbd_class_data *const c_data,
			    struct net_buf *buf, int err)
{
	struct usbd_context *ctx = usbd_class_get_ctx(c_data);
	uint8_t ep = udc_get_buf_info(buf)->ep;
	struct net_buf *reply;
	int ret;

	if (ep == LOOPBACK_BULK_OUT_EP) {
		if (err != 0 ||
		    !bulk_state.armed ||
		    buf->len != bulk_state.req_len) {
			LOG_ERR("BULK OUT failed: err=%d len=%u req_len=%zu",
				err, buf->len, bulk_state.req_len);

			bulk_state.armed = false;
			bulk_state.zlp_required = false;
			bulk_state.req_len = 0;
			bulk_state.resp_len = 0;
			bulk_state.host_in_len = 0;

			return usbd_ep_buf_free(ctx, buf);
		}

		reply = usbd_ep_buf_alloc(c_data, LOOPBACK_BULK_IN_EP,
					 bulk_state.resp_len);
		if (reply == NULL) {
			LOG_ERR("BULK IN buffer allocation failed");

			bulk_state.armed = false;
			bulk_state.zlp_required = false;
			bulk_state.req_len = 0;
			bulk_state.resp_len = 0;
			bulk_state.host_in_len = 0;

			return usbd_ep_buf_free(ctx, buf);
		}

		net_buf_add_mem(reply, buf->data, bulk_state.resp_len);

		ret = usbd_ep_buf_free(ctx, buf);
		if (ret != 0) {
			LOG_ERR("BULK OUT buffer free failed: %d", ret);
		}

		ret = usbd_ep_enqueue(c_data, reply);
		if (ret != 0) {
			LOG_ERR("BULK IN enqueue failed: %d", ret);
			net_buf_unref(reply);

			bulk_state.armed = false;
			bulk_state.zlp_required = false;
			bulk_state.req_len = 0;
			bulk_state.resp_len = 0;
			bulk_state.host_in_len = 0;
		}

		/* Keep armed until BULK IN (and optional ZLP) completes. */
		return ret;
	}

	if (ep == LOOPBACK_BULK_IN_EP) {
		bool send_zlp = bulk_state.zlp_required;

		ret = usbd_ep_buf_free(ctx, buf);
		if (ret != 0) {
			LOG_ERR("BULK IN buffer free failed: %d", ret);
			goto bulk_in_done;
		}

		if (err != 0) {
			LOG_ERR("BULK IN failed: %d", err);
			ret = err;
			goto bulk_in_done;
		}

		if (send_zlp) {
			bulk_state.zlp_required = false;

			reply = usbd_ep_buf_alloc(c_data,
						 LOOPBACK_BULK_IN_EP, 0);
			if (reply == NULL) {
				LOG_ERR("BULK IN ZLP allocation failed");
				ret = -ENOMEM;
				goto bulk_in_done;
			}

			LOG_INF("BULK IN sending ZLP");

			ret = usbd_ep_enqueue(c_data, reply);
			if (ret != 0) {
				LOG_ERR("BULK IN ZLP enqueue failed: %d", ret);
				net_buf_unref(reply);
				goto bulk_in_done;
			}

			/*
			 * ZLP is queued. Keep the transfer armed until
			 * its completion callback.
			 */
			return 0;
		}

bulk_in_done:
		bulk_state.armed = false;
		bulk_state.zlp_required = false;
		bulk_state.req_len = 0;
		bulk_state.resp_len = 0;
		bulk_state.host_in_len = 0;

		return ret;
	}

	return usbd_ep_buf_free(ctx, buf);
}

static const struct usbd_class_api loopback_api = {
	.init = loopback_init,
	.enable = loopback_enable,
	.disable = loopback_disable,
	.request = loopback_request,
	.control_to_dev = loopback_control_to_dev,
	.control_to_host = loopback_control_to_host,
	.get_desc = loopback_get_desc,
};

static const struct usbd_cctx_vendor_req loopback_vreqs =
    USBD_VENDOR_REQ(LOOPBACK_REQ_SET_PAYLOAD,
                    LOOPBACK_REQ_GET_PAYLOAD,
                    LOOPBACK_BULK_REQ_ECHO);

USBD_DEFINE_CLASS(usb_loopback, &loopback_api, NULL, &loopback_vreqs);

int loopback_register(struct usbd_context *ctx, enum usbd_speed speed)
{
	return usbd_register_class(ctx, "usb_loopback", speed, 1);
}

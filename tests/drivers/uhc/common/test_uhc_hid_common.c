#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/ztest.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/drivers/usb/uhc.h>
#include <zephyr/usb/usb_ch9.h>

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(test_uhc_hid_common, LOG_LEVEL_INF);

#include "test_uhc_common.h"
#include "test_uhc_hid_common.h"

#define TEST_UHC_HID_CFG_BUF_SIZE	512
#define TEST_UHC_HID_DESC_TYPE_HID	0x21
#define TEST_UHC_HID_DESC_TYPE_REPORT	0x22
struct test_uhc_hid_class_descriptor {
	uint8_t bLength;
	uint8_t bDescriptorType;
	uint16_t bcdHID;
	uint8_t bCountryCode;
	uint8_t bNumDescriptors;
	uint8_t bReportDescriptorType;
	uint16_t wDescriptorLength;
} __packed;

static void test_uhc_hid_parse_cfg_desc(const uint8_t *buf,
					 size_t len,
					 const uint8_t iface_num,
					 struct test_uhc_hid_info *hid)
{
	const struct usb_cfg_descriptor *cfg;
	const struct usb_if_descriptor *iface = NULL;
	bool found_hid = false;
	bool found_report_desc = false;
	bool found_ep = false;
	size_t off;

	zassert_not_null(buf, "Configuration descriptor buffer is NULL");
	zassert_not_null(hid, "HID info is NULL");
	zassert_true(len >= sizeof(struct usb_cfg_descriptor),
		     "Configuration descriptor too small");

	memset(hid, 0, sizeof(*hid));

	cfg = (const struct usb_cfg_descriptor *)buf;
	hid->cfg_value = cfg->bConfigurationValue;

	off = cfg->bLength;

	while (off + 2 <= len) {
		const struct usb_desc_header *hdr =
			(const struct usb_desc_header *)&buf[off];

		zassert_not_equal(hdr->bLength, 0,
				  "Descriptor with zero bLength at offset %u",
				  off);

		zassert_true(off + hdr->bLength <= len,
			     "Descriptor overruns config buffer at offset %u",
			     off);

		if (hdr->bDescriptorType == USB_DESC_INTERFACE) {
			iface = (const struct usb_if_descriptor *)hdr;

			if ((iface->bInterfaceClass == TEST_UHC_HID_CLASS) &&
			    (iface->bInterfaceNumber == iface_num)) {
				found_hid = true;
				hid->iface = iface->bInterfaceNumber;
			} else {
				iface = NULL;
			}
		} else if ((hdr->bDescriptorType == TEST_UHC_HID_DESC_TYPE_HID) &&
			   (iface != NULL)) {
			const struct test_uhc_hid_class_descriptor *hid_desc =
				(const struct test_uhc_hid_class_descriptor *)hdr;

			zassert_true(hdr->bLength >= sizeof(*hid_desc),
				     "HID descriptor too small: %u",
				     hdr->bLength);

			zassert_equal(hid_desc->bReportDescriptorType,
				      TEST_UHC_HID_DESC_TYPE_REPORT,
				      "Unexpected HID sub-descriptor type: 0x%02x",
				      hid_desc->bReportDescriptorType);

			hid->report_desc_len =
				sys_le16_to_cpu(hid_desc->wDescriptorLength);

			found_report_desc = true;
		} else if ((hdr->bDescriptorType == USB_DESC_ENDPOINT) &&
			   (iface != NULL)) {
			const struct usb_ep_descriptor *ep =
				(const struct usb_ep_descriptor *)hdr;

			if (((ep->bmAttributes & USB_EP_TRANSFER_TYPE_MASK) ==
			     USB_EP_TYPE_INTERRUPT) &&
			    USB_EP_DIR_IS_IN(ep->bEndpointAddress)) {
				hid->ep_in.num = ep->bEndpointAddress;
				hid->ep_in.mps = sys_le16_to_cpu(ep->wMaxPacketSize);
				hid->ep_in.interval = ep->bInterval;
				hid->ep_in.desc = (struct usb_ep_descriptor *)ep;
				found_ep = true;
			}
		}

		off += hdr->bLength;
	}

	zassert_true(found_hid, "No HID interface found");
	zassert_true(found_report_desc, "No HID report descriptor length found");
	zassert_true(found_ep, "No HID interrupt IN endpoint found");
}

#if (0)
static void test_uhc_hid_parse_cfg_desc(const uint8_t *buf,
					 size_t len,
					 struct test_uhc_hid_info *hid)
{
	const struct usb_cfg_descriptor *cfg;
	const struct usb_if_descriptor *iface = NULL;
	bool found_hid = false;
	bool found_ep = false;
	size_t off;

	zassert_not_null(buf, "Configuration descriptor buffer is NULL");
	zassert_not_null(hid, "HID info is NULL");
	zassert_true(len >= sizeof(struct usb_cfg_descriptor),
		     "Configuration descriptor too small");

	memset(hid, 0, sizeof(*hid));

	cfg = (const struct usb_cfg_descriptor *)buf;
	hid->cfg_value = cfg->bConfigurationValue;

	off = cfg->bLength;

	while (off + 2 <= len) {
		const struct usb_desc_header *hdr =
			(const struct usb_desc_header *)&buf[off];

		zassert_not_equal(hdr->bLength, 0,
				  "Descriptor with zero bLength at offset %u",
				  off);

		zassert_true(off + hdr->bLength <= len,
			     "Descriptor overruns config buffer at offset %u",
			     off);

		if (hdr->bDescriptorType == USB_DESC_INTERFACE) {
			iface = (const struct usb_if_descriptor *)hdr;

			if (iface->bInterfaceClass == TEST_UHC_HID_CLASS) {
				found_hid = true;
				hid->iface = iface->bInterfaceNumber;
			} else {
				iface = NULL;
			}
		} else if ((hdr->bDescriptorType == USB_DESC_ENDPOINT) &&
			   (iface != NULL)) {
			const struct usb_ep_descriptor *ep =
				(const struct usb_ep_descriptor *)hdr;

			if (((ep->bmAttributes & USB_EP_TRANSFER_TYPE_MASK) ==
			     USB_EP_TYPE_INTERRUPT) &&
			    USB_EP_DIR_IS_IN(ep->bEndpointAddress)) {
				hid->ep_in.num = ep->bEndpointAddress;
				hid->ep_in.mps = sys_le16_to_cpu(ep->wMaxPacketSize);
				hid->ep_in.interval = ep->bInterval;
				hid->ep_in.desc = (struct usb_ep_descriptor *)ep;
				found_ep = true;
				break;
			}
		}

		off += hdr->bLength;
	}

	zassert_true(found_hid, "No HID interface found");
	zassert_true(found_ep, "No HID interrupt IN endpoint found");
}
#endif // 

void test_uhc_hid_init(struct usb_device *udev,
		       const uint8_t iface_num,
		       struct test_uhc_hid_info *hid)
{
	struct usb_cfg_descriptor cfg_desc;
	uint8_t cfg_buf[TEST_UHC_HID_CFG_BUF_SIZE];
	uint16_t total_len;

	zassert_not_null(udev, "udev is NULL");
	zassert_not_null(hid, "hid is NULL");

	memset(&cfg_desc, 0, sizeof(cfg_desc));
	memset(cfg_buf, 0, sizeof(cfg_buf));

	/* We are not able to create xfer in usbh layer for endpoints without ep data */

	test_uhc_dev_get_cfg_desc(udev, &cfg_desc, sizeof(cfg_desc));

	zassert_equal(cfg_desc.bDescriptorType, USB_DESC_CONFIGURATION,
		      "Unexpected config descriptor type: %u",
		      cfg_desc.bDescriptorType);

	total_len = sys_le16_to_cpu(cfg_desc.wTotalLength);

	zassert_true(total_len >= sizeof(struct usb_cfg_descriptor),
		     "Invalid config descriptor total length: %u",
		     total_len);

	zassert_true(total_len <= sizeof(cfg_buf),
		     "Config descriptor too large for MSC test buffer: %u",
		     total_len);

	test_uhc_dev_get_cfg_desc(udev, cfg_buf, total_len);

	test_uhc_hid_parse_cfg_desc(cfg_buf, total_len, iface_num, hid);

	/* Workaround: for usbh, assign ep_desc_ptr */
	test_uhc_assign_ep_desc_ptr(udev, hid->ep_in.num, hid->ep_in.desc);

	test_uhc_dev_set_config(udev, hid->cfg_value);
}

void test_uhc_hid_interrupt_in_once(struct usb_device *udev,
				    const struct test_uhc_hid_info *hid,
				    void *buf,
				    size_t len)
{
	const struct device *uhc_dev = test_uhc_get_dev();
	struct uhc_transfer *xfer;
	int ret;

	zassert_not_null(udev, "udev is NULL");
	zassert_not_null(hid, "hid is NULL");
	zassert_not_null(buf, "buf is NULL");
	zassert_true(len >= hid->ep_in.mps,
		     "Interrupt IN buffer too small: len=%u mps=%u",
		     len, hid->ep_in.mps);

	xfer = uhc_xfer_alloc_with_buf(uhc_dev,
				       hid->ep_in.num,
				       udev,
				       NULL,
				       NULL,
				       len);
	zassert_not_null(xfer, "Failed to allocate interrupt IN transfer");

	// Do we need it ?
	// xfer->interval = hid->ep_in.interval; 

	ret = uhc_ep_enqueue(uhc_dev, xfer);
	zassert_equal(ret, 0, "Interrupt IN enqueue failed: %d", ret);

	test_uhc_wait_ep_request();

	zassert_equal(xfer->err, 0,
		      "Interrupt IN transfer failed: %d", xfer->err);

	zassert_true(xfer->buf->len <= hid->ep_in.mps,
		     "Interrupt IN length too large: len=%u, mps=%u",
		     xfer->buf->len,
		     hid->ep_in.mps);

	memcpy(buf, xfer->buf->data, xfer->buf->len);

	uhc_xfer_free(uhc_dev, xfer);
}

void test_uhc_hid_interrupt_in_poll_ms(struct usb_device *udev,
				    const struct test_uhc_hid_info *hid,
				    uint32_t msec, 
				    void *buf,
				    size_t len)
{
	const struct device *uhc_dev = test_uhc_get_dev();
	const k_timepoint_t timepoint = sys_timepoint_calc(K_MSEC(msec));
	struct uhc_transfer *xfer;

	int ret;

	zassert_not_null(udev, "udev is NULL");
	zassert_not_null(hid, "hid is NULL");
	zassert_not_equal(msec, 0, "Poll duration is zero");

	xfer = uhc_xfer_alloc(uhc_dev, hid->ep_in.num, udev, NULL, NULL);
	zassert_not_null(xfer, "Failed to allocate interrupt IN transfer");

	buf = uhc_xfer_buf_alloc(uhc_dev, len);
	zassert_not_null(buf, "Failed to allocate buffer for interrupt IN transfer");

	xfer->buf = buf;

	while (!sys_timepoint_expired(timepoint)) {
		net_buf_reset(buf);
		
		ret = uhc_ep_enqueue(uhc_dev, xfer);

		zassert_equal(ret, 0,
				"Interrupt IN enqueue failed: %d", ret);
				
		test_uhc_wait_ep_request();
	
		zassert_equal(xfer->err, 0,
				"Interrupt IN transfer failed: %d", xfer->err);
	
		zassert_true(xfer->buf->len <= hid->ep_in.mps,
				"Interrupt IN length too large: %u > %u",
				xfer->buf->len,
				hid->ep_in.mps);
		
		if (xfer->buf->len > 0) {
			LOG_HEXDUMP_INF(xfer->buf->data, xfer->buf->len,
					"HID interrupt IN report");
		}
	}


	uhc_xfer_free(uhc_dev, xfer);
	net_buf_unref(buf);
}

void test_uhc_hid_get_report_desc(struct usb_device *udev,
				  const struct test_uhc_hid_info *hid,
				  void *buf,
				  size_t len)
{
	const struct device *uhc_dev = test_uhc_get_dev();
	struct usb_setup_packet setup = {
		.bmRequestType = USB_REQTYPE_DIR_TO_HOST << 7 |
				 USB_REQTYPE_TYPE_STANDARD << 5 |
				 USB_REQTYPE_RECIPIENT_INTERFACE,
		.bRequest = USB_SREQ_GET_DESCRIPTOR,
		.wValue = sys_cpu_to_le16(TEST_UHC_HID_DESC_TYPE_REPORT << 8),
		.wIndex = 0,
		.wLength = 0,
	};
	struct uhc_transfer *xfer;
	int ret;

	zassert_not_null(udev, "udev is NULL");
	zassert_not_null(hid, "hid is NULL");
	zassert_not_null(buf, "buf is NULL");
	zassert_not_equal(hid->report_desc_len, 0,
			  "HID report descriptor length is zero");
	zassert_true(len >= hid->report_desc_len,
		     "Report descriptor buffer too small: len=%u desc_len=%u",
		     len, hid->report_desc_len);

	setup.wIndex = sys_cpu_to_le16(hid->iface);
	setup.wLength = sys_cpu_to_le16(hid->report_desc_len);

	xfer = uhc_xfer_alloc_with_buf(uhc_dev, 0x80, udev, NULL, NULL,
				       hid->report_desc_len);
	zassert_not_null(xfer, "Failed to allocate report descriptor transfer");

	memcpy(xfer->setup_pkt, &setup, sizeof(setup));

	ret = uhc_ep_enqueue(uhc_dev, xfer);
	zassert_equal(ret, 0, "GET_REPORT_DESCRIPTOR enqueue failed: %d", ret);

	test_uhc_wait_ep_request();

	zassert_equal(xfer->err, 0,
		      "GET_REPORT_DESCRIPTOR failed: %d", xfer->err);

	zassert_equal(xfer->buf->len, hid->report_desc_len,
		      "Unexpected report descriptor length: got %u expected %u",
		      xfer->buf->len, hid->report_desc_len);

	memcpy(buf, xfer->buf->data, xfer->buf->len);

	uhc_xfer_free(uhc_dev, xfer);
}
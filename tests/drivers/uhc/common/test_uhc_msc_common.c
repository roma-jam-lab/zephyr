#include <string.h>
#include <zephyr/kernel.h>
#include <zephyr/ztest.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/net_buf.h>
#include <zephyr/usb/usb_ch9.h>
#include <zephyr/drivers/usb/uhc.h>

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(test_uhc_msc_common, LOG_LEVEL_INF);

#include "test_uhc_msc_common.h"

#define TEST_UHC_MSC_CFG_BUF_SIZE	512
#define TEST_UHC_MSC_CBW_LEN		31
#define TEST_UHC_MSC_CSW_LEN		13

#define TEST_UHC_MSC_CBW_FLAG_DATA_IN	0x80
#define TEST_UHC_MSC_CBW_FLAG_DATA_OUT	0x00

#define TEST_UHC_MSC_CSW_STATUS_PASS	0x00

static uint32_t test_uhc_msc_tag = 1;

static uint32_t test_uhc_msc_next_tag(void)
{
	return test_uhc_msc_tag++;
}

static bool test_uhc_msc_is_bulk_ep(const struct usb_ep_descriptor *ep_desc)
{
	return (ep_desc->bmAttributes & USB_EP_TRANSFER_TYPE_MASK) ==
		USB_EP_TYPE_BULK;
}

static bool test_uhc_msc_is_bulk_in_ep(const struct usb_ep_descriptor *ep_desc)
{
	return test_uhc_msc_is_bulk_ep(ep_desc) &&
	       USB_EP_DIR_IS_IN(ep_desc->bEndpointAddress);
}

static bool test_uhc_msc_is_bulk_out_ep(const struct usb_ep_descriptor *ep_desc)
{
	return test_uhc_msc_is_bulk_ep(ep_desc) &&
	       USB_EP_DIR_IS_OUT(ep_desc->bEndpointAddress);
}

static void test_uhc_msc_parse_cfg_desc(const uint8_t *cfg,
					size_t cfg_len,
					struct test_uhc_msc_info *msc)
{
	const struct usb_if_descriptor *iface_desc = NULL;
	bool found_msc_iface = false;
	size_t offset = 0;

	zassert_not_null(cfg, "cfg is NULL");
	zassert_not_null(msc, "msc is NULL");

	memset(msc, 0, sizeof(*msc));

	while (offset + 2 <= cfg_len) {
		uint8_t len = cfg[offset];
		uint8_t type = cfg[offset + 1];

		zassert_true(len >= 2, "Invalid descriptor length at %u", offset);
		zassert_true(offset + len <= cfg_len,
			     "Descriptor overruns config buffer: off=%u len=%u cfg_len=%u",
			     offset, len, cfg_len);

		switch (type) {
		case USB_DESC_INTERFACE:
			iface_desc = (const struct usb_if_descriptor *)&cfg[offset];

			if (iface_desc->bInterfaceClass == TEST_UHC_MSC_CLASS &&
			    iface_desc->bInterfaceSubClass ==
				    TEST_UHC_MSC_SUBCLASS_SCSI &&
			    iface_desc->bInterfaceProtocol ==
				    TEST_UHC_MSC_PROTOCOL_BULK_ONLY) {
				msc->iface = iface_desc->bInterfaceNumber;
				found_msc_iface = true;
				printk("Found MSC interface %u\n", msc->iface);
			} else {
				found_msc_iface = false;
			}
			break;

		case USB_DESC_ENDPOINT:
			if (found_msc_iface) {
				const struct usb_ep_descriptor *ep_desc =
					(const struct usb_ep_descriptor *)&cfg[offset];

				if (test_uhc_msc_is_bulk_in_ep(ep_desc)) {
					msc->ep_in.num = ep_desc->bEndpointAddress;
					msc->ep_in.mps = sys_le16_to_cpu(ep_desc->wMaxPacketSize);
					msc->ep_in.desc = (struct usb_ep_descriptor *)ep_desc;

					printk("MSC IN EP=%02x mps=%u\n",
						msc->ep_in.num,
						msc->ep_in.mps);
				} else if (test_uhc_msc_is_bulk_out_ep(ep_desc)) {
					msc->ep_out.num = ep_desc->bEndpointAddress;
					msc->ep_out.mps = sys_le16_to_cpu(ep_desc->wMaxPacketSize);
					msc->ep_out.desc = (struct usb_ep_descriptor *)ep_desc;

					printk("MSC OUT EP=%02x mps=%u\n",
						msc->ep_out.num,
						msc->ep_out.mps);
				}
			}
			break;

		default:
			break;
		}

		offset += len;
	}

	zassert_not_equal(msc->ep_in.num, 0,
			  "MSC bulk IN endpoint not found");
	zassert_not_equal(msc->ep_out.num, 0,
			  "MSC bulk OUT endpoint not found");
	zassert_not_equal(msc->ep_in.mps, 0,
			  "MSC bulk IN MPS is zero");
	zassert_not_equal(msc->ep_out.mps, 0,
			  "MSC bulk OUT MPS is zero");
}

static size_t test_uhc_msc_bulk_in(struct usb_device *udev,
				   const struct test_uhc_msc_info *msc,
				   void *buf,
				   size_t len)
{
	const struct device *uhc_dev = test_uhc_get_dev();
	struct uhc_transfer *xfer;
	size_t actual_len;
	int ret;

	zassert_not_null(udev, "udev is NULL");
	zassert_not_null(msc, "msc is NULL");
	zassert_not_null(buf, "buf is NULL");
	zassert_true(len > 0, "len is zero");

	xfer = uhc_xfer_alloc_with_buf(uhc_dev, msc->ep_in.num, udev, NULL, NULL, len);
	zassert_not_null(xfer, "Failed to allocate bulk IN transfer");
	zassert_not_null(xfer->buf, "Bulk IN transfer buffer is NULL");

	ret = uhc_ep_enqueue(uhc_dev, xfer);
	zassert_equal(ret, 0, "bulk IN enqueue failed: %d", ret);

	test_uhc_wait_ep_request();

	zassert_equal(xfer->err, 0, "bulk IN transfer failed: %d", xfer->err);
	zassert_true(xfer->buf->len <= len,
		     "bulk IN received more than requested: got %u max %u",
		     xfer->buf->len, len);

	actual_len = xfer->buf->len;
	memcpy(buf, xfer->buf->data, actual_len);

	uhc_xfer_buf_free(uhc_dev, xfer->buf);
	uhc_xfer_free(uhc_dev, xfer);

	return actual_len;
}

static void test_uhc_msc_bulk_out(struct usb_device *udev,
				  const struct test_uhc_msc_info *msc,
				  const void *buf,
				  size_t len)
{
	const struct device *uhc_dev = test_uhc_get_dev();
	struct uhc_transfer *xfer;
	int ret;

	zassert_not_null(udev, "udev is NULL");
	zassert_not_null(msc, "msc is NULL");
	zassert_not_null(buf, "buf is NULL");
	zassert_true(len > 0, "len is zero");

	xfer = uhc_xfer_alloc_with_buf(uhc_dev, msc->ep_out.num, udev, NULL, NULL, len);
	zassert_not_null(xfer, "Failed to allocate bulk OUT transfer");
	zassert_not_null(xfer->buf, "Bulk OUT transfer buffer is NULL");

	net_buf_add_mem(xfer->buf, buf, len);

	ret = uhc_ep_enqueue(uhc_dev, xfer);
	zassert_equal(ret, 0, "bulk OUT enqueue failed: %d", ret);

	test_uhc_wait_ep_request();

	zassert_equal(xfer->err, 0, "bulk OUT transfer failed: %d", xfer->err);

	uhc_xfer_buf_free(uhc_dev, xfer->buf);
	uhc_xfer_free(uhc_dev, xfer);
}

static void test_uhc_msc_send_cbw(struct usb_device *udev,
				  const struct test_uhc_msc_info *msc,
				  const struct test_uhc_msc_cbw *cbw)
{
	test_uhc_msc_bulk_out(udev, msc, cbw, sizeof(*cbw));
}

static void test_uhc_msc_read_csw(struct usb_device *udev,
				  const struct test_uhc_msc_info *msc,
				  uint32_t expected_tag)
{
	struct test_uhc_msc_csw csw;
	size_t len;

	memset(&csw, 0, sizeof(csw));

	len = test_uhc_msc_bulk_in(udev, msc, &csw, sizeof(csw));

	zassert_equal(len, sizeof(csw),
		      "Unexpected CSW length: got %u expected %u",
		      len, sizeof(csw));

	zassert_equal(sys_le32_to_cpu(csw.dCSWSignature),
		      TEST_UHC_MSC_CSW_SIGNATURE,
		      "Invalid CSW signature: %08x",
		      sys_le32_to_cpu(csw.dCSWSignature));

	zassert_equal(sys_le32_to_cpu(csw.dCSWTag), expected_tag,
		      "Invalid CSW tag: got %08x expected %08x",
		      sys_le32_to_cpu(csw.dCSWTag), expected_tag);

	zassert_equal(csw.bCSWStatus, TEST_UHC_MSC_CSW_STATUS_PASS,
		      "MSC command failed, CSW status: %u",
		      csw.bCSWStatus);
}

void test_uhc_msc_get_max_lun(struct usb_device *udev,
			      struct test_uhc_msc_info *msc)
{
	const struct device *uhc_dev = test_uhc_get_dev();
	struct usb_setup_packet setup = {
		.bmRequestType = USB_REQTYPE_DIR_TO_HOST << 7 |
				 USB_REQTYPE_TYPE_CLASS << 5 |
				 USB_REQTYPE_RECIPIENT_INTERFACE,
		.bRequest = TEST_UHC_MSC_REQ_GET_MAX_LUN,
		.wValue = 0,
		.wIndex = sys_cpu_to_le16(msc->iface),
		.wLength = sys_cpu_to_le16(1),
	};
	struct uhc_transfer *xfer;
	int ret;

	zassert_not_null(udev, "udev is NULL");
	zassert_not_null(msc, "msc is NULL");

	xfer = uhc_xfer_alloc_with_buf(uhc_dev, 0x80, udev, NULL, NULL, 1);
	zassert_not_null(xfer, "Failed to allocate GET_MAX_LUN transfer");
	zassert_not_null(xfer->buf, "GET_MAX_LUN transfer buffer is NULL");

	memcpy(xfer->setup_pkt, &setup, sizeof(setup));

	ret = uhc_ep_enqueue(uhc_dev, xfer);
	zassert_equal(ret, 0, "GET_MAX_LUN enqueue failed: %d", ret);

	test_uhc_wait_ep_request();

	if (xfer->err == -EPIPE) {
		/*
		 * BOT spec allows devices that do not support GET_MAX_LUN
		 * to STALL. In that case host shall assume max LUN 0.
		 */
		LOG_INF("GET_MAX_LUN STALLed, assuming max LUN 0");
		msc->max_lun = 0;
	} else {
		zassert_equal(xfer->err, 0,
			      "GET_MAX_LUN failed: %d", xfer->err);

		zassert_equal(xfer->buf->len, 1,
			      "Unexpected GET_MAX_LUN length: %u",
			      xfer->buf->len);

		msc->max_lun = xfer->buf->data[0];
	}

	uhc_xfer_buf_free(uhc_dev, xfer->buf);
	uhc_xfer_free(uhc_dev, xfer);
}

void test_uhc_msc_bulk_only_reset(struct usb_device *udev,
				  const struct test_uhc_msc_info *msc)
{
	const struct device *uhc_dev = test_uhc_get_dev();
	struct usb_setup_packet setup = {
		.bmRequestType = USB_REQTYPE_DIR_TO_DEVICE << 7 |
				 USB_REQTYPE_TYPE_CLASS << 5 |
				 USB_REQTYPE_RECIPIENT_INTERFACE,
		.bRequest = TEST_UHC_MSC_REQ_BULK_ONLY_RESET,
		.wValue = 0,
		.wIndex = sys_cpu_to_le16(msc->iface),
		.wLength = 0,
	};
	struct uhc_transfer *xfer;
	int ret;

	zassert_not_null(udev, "udev is NULL");
	zassert_not_null(msc, "msc is NULL");

	xfer = uhc_xfer_alloc(uhc_dev, 0x00, udev, NULL, NULL);
	zassert_not_null(xfer, "Failed to allocate BULK_ONLY_RESET transfer");

	memcpy(xfer->setup_pkt, &setup, sizeof(setup));

	ret = uhc_ep_enqueue(uhc_dev, xfer);
	zassert_equal(ret, 0, "BULK_ONLY_RESET enqueue failed: %d", ret);

	test_uhc_wait_ep_request();

	zassert_equal(xfer->err, 0,
		      "BULK_ONLY_RESET failed: %d", xfer->err);

	uhc_xfer_free(uhc_dev, xfer);
}

void test_uhc_msc_init(struct usb_device *udev,
		       struct test_uhc_msc_info *msc)
{
	struct usb_cfg_descriptor cfg_desc;
	uint8_t cfg_buf[TEST_UHC_MSC_CFG_BUF_SIZE];
	uint16_t total_len;

	zassert_not_null(udev, "udev is NULL");
	zassert_not_null(msc, "msc is NULL");

	memset(&cfg_desc, 0, sizeof(cfg_desc));
	memset(cfg_buf, 0, sizeof(cfg_buf));

	/* We are not able to create xfer for endpoints without request configuration */

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

	test_uhc_msc_parse_cfg_desc(cfg_buf, total_len, msc);

	/* 
	 * Workaround: 
	 */
	test_uhc_assign_ep_desc_ptr(udev, msc->ep_in.num, msc->ep_in.desc);
	test_uhc_assign_ep_desc_ptr(udev, msc->ep_out.num, msc->ep_out.desc);

	/*
	 * Most MSC devices only start accepting BOT traffic after configuration.
	 * Use configuration value from the descriptor instead of hardcoding 1.
	 */
	test_uhc_dev_set_config(udev, cfg_desc.bConfigurationValue);

	test_uhc_msc_get_max_lun(udev, msc);
}

void test_uhc_msc_inquiry(struct usb_device *udev,
			  const struct test_uhc_msc_info *msc,
			  uint8_t lun,
			  void *buf,
			  size_t len)
{
	struct test_uhc_msc_cbw cbw;
	uint32_t tag;

	zassert_not_null(udev, "udev is NULL");
	zassert_not_null(msc, "msc is NULL");
	zassert_not_null(buf, "buf is NULL");
	zassert_true(len >= 36, "INQUIRY buffer too small: %u", len);
	zassert_true(lun <= msc->max_lun,
		     "Invalid LUN %u, max LUN %u", lun, msc->max_lun);

	tag = test_uhc_msc_next_tag();

	memset(&cbw, 0, sizeof(cbw));

	cbw.dCBWSignature = sys_cpu_to_le32(TEST_UHC_MSC_CBW_SIGNATURE);
	cbw.dCBWTag = sys_cpu_to_le32(tag);
	cbw.dCBWDataTransferLength = sys_cpu_to_le32(len);
	cbw.bmCBWFlags = TEST_UHC_MSC_CBW_FLAG_DATA_IN;
	cbw.bCBWLUN = lun;
	cbw.bCBWCBLength = 6;

	cbw.CBWCB[0] = TEST_UHC_SCSI_INQUIRY;
	cbw.CBWCB[4] = len;

	test_uhc_msc_send_cbw(udev, msc, &cbw);

	test_uhc_msc_bulk_in(udev, msc, buf, len);

	test_uhc_msc_read_csw(udev, msc, tag);
}

void test_uhc_msc_read_capacity_10(struct usb_device *udev,
				   const struct test_uhc_msc_info *msc,
				   uint8_t lun,
				   void *buf,
				   size_t len)
{
	struct test_uhc_msc_cbw cbw;
	size_t actual_len;
	uint32_t tag;

	zassert_not_null(udev, "udev is NULL");
	zassert_not_null(msc, "msc is NULL");
	zassert_not_null(buf, "buf is NULL");
	zassert_true(len >= 8, "READ_CAPACITY_10 buffer too small: %u", len);
	zassert_true(lun <= msc->max_lun,
		     "Invalid LUN %u, max LUN %u", lun, msc->max_lun);

	tag = test_uhc_msc_next_tag();

	memset(&cbw, 0, sizeof(cbw));

	cbw.dCBWSignature = sys_cpu_to_le32(TEST_UHC_MSC_CBW_SIGNATURE);
	cbw.dCBWTag = sys_cpu_to_le32(tag);
	cbw.dCBWDataTransferLength = sys_cpu_to_le32(8);
	cbw.bmCBWFlags = TEST_UHC_MSC_CBW_FLAG_DATA_IN;
	cbw.bCBWLUN = lun;
	cbw.bCBWCBLength = 10;

	cbw.CBWCB[0] = TEST_UHC_SCSI_READ_CAPACITY_10;


	LOG_WRN("read cap");

	test_uhc_msc_send_cbw(udev, msc, &cbw);

	actual_len = test_uhc_msc_bulk_in(udev, msc, buf, 8);

	zassert_equal(actual_len, 8,
		      "Unexpected READ_CAPACITY_10 length: %u",
		      actual_len);

	test_uhc_msc_read_csw(udev, msc, tag);
}

void test_uhc_msc_test_unit_ready(struct usb_device *udev,
				  const struct test_uhc_msc_info *msc,
				  uint8_t lun)
{
	struct test_uhc_msc_cbw cbw;
	uint32_t tag;

	zassert_not_null(udev, "udev is NULL");
	zassert_not_null(msc, "msc is NULL");
	zassert_true(lun <= msc->max_lun,
		     "Invalid LUN %u, max LUN %u", lun, msc->max_lun);

	tag = test_uhc_msc_next_tag();

	memset(&cbw, 0, sizeof(cbw));

	cbw.dCBWSignature = sys_cpu_to_le32(TEST_UHC_MSC_CBW_SIGNATURE);
	cbw.dCBWTag = sys_cpu_to_le32(tag);
	cbw.dCBWDataTransferLength = 0;
	cbw.bmCBWFlags = TEST_UHC_MSC_CBW_FLAG_DATA_IN;
	cbw.bCBWLUN = lun;
	cbw.bCBWCBLength = 6;

	cbw.CBWCB[0] = TEST_UHC_SCSI_TEST_UNIT_READY;

	test_uhc_msc_send_cbw(udev, msc, &cbw);
	test_uhc_msc_read_csw(udev, msc, tag);
}

void test_uhc_msc_request_sense(struct usb_device *udev,
				const struct test_uhc_msc_info *msc,
				uint8_t lun,
				void *buf,
				size_t len)
{
	struct test_uhc_msc_cbw cbw;
	uint32_t tag;
	size_t actual_len;

	zassert_not_null(udev, "udev is NULL");
	zassert_not_null(msc, "msc is NULL");
	zassert_not_null(buf, "buf is NULL");
	zassert_true(len >= TEST_UHC_MSC_REQUEST_SENSE_LEN,
		     "REQUEST_SENSE buffer too small: %u", len);
	zassert_true(lun <= msc->max_lun,
		     "Invalid LUN %u, max LUN %u", lun, msc->max_lun);

	tag = test_uhc_msc_next_tag();

	memset(&cbw, 0, sizeof(cbw));

	cbw.dCBWSignature = sys_cpu_to_le32(TEST_UHC_MSC_CBW_SIGNATURE);
	cbw.dCBWTag = sys_cpu_to_le32(tag);
	cbw.dCBWDataTransferLength =
		sys_cpu_to_le32(TEST_UHC_MSC_REQUEST_SENSE_LEN);
	cbw.bmCBWFlags = TEST_UHC_MSC_CBW_FLAG_DATA_IN;
	cbw.bCBWLUN = lun;
	cbw.bCBWCBLength = 6;

	cbw.CBWCB[0] = TEST_UHC_SCSI_REQUEST_SENSE;
	cbw.CBWCB[4] = TEST_UHC_MSC_REQUEST_SENSE_LEN;

	test_uhc_msc_send_cbw(udev, msc, &cbw);

	actual_len = test_uhc_msc_bulk_in(udev, msc, buf,
					  TEST_UHC_MSC_REQUEST_SENSE_LEN);

	zassert_equal(actual_len, TEST_UHC_MSC_REQUEST_SENSE_LEN,
		      "Unexpected REQUEST_SENSE length: %u", actual_len);

	test_uhc_msc_read_csw(udev, msc, tag);
}

void test_uhc_msc_read_10(struct usb_device *udev,
			  const struct test_uhc_msc_info *msc,
			  uint8_t lun,
			  uint32_t lba,
			  uint16_t blocks,
			  void *buf,
			  size_t len)
{
	struct test_uhc_msc_cbw cbw;
	uint32_t tag;
	size_t actual_len;

	zassert_not_null(udev, "udev is NULL");
	zassert_not_null(msc, "msc is NULL");
	zassert_not_null(buf, "buf is NULL");
	zassert_true(blocks > 0, "blocks is zero");
	zassert_true(len > 0, "len is zero");
	zassert_true(lun <= msc->max_lun,
		     "Invalid LUN %u, max LUN %u", lun, msc->max_lun);

	tag = test_uhc_msc_next_tag();

	memset(&cbw, 0, sizeof(cbw));

	cbw.dCBWSignature = sys_cpu_to_le32(TEST_UHC_MSC_CBW_SIGNATURE);
	cbw.dCBWTag = sys_cpu_to_le32(tag);
	cbw.dCBWDataTransferLength = sys_cpu_to_le32(len);
	cbw.bmCBWFlags = TEST_UHC_MSC_CBW_FLAG_DATA_IN;
	cbw.bCBWLUN = lun;
	cbw.bCBWCBLength = 10;

	cbw.CBWCB[0] = TEST_UHC_SCSI_READ_10;
	sys_put_be32(lba, &cbw.CBWCB[2]);
	sys_put_be16(blocks, &cbw.CBWCB[7]);

	test_uhc_msc_send_cbw(udev, msc, &cbw);

	actual_len = test_uhc_msc_bulk_in(udev, msc, buf, len);

	zassert_equal(actual_len, len,
		      "Unexpected READ_10 length: got %u expected %u",
		      actual_len, len);

	test_uhc_msc_read_csw(udev, msc, tag);
}
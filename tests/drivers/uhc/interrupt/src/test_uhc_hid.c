#include <string.h>

#include <zephyr/ztest.h>
#include <zephyr/drivers/usb/uhc.h>
#include <zephyr/sys/byteorder.h>

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(test_uhc_hid, LOG_LEVEL_INF);

#include "test_uhc_common.h"
#include "test_uhc_hid_common.h"

#define TEST_UHC_HID_ADDR			1
#define TEST_UHC_HID_REPORT_DESC_BUF_SIZE	512

#if (0)
ZTEST(test_uhc_hid, test_probe_interface)
{
	struct usb_device udev;
	struct test_uhc_hid_info hid;
	enum usb_device_speed speed;

	test_uhc_init();

	test_uhc_prepare_addressed_device(&udev, &speed, TEST_UHC_HID_ADDR);

	test_uhc_hid_init(&udev, 0, &hid);

	zassert_not_equal(hid.ep_in.num, 0,
			  "HID interrupt IN endpoint not found");
	zassert_not_equal(hid.ep_in.mps, 0,
			  "HID interrupt IN MPS is zero");
	zassert_not_equal(hid.ep_in.interval, 0,
			  "HID interrupt IN interval is zero");

	LOG_INF("HID iface=%u cfg=%u int_in=0x%02x mps=%u interval=%u",
		hid.iface,
		hid.cfg_value,
		hid.ep_in.num,
		hid.ep_in.mps,
		hid.ep_in.interval);

	test_uhc_device_cleanup();
}

ZTEST(test_uhc_hid, test_interrupt_in_once)
{
	struct usb_device udev;
	struct test_uhc_hid_info hid;
	enum usb_device_speed speed;
	uint8_t report_desc[TEST_UHC_HID_REPORT_DESC_BUF_SIZE];
	uint8_t report[64];

	test_uhc_init();

	test_uhc_prepare_addressed_device(&udev, &speed, TEST_UHC_HID_ADDR);

	test_uhc_hid_init(&udev, &hid);

	zassert_true(hid.ep_in.mps <= sizeof(report),
		     "HID report buffer too small: mps=%u",
		     hid.ep_in.mps);

	zassert_true(hid.report_desc_len <= sizeof(report_desc),
		     "Report descriptor too large: %u",
		     hid.report_desc_len);

	memset(report_desc, 0, sizeof(report_desc));

	test_uhc_hid_get_report_desc(&udev, &hid,
				     report_desc, sizeof(report_desc));

	LOG_INF("HID report descriptor length=%u", hid.report_desc_len);
	LOG_HEXDUMP_INF(report_desc, hid.report_desc_len,
			"HID report descriptor");

	memset(report, 0, sizeof(report));

	test_uhc_hid_interrupt_in_once(&udev, &hid, report, hid.ep_in.mps);

	LOG_HEXDUMP_INF(report, hid.ep_in.mps, "HID interrupt IN report");

	test_uhc_device_cleanup();
}

ZTEST(test_uhc_hid, test_interrupt_in_poll_5s)
{
	struct usb_device udev;
	struct test_uhc_hid_info hid;
	enum usb_device_speed speed;
	uint8_t report_desc[TEST_UHC_HID_REPORT_DESC_BUF_SIZE];
	uint8_t report[64];

	test_uhc_init();

	test_uhc_prepare_addressed_device(&udev, &speed, TEST_UHC_HID_ADDR);

	test_uhc_hid_init(&udev, 1, &hid);

	zassert_true(hid.report_desc_len <= sizeof(report_desc),
		     "Report descriptor too large: %u",
		     hid.report_desc_len);

	memset(report_desc, 0, sizeof(report_desc));

	test_uhc_hid_get_report_desc(&udev, &hid,
				     report_desc, sizeof(report_desc));

	test_uhc_hid_interrupt_in_poll_ms(&udev, &hid, 5000, report, hid.ep_in.mps);

	test_uhc_device_cleanup();
}
#endif // 

ZTEST(test_uhc_hid, test_two_ifaces_interrupt_enqueue_dequeue)
{
	struct usb_device udev;
	struct test_uhc_hid_info hid_mouse;
	struct test_uhc_hid_info hid_keyboard;
	enum usb_device_speed speed;
	uint8_t report_desc_mouse[TEST_UHC_HID_REPORT_DESC_BUF_SIZE];
	uint8_t report_desc_keyboard[TEST_UHC_HID_REPORT_DESC_BUF_SIZE];
	const struct device *uhc_dev;
	struct uhc_transfer *xfer_kb;
	struct uhc_transfer *xfer_mouse;
	int ret;

	uhc_dev = test_uhc_init();

	test_uhc_prepare_addressed_device(&udev, &speed, TEST_UHC_HID_ADDR);

	test_uhc_hid_init(&udev, 0, &hid_keyboard);
	test_uhc_hid_init(&udev, 1, &hid_mouse);

	zassert_true(hid_mouse.report_desc_len <= sizeof(report_desc_mouse),
		     "Mouse report descriptor too large: %u",
		     hid_mouse.report_desc_len);
	zassert_true(hid_keyboard.report_desc_len <= sizeof(report_desc_keyboard),
		     "Keyboard report descriptor too large: %u",
		     hid_keyboard.report_desc_len);	     

	memset(report_desc_mouse, 0, sizeof(report_desc_mouse));
	memset(report_desc_keyboard, 0, sizeof(report_desc_keyboard));

	test_uhc_hid_get_report_desc(&udev, 
				     &hid_keyboard,
				     report_desc_keyboard,
				     sizeof(report_desc_keyboard));

	test_uhc_hid_get_report_desc(&udev, 
				     &hid_mouse,
				     report_desc_mouse,
				     sizeof(report_desc_mouse));

	/* Prepare xfer's */
	xfer_kb = uhc_xfer_alloc_with_buf(uhc_dev,
				       hid_keyboard.ep_in.num,
				       &udev,
				       NULL,
				       NULL,
				       hid_keyboard.ep_in.mps);
	zassert_not_null(xfer_kb, "Failed to allocate interrupt IN transfer");

	xfer_mouse = uhc_xfer_alloc_with_buf(uhc_dev,
				       hid_mouse.ep_in.num,
				       &udev,
				       NULL,
				       NULL,
				       hid_mouse.ep_in.mps);
	zassert_not_null(xfer_mouse, "Failed to allocate interrupt IN transfer");

	/* Enqueue both */
	ret = uhc_ep_enqueue(uhc_dev, xfer_kb);
	zassert_equal(ret, 0, "Interrupt IN enqueue failed: %d", ret);

	ret = uhc_ep_enqueue(uhc_dev, xfer_mouse);
	zassert_equal(ret, 0, "Interrupt IN enqueue failed: %d", ret);

	/* Work */
	k_msleep(300);

	/* Enqueue both */
	ret = uhc_ep_dequeue(uhc_dev, xfer_kb);
	zassert_equal(ret, 0, "Interrupt IN dequeue failed: %d", ret);
	ret = uhc_ep_dequeue(uhc_dev, xfer_mouse);
	zassert_equal(ret, 0, "Interrupt IN dequeue failed: %d", ret);

	/* Wait for ECONNRESET two times */
	test_uhc_wait_ep_request();
	zassert_equal(xfer_kb->err, -ECONNRESET, "Waited -ECONNRESET, got: %d", xfer_kb->err);

	test_uhc_wait_ep_request();
	zassert_equal(xfer_mouse->err, -ECONNRESET, "Waited -ECONNRESET, got: %d", xfer_mouse->err);

	uhc_xfer_free(uhc_dev, xfer_kb);
	uhc_xfer_free(uhc_dev, xfer_mouse);

	test_uhc_device_cleanup();
}

#if (1)
ZTEST(test_uhc_hid, test_interrupt_enqueue_dequeue)
{
	struct usb_device udev;
	struct test_uhc_hid_info hid;
	enum usb_device_speed speed;
	uint8_t report_desc[TEST_UHC_HID_REPORT_DESC_BUF_SIZE];
	const struct device *uhc_dev; /* = test_uhc_get_dev() */;
	struct uhc_transfer *xfer;
	int ret;

	uhc_dev = test_uhc_init();

	test_uhc_prepare_addressed_device(&udev, &speed, TEST_UHC_HID_ADDR);

	test_uhc_hid_init(&udev, 0, &hid);

	zassert_true(hid.report_desc_len <= sizeof(report_desc),
		     "Report descriptor too large: %u",
		     hid.report_desc_len);

	memset(report_desc, 0, sizeof(report_desc));

	test_uhc_hid_get_report_desc(&udev, &hid, report_desc, sizeof(report_desc));

	/* Prepare xfer */
	xfer = uhc_xfer_alloc_with_buf(uhc_dev, hid.ep_in.num, &udev, NULL, NULL, hid.ep_in.mps);
	zassert_not_null(xfer, "Failed to allocate interrupt IN transfer");
	
	// Do we need it ?
	// xfer->interval = hid->ep_in.interval; 
	
	/* Enqueue / Dequeue */
	ret = uhc_ep_enqueue(uhc_dev, xfer);
	zassert_equal(ret, 0, "Interrupt IN enqueue failed: %d", ret);

	ret = uhc_ep_dequeue(uhc_dev, xfer);
	zassert_equal(ret, 0, "Interrupt IN dequeue failed: %d", ret);

	/* Wait for ECONNRESET */
	test_uhc_wait_ep_request();
	zassert_equal(xfer->err, -ECONNRESET, "Waited -ECONNRESET, got: %d", xfer->err);

	/* Enqueue / Dequeue with different intervals */

	for (uint16_t delay_ms = 10; delay_ms <= 300; delay_ms += 10) {
		net_buf_reset(xfer->buf);
		
		ret = uhc_ep_enqueue(uhc_dev, xfer);
		zassert_equal(ret, 0, "Interrupt IN enqueue failed: %d", ret);
		/* Give channel to work a bit */
		k_msleep(delay_ms);
		ret = uhc_ep_dequeue(uhc_dev, xfer);
		zassert_equal(ret, 0, "Interrupt IN dequeue failed: %d", ret);
		/* Wait for ECONNRESET */
		test_uhc_wait_ep_request();
		zassert_equal(xfer->err, -ECONNRESET, "Waited -ECONNRESET, got: %d", xfer->err);
	}

	uhc_xfer_free(uhc_dev, xfer);

	test_uhc_device_cleanup();
}
#endif //

ZTEST_SUITE(test_uhc_hid, NULL, NULL, NULL, NULL, NULL);
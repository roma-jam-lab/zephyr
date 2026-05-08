/*
 * Copyright (c) 2026 Roman Leonov <jam_roma@yahoo.com>
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/ztest.h>
#include <zephyr/drivers/usb/uhc.h>
#include <zephyr/sys/byteorder.h>

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(test_uhc_buffer, LOG_LEVEL_INF);

#include "test_uhc_common.h"

ZTEST(uhc_buffer, test_xfer_alloc_with_buf_max_size_mps64)
{
	const struct device *uhc_dev;
	struct uhc_transfer *xfer;
	struct usb_device udev = {
		.dev_desc = {
			.bMaxPacketSize0 = 64,
		},
	};

	test_uhc_print_current_stack_usage();

	uhc_dev = test_uhc_init();

	xfer = uhc_xfer_alloc_with_buf(uhc_dev, USB_CONTROL_EP_IN, &udev, 
				       NULL, NULL, TEST_UHC_MAX_XFER_DATA_BUF_SIZE);

	zassert_not_null(xfer, "Failed to allocate UHC transfer with %d-byte buffer", 
				TEST_UHC_MAX_XFER_DATA_BUF_SIZE);
	zassert_not_null(xfer->buf, "Transfer buffer is NULL");
	zassert_true(net_buf_tailroom(xfer->buf) >= TEST_UHC_MAX_XFER_DATA_BUF_SIZE,
			"Buffer tailroom too small: %u", net_buf_tailroom(xfer->buf));

	uhc_xfer_buf_free(uhc_dev, xfer->buf);
	xfer->buf = NULL;

	zassert_ok(uhc_xfer_free(uhc_dev, xfer));

	test_uhc_shutdown();

	test_uhc_print_current_stack_usage();
}

ZTEST(uhc_buffer, test_xfer_alloc_with_buf_max_size_mps8)
{
	const struct device *uhc_dev;
	struct uhc_transfer *xfer;
	struct usb_device udev = {
		.dev_desc = {
			.bMaxPacketSize0 = 8,
		},
	};

	test_uhc_print_current_stack_usage();

	uhc_dev = test_uhc_init();

	xfer = uhc_xfer_alloc_with_buf(uhc_dev, USB_CONTROL_EP_IN, &udev, 
				       NULL, NULL, TEST_UHC_MAX_XFER_DATA_BUF_SIZE);

	zassert_not_null(xfer, "Failed to allocate UHC transfer with %d-byte buffer", 
				TEST_UHC_MAX_XFER_DATA_BUF_SIZE);
	zassert_not_null(xfer->buf, "Transfer buffer is NULL");
	zassert_true(net_buf_tailroom(xfer->buf) >= TEST_UHC_MAX_XFER_DATA_BUF_SIZE,
			"Buffer tailroom too small: %u", net_buf_tailroom(xfer->buf));

	uhc_xfer_buf_free(uhc_dev, xfer->buf);
	xfer->buf = NULL;

	zassert_ok(uhc_xfer_free(uhc_dev, xfer));

	test_uhc_shutdown();

	test_uhc_print_current_stack_usage();
}

ZTEST_SUITE(uhc_buffer, NULL, NULL, NULL, NULL, NULL);
/*
 * Copyright (c) 2026 Roman Leonov <jam_roma@yahoo.com>
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/ztest.h>
#include <zephyr/drivers/usb/uhc.h>
#include <zephyr/sys/byteorder.h>

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(test_uhc_msc, LOG_LEVEL_INF);

#include "test_uhc_common.h"
#include "test_uhc_msc_common.h"

#define TEST_UHC_MSC_ADDR		1
#define TEST_UHC_MSC_READ_BUF_SIZE	(64 * 512)

ZTEST(uhc_bulk_msc, test_probe_interface)
{
	struct usb_device udev;
	struct test_uhc_msc_info msc;
	enum usb_device_speed speed;

	test_uhc_init();

	test_uhc_prepare_addressed_device(&udev, &speed, TEST_UHC_MSC_ADDR);

	test_uhc_msc_init(&udev, &msc);

	zassert_not_equal(msc.ep_in.num, 0,
			  "MSC bulk IN endpoint not found");

	zassert_not_equal(msc.ep_out.num, 0,
			  "MSC bulk OUT endpoint not found");

	zassert_not_equal(msc.ep_in.mps, 0,
			  "MSC bulk IN MPS is zero");

	zassert_not_equal(msc.ep_out.mps, 0,
			  "MSC bulk OUT MPS is zero");

	zassert_true(msc.max_lun <= 15,
		     "Unexpected MSC max LUN: %u", msc.max_lun);

	test_uhc_device_cleanup();
}


ZTEST(uhc_bulk_msc, test_inquiry)
{
	struct usb_device udev;
	struct test_uhc_msc_info msc;
	enum usb_device_speed speed;
	uint8_t inquiry[36];

	test_uhc_init();

	test_uhc_prepare_addressed_device(&udev, &speed, TEST_UHC_MSC_ADDR);

	test_uhc_msc_init(&udev, &msc);

	memset(inquiry, 0, sizeof(inquiry));

	test_uhc_msc_inquiry(&udev, &msc, 0, inquiry, sizeof(inquiry));

	// zassert_equal(inquiry[4], 31,
		//       "Unexpected INQUIRY additional length: %u",
		//       inquiry[4]);

	LOG_INF("MSC vendor: %.8s product: %.16s revision: %.4s",
		&inquiry[8], &inquiry[16], &inquiry[32]);

	test_uhc_device_cleanup();
}

ZTEST(uhc_bulk_msc, test_read_capacity_10)
{
	struct usb_device udev;
	struct test_uhc_msc_info msc;
	enum usb_device_speed speed;
	uint8_t capacity[8];
	uint32_t last_lba;
	uint32_t block_size;

	test_uhc_init();

	test_uhc_prepare_addressed_device(&udev, &speed, TEST_UHC_MSC_ADDR);

	test_uhc_msc_init(&udev, &msc);

	memset(capacity, 0, sizeof(capacity));

	test_uhc_msc_read_capacity_10(&udev, &msc, 0,
				      capacity, sizeof(capacity));

	last_lba = sys_get_be32(&capacity[0]);
	block_size = sys_get_be32(&capacity[4]);

	zassert_not_equal(block_size, 0,
			  "READ_CAPACITY_10 returned zero block size");

	LOG_INF("MSC capacity: blocks_num=%u, block_size=%u", last_lba + 1, block_size);

	test_uhc_device_cleanup();
}

ZTEST(uhc_bulk_msc, test_bulk_only_reset)
{
	struct usb_device udev;
	struct test_uhc_msc_info msc;
	enum usb_device_speed speed;

	test_uhc_init();

	test_uhc_prepare_addressed_device(&udev, &speed, TEST_UHC_MSC_ADDR);

	test_uhc_msc_init(&udev, &msc);

	test_uhc_msc_bulk_only_reset(&udev, &msc);

	/*
	 * Verify device still responds to class request after reset.
	 */
	test_uhc_msc_get_max_lun(&udev, &msc);

	zassert_true(msc.max_lun <= 15,
		     "Unexpected MSC max LUN: %u", msc.max_lun);

	test_uhc_device_cleanup();
}

ZTEST(uhc_bulk_msc, test_request_sense)
{
	struct usb_device udev;
	struct test_uhc_msc_info msc;
	enum usb_device_speed speed;
	uint8_t sense[TEST_UHC_MSC_REQUEST_SENSE_LEN];

	test_uhc_init();

	test_uhc_prepare_addressed_device(&udev, &speed, TEST_UHC_MSC_ADDR);

	test_uhc_msc_init(&udev, &msc);

	memset(sense, 0, sizeof(sense));

	test_uhc_msc_request_sense(&udev, &msc, 0, sense, sizeof(sense));

	LOG_INF("REQUEST_SENSE: response_code=0x%02x sense_key=0x%02x asc=0x%02x ascq=0x%02x",
		sense[0] & 0x7f, sense[2] & 0x0f, sense[12], sense[13]);

	test_uhc_device_cleanup();
}

ZTEST(uhc_bulk_msc, test_test_unit_ready)
{
	struct usb_device udev;
	struct test_uhc_msc_info msc;
	enum usb_device_speed speed;

	test_uhc_init();

	test_uhc_prepare_addressed_device(&udev, &speed, TEST_UHC_MSC_ADDR);

	test_uhc_msc_init(&udev, &msc);

	test_uhc_msc_test_unit_ready(&udev, &msc, 0);

	test_uhc_device_cleanup();
}

ZTEST(uhc_bulk_msc, test_read_10_first_block)
{
	struct usb_device udev;
	struct test_uhc_msc_info msc;
	enum usb_device_speed speed;
	uint8_t capacity[8];
	uint8_t block[512];
	uint32_t block_size;

	memset(capacity, 0, sizeof(capacity));

	test_uhc_init();

	test_uhc_prepare_addressed_device(&udev, &speed, TEST_UHC_MSC_ADDR);

	test_uhc_msc_init(&udev, &msc);

	test_uhc_msc_test_unit_ready(&udev, &msc, 0);

	test_uhc_msc_read_capacity_10(&udev, &msc, 0, capacity, sizeof(capacity));

	// block_size = sys_get_be32(&capacity[4]);

	// zassert_equal(block_size, sizeof(block),
	// 	      "Unexpected block size: %u", block_size);

	memset(block, 0, sizeof(block));

	test_uhc_msc_read_10(&udev, &msc, 0, 0, 1, block, sizeof(block));

	LOG_HEXDUMP_INF(block, 64, "MSC LBA0 first 64 bytes");

	test_uhc_device_cleanup();
}

#if (1)
ZTEST(uhc_bulk_msc, test_read_10_block_counts)
{
	struct usb_device udev;
	struct test_uhc_msc_info msc;
	enum usb_device_speed speed;
	uint8_t capacity[8];
	uint8_t read_buf[TEST_UHC_MSC_READ_BUF_SIZE];
	uint32_t last_lba;
	uint32_t block_size;
	uint32_t block_count;
	uint32_t len;
	static const uint16_t blocks_list[] = {
		1, 2, 4, 8, 16, 32
	};

	test_uhc_init();

	test_uhc_prepare_addressed_device(&udev, &speed, TEST_UHC_MSC_ADDR);

	test_uhc_msc_init(&udev, &msc);
	test_uhc_msc_get_max_lun(&udev, &msc);
	test_uhc_msc_test_unit_ready(&udev, &msc, 0);

	memset(capacity, 0, sizeof(capacity));

	test_uhc_msc_read_capacity_10(&udev, &msc, 0,
				      capacity, sizeof(capacity));

	last_lba = sys_get_be32(&capacity[0]);
	block_size = sys_get_be32(&capacity[4]);
	block_count = last_lba + 1;

	zassert_not_equal(block_size, 0,
			  "READ_CAPACITY_10 returned zero block size");

	zassert_true(block_size <= 512,
		     "Unsupported block size for test buffer: %u",
		     block_size);

	zassert_true(block_count >= 240,
		     "Device too small for read block count test: %u blocks",
		     block_count);

	for (size_t i = 0; i < ARRAY_SIZE(blocks_list); i++) {
		uint16_t blocks = blocks_list[i];

		len = block_size * blocks;

		zassert_true(len <= sizeof(read_buf),
			     "READ_10 buffer too small: blocks=%u len=%u",
			     blocks, len);

		memset(read_buf, 0, len);

		LOG_INF("READ_10: lba=0 blocks=%u len=%u",
			blocks, len);

		test_uhc_msc_read_10(&udev, &msc, 0, 0,
				     blocks, read_buf, len);
	}

	test_uhc_device_cleanup();
}
#endif // 

ZTEST_SUITE(uhc_bulk_msc, NULL, NULL, NULL, NULL, NULL);
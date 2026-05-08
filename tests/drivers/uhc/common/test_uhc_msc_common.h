/*
 * Copyright (c) 2026 Roman Leonov <jam_roma@yahoo.com>
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/kernel.h>
#include <zephyr/device.h>

#ifndef TEST_UHC_MSC_COMMON_H
#define TEST_UHC_MSC_COMMON_H

#include <stdint.h>
#include <zephyr/drivers/usb/uhc.h>

#include "test_uhc_common.h"

#define TEST_UHC_MSC_CLASS			0x08
#define TEST_UHC_MSC_SUBCLASS_SCSI		0x06
#define TEST_UHC_MSC_PROTOCOL_BULK_ONLY		0x50

#define TEST_UHC_MSC_REQ_GET_MAX_LUN		0xFE
#define TEST_UHC_MSC_REQ_BULK_ONLY_RESET	0xFF

#define TEST_UHC_MSC_CBW_SIGNATURE		0x43425355
#define TEST_UHC_MSC_CSW_SIGNATURE		0x53425355

#define TEST_UHC_SCSI_TEST_UNIT_READY		0x00
#define TEST_UHC_SCSI_INQUIRY			0x12
#define TEST_UHC_SCSI_READ_CAPACITY_10		0x25
#define TEST_UHC_SCSI_REQUEST_SENSE		0x03
#define TEST_UHC_SCSI_READ_10			0x28

#define TEST_UHC_MSC_REQUEST_SENSE_LEN		18

struct test_uhc_msc_ep_bulk {
	uint8_t num;
	uint16_t mps;
	/** Pointer to the endpoint descriptor */
	struct usb_ep_descriptor *desc;
};
struct test_uhc_msc_info {
	uint8_t iface;
	struct test_uhc_msc_ep_bulk ep_in;
	struct test_uhc_msc_ep_bulk ep_out;
	uint8_t max_lun;
};

struct test_uhc_msc_cbw {
	uint32_t dCBWSignature;
	uint32_t dCBWTag;
	uint32_t dCBWDataTransferLength;
	uint8_t bmCBWFlags;
	uint8_t bCBWLUN;
	uint8_t bCBWCBLength;
	uint8_t CBWCB[16];
} __packed;

struct test_uhc_msc_csw {
	uint32_t dCSWSignature;
	uint32_t dCSWTag;
	uint32_t dCSWDataResidue;
	uint8_t bCSWStatus;
} __packed;

void test_uhc_msc_init(struct usb_device *udev, struct test_uhc_msc_info *msc);

void test_uhc_msc_get_max_lun(struct usb_device *udev, struct test_uhc_msc_info *msc);

void test_uhc_msc_bulk_only_reset(struct usb_device *udev,
				  const struct test_uhc_msc_info *msc);

void test_uhc_msc_inquiry(struct usb_device *udev,
			  const struct test_uhc_msc_info *msc,
			  uint8_t lun,
			  void *buf,
			  size_t len);

void test_uhc_msc_read_capacity_10(struct usb_device *udev,
				   const struct test_uhc_msc_info *msc,
				   uint8_t lun,
				   void *buf,
				   size_t len);

void test_uhc_msc_test_unit_ready(struct usb_device *udev,
				  const struct test_uhc_msc_info *msc,
				  uint8_t lun);

void test_uhc_msc_request_sense(struct usb_device *udev,
				const struct test_uhc_msc_info *msc,
				uint8_t lun,
				void *buf,
				size_t len);

void test_uhc_msc_read_10(struct usb_device *udev,
			  const struct test_uhc_msc_info *msc,
			  uint8_t lun,
			  uint32_t lba,
			  uint16_t blocks,
			  void *buf,
			  size_t len);

#endif /* TEST_UHC_MSC_COMMON_H */
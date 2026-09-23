/*
 * Copyright (c) 2026 Roman Leonov <jam_roma@yahoo.com>
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TEST_UHC_HUB_COMMON_H
#define TEST_UHC_HUB_COMMON_H

#include <stdint.h>

#include <zephyr/drivers/usb/uhc.h>
#include <zephyr/usb/usb_ch9.h>

#include "test_uhc_common.h"

#define TEST_UHC_HUB_CLASS			0x09
#define TEST_UHC_HUB_SUBCLASS			0x00
#define TEST_UHC_HUB_DESC_TYPE			0x29

#define TEST_UHC_HUB_CFG_BUF_SIZE		512U
#define TEST_UHC_HUB_DESC_BUF_SIZE		64U
#define TEST_UHC_HUB_CHANGE_BUF_SIZE		8U

#define TEST_UHC_HUB_PORT_STATUS_CONNECTION	BIT(0)
#define TEST_UHC_HUB_PORT_STATUS_ENABLE		BIT(1)
#define TEST_UHC_HUB_PORT_STATUS_SUSPEND	BIT(2)
#define TEST_UHC_HUB_PORT_STATUS_OVER_CURRENT	BIT(3)
#define TEST_UHC_HUB_PORT_STATUS_RESET		BIT(4)
#define TEST_UHC_HUB_PORT_STATUS_POWER		BIT(8)
#define TEST_UHC_HUB_PORT_STATUS_LOW_SPEED	BIT(9)
#define TEST_UHC_HUB_PORT_STATUS_HIGH_SPEED	BIT(10)

#define TEST_UHC_HUB_PORT_CHANGE_CONNECTION	BIT(0)
#define TEST_UHC_HUB_PORT_CHANGE_ENABLE		BIT(1)
#define TEST_UHC_HUB_PORT_CHANGE_SUSPEND	BIT(2)
#define TEST_UHC_HUB_PORT_CHANGE_OVER_CURRENT	BIT(3)
#define TEST_UHC_HUB_PORT_CHANGE_RESET		BIT(4)

struct test_uhc_hub_descriptor {
	uint8_t bDescLength;
	uint8_t bDescriptorType;
	uint8_t bNbrPorts;
	uint16_t wHubCharacteristics;
	uint8_t bPwrOn2PwrGood;
	uint8_t bHubContrCurrent;
} __packed;

struct test_uhc_hub_port_status {
	uint16_t status;
	uint16_t change;
};

struct test_uhc_hub_ep_intr {
	uint8_t num;
	uint16_t mps;
	uint8_t interval;
	struct usb_ep_descriptor desc;
};

struct test_uhc_hub_info {
	uint8_t iface;
	uint8_t cfg_value;

	struct test_uhc_hub_ep_intr ep_in;
	struct test_uhc_hub_descriptor desc;

	uint8_t change_bitmap[TEST_UHC_HUB_CHANGE_BUF_SIZE];
	size_t change_len;
};

struct test_uhc_hub_change {
	uint8_t port;
	bool connected;
	uint16_t status;
	uint16_t change;
};

void test_uhc_hub_init(struct usb_device *udev,
		       const uint8_t iface_num,
		       struct test_uhc_hub_info *hub);

void test_uhc_hub_get_descriptor(struct usb_device *udev,
				 struct test_uhc_hub_info *hub);

void test_uhc_hub_get_port_status(struct usb_device *udev,
				  uint8_t port,
				  struct test_uhc_hub_port_status *status);

void test_uhc_hub_set_port_feature(struct usb_device *udev,
				   uint8_t port,
				   uint16_t feature);

void test_uhc_hub_clear_port_feature(struct usb_device *udev,
				     uint8_t port,
				     uint16_t feature);

void test_uhc_hub_power_ports(struct usb_device *udev,
			      const struct test_uhc_hub_info *hub);

/*
 * Process the current state of all hub ports synchronously.
 *
 * Initial version only reads and clears change flags.
 * Child enumeration/reset handling can be added here next.
 */
void test_uhc_hub_scan(struct usb_device *udev,
		       struct test_uhc_hub_info *hub);

/*
 * Wait for one hub interrupt-IN transaction and store the returned
 * hub/port change bitmap in hub->change_bitmap.
 */
void test_uhc_hub_interrupt_in(struct usb_device *udev,
			       struct test_uhc_hub_info *hub);

/*
 * Process only ports reported by the last interrupt-IN bitmap.
 */
bool test_uhc_hub_process_change(struct usb_device *udev,
				 struct test_uhc_hub_info *hub,
                                 struct test_uhc_hub_change *result);

#endif /* TEST_UHC_HUB_COMMON_H */
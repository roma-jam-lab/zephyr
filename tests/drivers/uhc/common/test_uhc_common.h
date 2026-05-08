/*
 * Copyright (c) 2026 Roman Leonov <jam_roma@yahoo.com>
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/kernel.h>
#include <zephyr/device.h>

#ifndef TEST_UHC_COMMON_H
#define TEST_UHC_COMMON_H

#define TEST_UHC_MAX_XFER_DATA_BUF_SIZE	 (1024U + 256U) 

void test_uhc_print_current_stack_usage(void);

const struct device *test_uhc_init(void);
const struct device *test_uhc_get_dev(void);
void test_uhc_enable(void);
void test_uhc_wait_connection(enum usb_device_speed *speed);
void test_uhc_wait_ep_request(void);
void test_uhc_wait_removed(void);
void test_uhc_bus_reset(void);
void test_uhc_bus_suspend(void);
void test_uhc_bus_resume(void);
void test_uhc_disable(void);
void test_uhc_disable_wait_removed(void);
void test_uhc_shutdown(void);

void test_uhc_prepare_device(struct usb_device *udev, enum usb_device_speed *speed);
void test_uhc_prepare_addressed_device(struct usb_device *udev,
				    	      enum usb_device_speed *speed, 
					      uint8_t dev_addr);
void test_uhc_device_cleanup(void);


void test_uhc_dev_init(struct usb_device *const udev);
void test_uhc_dev_set_address(struct usb_device *udev, uint8_t addr);
void test_uhc_dev_set_config(struct usb_device *udev, uint8_t cfg);
void test_uhc_dev_get_short_dev_desc(struct usb_device *udev, uint8_t *ep0_mps);
void test_uhc_dev_get_full_dev_desc(struct usb_device *udev, uint8_t ep0_mps);
void test_uhc_dev_get_cfg_desc(struct usb_device *udev, void *buf, size_t len);
void test_uhc_dev_get_string_desc(struct usb_device *udev,
				  uint8_t index,
				  uint16_t lang_id,
				  void *buf,
				  size_t len);
size_t test_uhc_dev_get_string_desc_short_allowed(struct usb_device *udev,
						  uint8_t index,
						  uint16_t lang_id,
						  void *buf,
						  size_t len);

void test_uhc_dev_uvc_get_probe_cur(struct usb_device *udev, uint8_t iface, void *buf, size_t len);
void test_uhc_dev_uvc_set_probe_cur(struct usb_device *udev, uint8_t iface, void *buf, size_t len);

void test_uhc_assign_ep_desc_ptr(struct usb_device *const udev, const uint8_t ep, void *const ptr);

#endif /* TEST_UHC_COMMON_H */

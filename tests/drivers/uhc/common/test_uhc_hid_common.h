#ifndef TEST_UHC_HID_COMMON_H
#define TEST_UHC_HID_COMMON_H

#include <stdint.h>
#include <zephyr/drivers/usb/uhc.h>

#include "test_uhc_common.h"

#define TEST_UHC_HID_CLASS		0x03

struct test_uhc_hid_ep_intr {
	uint8_t num;
	uint16_t mps;
	uint8_t interval;
	/** Pointer to the endpoint descriptor */
	struct usb_ep_descriptor *desc;
};

struct test_uhc_hid_info {
	uint8_t iface;
	uint8_t cfg_value;
	struct test_uhc_hid_ep_intr ep_in;
	uint16_t report_desc_len;
};

void test_uhc_hid_init(struct usb_device *udev,
		       const uint8_t iface_num,
		       struct test_uhc_hid_info *hid);

void test_uhc_hid_interrupt_in_once(struct usb_device *udev,
				    const struct test_uhc_hid_info *hid,
				    void *buf,
				    size_t len);

void test_uhc_hid_interrupt_in_poll_ms(struct usb_device *udev,
				    const struct test_uhc_hid_info *hid,
				    uint32_t msec,
				    void *buf, 
				    size_t len);

void test_uhc_hid_get_report_desc(struct usb_device *udev,
				  const struct test_uhc_hid_info *hid,
				  void *buf,
				  size_t len);

#endif /* TEST_UHC_HID_COMMON_H */
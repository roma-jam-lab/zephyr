/*
 * Copyright (c) 2026 Roman Leonov <jam_roma@yahoo.com>
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef USB_LOOPBACK_H_
#define USB_LOOPBACK_H_

#include <zephyr/usb/usbd.h>

/* Register the loopback class on a USBD configuration at the given speed. */
int loopback_register(struct usbd_context *ctx, enum usbd_speed speed);

#endif /* USB_LOOPBACK_H_ */

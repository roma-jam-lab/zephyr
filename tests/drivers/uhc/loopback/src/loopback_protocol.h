/*
 * Copyright (c) 2026 Roman Leonov <jam_roma@yahoo.com>
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef USB_LOOPBACK_PROTOCOL_H_
#define USB_LOOPBACK_PROTOCOL_H_

#define LOOPBACK_MAX_PAYLOAD            2048U

#define LOOPBACK_BMREQUEST_OUT          0x41
#define LOOPBACK_BMREQUEST_IN           0xC1

#define LOOPBACK_REQ_SET_PAYLOAD        0x01
#define LOOPBACK_REQ_GET_PAYLOAD        0x02

#define LOOPBACK_INTERFACE_NUMBER       0

/* BULK endpoint addresses */
#define LOOPBACK_BULK_OUT_EP            0x01U
#define LOOPBACK_BULK_IN_EP             0x81U

/* Full-speed and high-speed packet sizes */
#define LOOPBACK_FS_BULK_MPS            64U
#define LOOPBACK_HS_BULK_MPS            512U

/*
 * ECHO:
 *   bmRequestType = 0x41 (vendor, host-to-device, interface)
 *   bRequest      = 0x03
 *   wValue        = 0
 *   wIndex        = LOOPBACK_INTERFACE_NUMBER
 *   wLength       = 4
 *
 *   DATA OUT:
 *     bytes 0..1: BULK OUT request length (LE16)
 *     bytes 2..3: BULK IN response length (LE16)
 */
#define LOOPBACK_BULK_REQ_ECHO          0x03

#endif /* USB_LOOPBACK_PROTOCOL_H_ */

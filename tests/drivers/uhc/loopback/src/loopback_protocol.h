/*
 * Copyright (c) 2026 Roman Leonov <jam_roma@yahoo.com>
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef USB_LOOPBACK_PROTOCOL_H_
#define USB_LOOPBACK_PROTOCOL_H_

#define LOOPBACK_REQ_SET_PAYLOAD        0x01
#define LOOPBACK_REQ_GET_PAYLOAD        0x02
#define LOOPBACK_BMREQUEST_OUT          0x41
#define LOOPBACK_BMREQUEST_IN           0xC1
#define LOOPBACK_INTERFACE_NUMBER       0
#define LOOPBACK_MAX_PAYLOAD            256U

#endif /* USB_LOOPBACK_PROTOCOL_H_ */

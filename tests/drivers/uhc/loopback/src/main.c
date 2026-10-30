/*
 * Copyright (c) 2026 Roman Leonov <jam_roma@yahoo.com>
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/usb/usbd.h>

#include "loopback.h"

LOG_MODULE_REGISTER(usb_loopback_main, LOG_LEVEL_INF);

/* Example-only VID/PID: replace before distributing a real USB product. */
USBD_DEVICE_DEFINE(loopback_usbd, DEVICE_DT_GET(DT_NODELABEL(zephyr_udc0)), 0x2fe3, 0x0100);
USBD_DESC_LANG_DEFINE(loopback_lang);
USBD_DESC_MANUFACTURER_DEFINE(loopback_mfr, "Zephyr Test");
USBD_DESC_PRODUCT_DEFINE(loopback_product, "UHC Control Loopback");
USBD_CONFIGURATION_DEFINE(loopback_fs, 0, 50, NULL);
USBD_CONFIGURATION_DEFINE(loopback_hs, 0, 50, NULL);

static int loopback_usbd_start(void)
{
	int ret;

	ret = usbd_add_descriptor(&loopback_usbd, &loopback_lang);
	if (ret) {
		return ret;
	}
	ret = usbd_add_descriptor(&loopback_usbd, &loopback_mfr);
	if (ret) {
		return ret;
	}
	ret = usbd_add_descriptor(&loopback_usbd, &loopback_product);
	if (ret) {
		return ret;
	}

	ret = usbd_add_configuration(&loopback_usbd, USBD_SPEED_FS, &loopback_fs);
	if (ret) {
		return ret;
	}
	ret = loopback_register(&loopback_usbd, USBD_SPEED_FS);
	if (ret) {
		return ret;
	}

	if (USBD_SUPPORTS_HIGH_SPEED &&
	    usbd_caps_speed(&loopback_usbd) == USBD_SPEED_HS) {
		ret = usbd_add_configuration(&loopback_usbd, USBD_SPEED_HS,
					     &loopback_hs);
		if (ret) {
			return ret;
		}
		ret = loopback_register(&loopback_usbd, USBD_SPEED_HS);
		if (ret) {
			return ret;
		}
	}

	ret = usbd_device_set_code_triple(&loopback_usbd, USBD_SPEED_FS,
					  0, 0, 0);
	if (ret) {
		return ret;
	}

	ret = usbd_init(&loopback_usbd);
	if (ret) {
		return ret;
	}

	return usbd_enable(&loopback_usbd);
}

int main(void)
{
	int ret = loopback_usbd_start();

	if (ret) {
		LOG_ERR("USB loopback initialization failed (%d)", ret);
		return 0;
	}

	LOG_INF("USB_LOOPBACK_READY");
	return 0;
}

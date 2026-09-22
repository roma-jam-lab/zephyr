#include <string.h>

#include <zephyr/ztest.h>
#include <zephyr/drivers/usb/uhc.h>
#include <zephyr/sys/byteorder.h>

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(test_uhc_hub, LOG_LEVEL_INF);

#include "test_uhc_common.h"
#include "test_uhc_hub_common.h"

#define TEST_UHC_HUB_ADDR	1
#define TEST_UHC_HUB_IFACE	0

#if (0)
ZTEST(test_uhc_hub, test_probe_interface)
{
	struct usb_device udev;
	struct test_uhc_hub_info hub;
	enum usb_device_speed speed;

	test_uhc_init();

	test_uhc_prepare_addressed_device(&udev,
					 &speed,
					 TEST_UHC_HUB_ADDR);

	test_uhc_hub_init(&udev,
			  TEST_UHC_HUB_IFACE,
			  &hub);

	zassert_not_equal(hub.ep_in.num, 0,
			  "HUB interrupt IN endpoint not found");

	zassert_not_equal(hub.ep_in.mps, 0,
			  "HUB interrupt IN MPS is zero");

	zassert_not_equal(hub.ep_in.interval, 0,
			  "HUB interrupt IN interval is zero");

	zassert_not_equal(hub.desc.bNbrPorts, 0,
			  "HUB reports zero downstream ports");

	LOG_INF("HUB iface=%u cfg=%u ports=%u int_in=0x%02x "
		"mps=%u interval=%u",
		hub.iface,
		hub.cfg_value,
		hub.desc.bNbrPorts,
		hub.ep_in.num,
		hub.ep_in.mps,
		hub.ep_in.interval);

	test_uhc_device_cleanup();
}
#endif

ZTEST(test_uhc_hub, test_interrupt_in_connect_disconnect)
{
	struct usb_device udev;
	struct test_uhc_hub_info hub;
	struct test_uhc_hub_change change;
	enum usb_device_speed speed;

	test_uhc_init();

	test_uhc_prepare_addressed_device(&udev,
					 &speed,
					 TEST_UHC_HUB_ADDR);

	test_uhc_hub_init(&udev,
			  TEST_UHC_HUB_IFACE,
			  &hub);

	test_uhc_hub_power_ports(&udev, &hub);

	/* Clear any state caused by devices already present. */
	test_uhc_hub_scan(&udev, &hub);

	/*
	 * CONNECT
	 *
	 * Start the transfer, then physically connect the test device.
	 */
        LOG_INF("Connect device to Hub port");
	test_uhc_hub_interrupt_in(&udev, &hub);

	zassert_true(test_uhc_hub_process_change(&udev,
						 &hub,
						 &change),
		     "No HUB port change found");

	zassert_equal(change.port, 2,
		      "Unexpected HUB port: %u",
		      change.port);

	zassert_true(change.change &
		     TEST_UHC_HUB_PORT_CHANGE_CONNECTION,
		     "Expected connection-change bit");

	zassert_true(change.connected,
		     "Expected device to be connected");

	/*
	 * DISCONNECT
	 *
	 * Re-arm interrupt transfer, then physically disconnect the device.
	 */
        memset(&change, 0, sizeof(change));
        
        LOG_INF("Disconnect device from Hub port");
	test_uhc_hub_interrupt_in(&udev, &hub);

	zassert_true(test_uhc_hub_process_change(&udev,
						 &hub,
						 &change),
		     "No HUB port change found");

	zassert_equal(change.port, 2,
		      "Unexpected HUB port: %u",
		      change.port);

	zassert_true(change.change &
		     TEST_UHC_HUB_PORT_CHANGE_CONNECTION,
		     "Expected connection-change bit");

	zassert_false(change.connected,
		      "Expected device to be disconnected");

	test_uhc_device_cleanup();
}

#if (0)
ZTEST(test_uhc_hub, test_interrupt_in)
{
	struct usb_device udev;
	struct test_uhc_hub_info hub;
	enum usb_device_speed speed;

	test_uhc_init();

	test_uhc_prepare_addressed_device(&udev,
					 &speed,
					 TEST_UHC_HUB_ADDR);

	test_uhc_hub_init(&udev,
			  TEST_UHC_HUB_IFACE,
			  &hub);

        test_uhc_hub_power_ports(&udev, &hub);
                          
	/*
	 * Process/clear the state which already exists before
	 * testing the interrupt endpoint.
	 *
	 * With the current test hardware, downstream hubs are
	 * already connected when the root external hub is enumerated.
	 */
	test_uhc_hub_scan(&udev, &hub);

	/*
	 * Now wait for one interrupt-IN change indication.
	 *
	 * Extend this point with:
	 *   - connect downstream device
	 *   - disconnect downstream device
	 *   - verify expected port bit
	 */
	test_uhc_hub_interrupt_in(&udev, &hub);

	zassert_not_equal(hub.change_len, 0,
			  "Expected HUB interrupt change data");

	LOG_HEXDUMP_INF(hub.change_bitmap,
			hub.change_len,
			"HUB interrupt change bitmap");

	test_uhc_hub_process_change(&udev, &hub);

	test_uhc_device_cleanup();
}


ZTEST(test_uhc_hub, test_interrupt_enqueue_dequeue)
{
	struct usb_device udev;
	struct test_uhc_hub_info hub;
	enum usb_device_speed speed;
	const struct device *uhc_dev;
	struct uhc_transfer *xfer;
	int ret;

	uhc_dev = test_uhc_init();

	test_uhc_prepare_addressed_device(&udev,
					 &speed,
					 TEST_UHC_HUB_ADDR);

	test_uhc_hub_init(&udev,
			  TEST_UHC_HUB_IFACE,
			  &hub);

	test_uhc_hub_scan(&udev, &hub);

	xfer = uhc_xfer_alloc_with_buf(uhc_dev,
				       hub.ep_in.num,
				       &udev,
				       NULL,
				       NULL,
				       hub.ep_in.mps);
	zassert_not_null(xfer,
			 "Failed to allocate HUB interrupt IN transfer");

	ret = uhc_ep_enqueue(uhc_dev, xfer);
	zassert_equal(ret, 0,
		      "HUB interrupt IN enqueue failed: %d",
		      ret);

	ret = uhc_ep_dequeue(uhc_dev, xfer);
	zassert_equal(ret, 0,
		      "HUB interrupt IN dequeue failed: %d",
		      ret);

	test_uhc_wait_ep_request();

	zassert_equal(xfer->err, -ECONNRESET,
		      "Expected -ECONNRESET, got %d",
		      xfer->err);

	uhc_xfer_free(uhc_dev, xfer);

	test_uhc_device_cleanup();
}
#endif

ZTEST_SUITE(test_uhc_hub, NULL, NULL, NULL, NULL, NULL);
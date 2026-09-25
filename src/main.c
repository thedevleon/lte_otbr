/*
 * Copyright 2026
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/kernel.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/logging/log.h>
#include <zephyr/net/net_event.h>
#include <zephyr/net/net_if.h>
#include <zephyr/net/net_mgmt.h>
#include <zephyr/net/ppp.h>

LOG_MODULE_REGISTER(main, LOG_LEVEL_INF);

static const struct gpio_dt_spec led = GPIO_DT_SPEC_GET(DT_ALIAS(led2), gpios);

static struct net_mgmt_event_callback net_cb;

static const char *iface_l2_name(struct net_if *iface)
{
	if (net_if_l2(iface) == &NET_L2_GET_NAME(PPP)) {
		return "PPP";
	}

	return "other";
}

static void net_event_handler(struct net_mgmt_event_callback *cb, uint64_t mgmt_event,
			      struct net_if *iface)
{
	ARG_UNUSED(cb);

	switch (mgmt_event) {
	case NET_EVENT_IF_UP:
		LOG_INF("iface %d (%s) UP", net_if_get_by_iface(iface), iface_l2_name(iface));
		break;
	case NET_EVENT_IF_DOWN:
		LOG_INF("iface %d (%s) DOWN", net_if_get_by_iface(iface), iface_l2_name(iface));
		break;
	case NET_EVENT_IPV4_ADDR_ADD:
		LOG_INF("iface %d (%s) IPv4 address added", net_if_get_by_iface(iface),
			iface_l2_name(iface));
		break;
	case NET_EVENT_L4_CONNECTED:
		LOG_INF("L4 connected");
		break;
	case NET_EVENT_L4_DISCONNECTED:
		LOG_INF("L4 disconnected");
		break;
	default:
		break;
	}
}

int main(void)
{
	struct net_if *ppp_iface;

	if (gpio_is_ready_dt(&led)) {
		(void)gpio_pin_configure_dt(&led, GPIO_OUTPUT_INACTIVE);
	}

	net_mgmt_init_event_callback(&net_cb, net_event_handler,
				     NET_EVENT_IF_UP | NET_EVENT_IF_DOWN |
				     NET_EVENT_IPV4_ADDR_ADD |
				     NET_EVENT_L4_CONNECTED | NET_EVENT_L4_DISCONNECTED);
	net_mgmt_add_event_callback(&net_cb);

	/* Do not use net_if_get_by_index() here: the OpenThread interface shifts indices. */
	ppp_iface = net_if_get_first_by_type(&NET_L2_GET_NAME(PPP));
	if (ppp_iface == NULL) {
		LOG_ERR("PPP interface not found");
	} else if (!net_if_is_up(ppp_iface)) {
		LOG_INF("Bringing up PPP interface %d", net_if_get_by_iface(ppp_iface));
		(void)net_if_up(ppp_iface);
	}

	while (true) {
		if (gpio_is_ready_dt(&led)) {
			(void)gpio_pin_toggle_dt(&led);
		}
		k_sleep(K_SECONDS(1));
	}

	return 0;
}

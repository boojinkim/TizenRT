/****************************************************************************
 *
 * Copyright 2016 Samsung Electronics All Rights Reserved.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing,
 * software distributed under the License is distributed on an
 * "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND,
 * either express or implied. See the License for the specific
 * language governing permissions and limitations under the License.
 *
 ****************************************************************************/

/****************************************************************************
 * net/netdev/netdev_findbyaddr.c
 *
 *   Copyright (C) 2007-2009, 2014-2015 Gregory Nutt. All rights reserved.
 *   Author: Gregory Nutt <gnutt@nuttx.org>
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 * 3. Neither the name NuttX nor the names of its contributors may be
 *    used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
 * COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS
 * OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED
 * AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN
 * ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 *
 ****************************************************************************/

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <tinyara/config.h>
#if defined(CONFIG_NET) && CONFIG_NSOCKET_DESCRIPTORS > 0

#include <stdbool.h>
#include <string.h>
#include <errno.h>
#include <debug.h>
#include <tinyara/net/ip.h>

#include <net/lwip/netif.h>
#include "netdev/netdev.h"
#include "route/route.h"

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

/****************************************************************************
 * Private Types
 ****************************************************************************/

/****************************************************************************
 * Private Data
 ****************************************************************************/

/****************************************************************************
 * Public Data
 ****************************************************************************/

/****************************************************************************
 * Private Functions
 ****************************************************************************/

/****************************************************************************
 * Function: netdev_finddevice_ipv4addr
 *
 * Description:
 *   Find a previously registered network device by matching a local address
 *   with the subnet served by the device.  Only "up" devices are considered
 *   (since a "down" device has no meaningful address).
 *
 * Parameters:
 *   ripaddr - Remote address of a connection to use in the lookup
 *
 * Returned Value:
 *  Pointer to driver on success; null on failure
 *
 * Assumptions:
 *  Called from normal user mode
 *
 ****************************************************************************/

#ifdef CONFIG_NET_IPv4
static FAR struct netif *netdev_finddevice_ipv4addr(in_addr_t ripaddr)
{
	FAR struct netif *dev;

	/* Examine each registered network device */

	netdev_semtake();
	for (dev = g_netdevices; dev; dev = dev->flink) {
		/* Is the interface in the "up" state? */

		if ((dev->d_flags & IFF_UP) != 0) {
			/* Yes.. check for an address match (under the netmask) */

			if (net_ipv4addr_maskcmp(dev->d_ipaddr, ripaddr, dev->d_netmask)) {
				/* Its a match */

				netdev_semgive();
				return dev;
			}
		}
	}

	/* No device with the matching address found */

	netdev_semgive();
	return NULL;
}
#endif							/* CONFIG_NET_IPv4 */

/****************************************************************************
 * Function: netdev_finddevice_ipv6addr
 *
 * Description:
 *   Find a previously registered network device by matching a local address
 *   with the subnet served by the device.  Only "up" devices are considered
 *   (since a "down" device has no meaningful address).
 *
 * Parameters:
 *   ripaddr - Remote address of a connection to use in the lookup
 *
 * Returned Value:
 *  Pointer to driver on success; null on failure
 *
 * Assumptions:
 *  Called from normal user mode
 *
 ****************************************************************************/

#ifdef CONFIG_NET_IPv6
static FAR struct netif *netdev_finddevice_ipv6addr(const net_ipv6addr_t ripaddr)
{
	FAR struct netif *dev;

	/* Examine each registered network device */

	netdev_semtake();
	for (dev = g_netdevices; dev; dev = dev->flink) {
		/* Is the interface in the "up" state? */

		if ((dev->d_flags & IFF_UP) != 0) {
			/* Yes.. check for an address match (under the netmask) */

			if (net_ipv6addr_maskcmp(dev->d_ipv6addr, ripaddr, dev->d_ipv6netmask)) {
				/* Its a match */

				netdev_semgive();
				return dev;
			}
		}
	}

	/* No device with the matching address found */

	netdev_semgive();
	return NULL;
}
#endif							/* CONFIG_NET_IPv6 */

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Function: netdev_findby_ipv4addr
 *
 * Description:
 *   Find a previously registered network device by matching an arbitrary
 *   IPv4 address.
 *
 * Parameters:
 *   lipaddr - Local, bound address of a connection.  Used only if ripaddr
 *     is the broadcast address.  Used only if CONFIG_NETDEV_MULTINIC.
 *   ripaddr - Remote address of a connection to use in the lookup
 *
 * Returned Value:
 *  Pointer to driver on success; null on failure
 *
 * Assumptions:
 *  Called from normal user mode
 *
 ****************************************************************************/

#ifdef CONFIG_NET_IPv4
#ifdef CONFIG_NETDEV_MULTINIC
FAR struct netif *netdev_findby_ipv4addr(in_addr_t lipaddr, in_addr_t ripaddr)
#else
FAR struct netif *netdev_findby_ipv4addr(in_addr_t ripaddr)
#endif
{
	struct netif *dev;
#ifdef CONFIG_NET_ROUTE
	in_addr_t router;
	int ret;
#endif

	/* First, check if this is the broadcast IP address */

	if (net_ipv4addr_cmp(ripaddr, g_ipv4_alloneaddr)) {
#ifdef CONFIG_NETDEV_MULTINIC
		/* Yes.. Check the local, bound address.  Is it INADDR_ANY? */

		if (net_ipv4addr_cmp(lipaddr, g_ipv4_allzeroaddr)) {
			/* Yes.. In this case, I think we are supposed to send the
			 * broadcast packet out ALL local networks.  I am not sure
			 * of that and, in any event, there is nothing we can do
			 * about that here.
			 */

			return NULL;
		} else {
			/* Return the device associated with the local address */

			return netdev_finddevice_ipv4addr(lipaddr);
		}
#else
		/* If there is only a single, registered network interface, then the
		 * decision is pretty easy.
		 */

		return g_netdevices;
#endif
	}

	/* Check if the address maps to a local network */

	dev = netdev_finddevice_ipv4addr(ripaddr);
	if (dev) {
		return dev;
	}

	/* No.. The address lies on an external network */

#ifdef CONFIG_NET_ROUTE
	/* If we have a routing table, then perhaps we can find the local
	 * address of a router that can forward packets to the external network.
	 */

	ret = net_ipv4_router(ripaddr, &router);
	if (ret >= 0) {
		/* Success... try to find the network device associated with the local
		 * router address
		 */

		dev = netdev_finddevice_ipv4addr(router);
		if (dev) {
			return dev;
		}
	}
#endif							/* CONFIG_NET_ROUTE */

	/* The above lookup will fail if the packet is being sent out of our
	 * out subnet to a router and there is no routing information.
	 */

#ifndef CONFIG_NETDEV_MULTINIC
	/* If there is only a single, registered network interface, then the
	 * decision is pretty easy.  Use that device and its default router
	 * address.
	 */

	dev = g_netdevices;
#endif

	/* If we will did not find the network device, then we might as well fail
	 * because we are not configured properly to determine the route to the
	 * destination.
	 */

	return dev;
}
#endif							/* CONFIG_NET_IPv4 */

/****************************************************************************
 * Function: netdev_findby_ipv6addr
 *
 * Description:
 *   Find a previously registered network device by matching an arbitrary
 *   IPv6 address.
 *
 * Parameters:
 *   lipaddr - Local, bound address of a connection.  Used only if ripaddr
 *     is the broadcast address.  Used only if CONFIG_NETDEV_MULTINIC.
 *   ripaddr - Remote address of a connection to use in the lookup
 *
 * Returned Value:
 *  Pointer to driver on success; null on failure
 *
 * Assumptions:
 *  Called from normal user mode
 *
 ****************************************************************************/

#ifdef CONFIG_NET_IPv6
#ifdef CONFIG_NETDEV_MULTINIC
FAR struct netif *netdev_findby_ipv6addr(const net_ipv6addr_t lipaddr, const net_ipv6addr_t ripaddr)
#else
FAR struct netif *netdev_findby_ipv6addr(const net_ipv6addr_t ripaddr)
#endif
{
	struct netif *dev;
#ifdef CONFIG_NET_ROUTE
	net_ipv6addr_t router;
	int ret;
#endif

	/* First, check if this is the broadcast IP address */

	if (net_ipv6addr_cmp(ripaddr, g_ipv6_alloneaddr)) {
#ifdef CONFIG_NETDEV_MULTINIC
		/* Yes.. Check the local, bound address.  Is it INADDR_ANY? */

		if (net_ipv6addr_cmp(lipaddr, g_ipv6_allzeroaddr)) {
			/* Yes.. In this case, I think we are supposed to send the
			 * broadcast packet out ALL local networks.  I am not sure
			 * of that and, in any event, there is nothing we can do
			 * about that here.
			 */

			return NULL;
		} else {
			/* Return the device associated with the local address */

			return netdev_finddevice_ipv6addr(lipaddr);
		}
#else
		/* If there is only a single, registered network interface, then the
		 * decision is pretty easy.
		 */

		return g_netdevices;
#endif
	}

	/* Check if the address maps to a local network */

	dev = netdev_finddevice_ipv6addr(ripaddr);
	if (dev) {
		return dev;
	}

	/* No.. The address lies on an external network */

#ifdef CONFIG_NET_ROUTE
	/* If we have a routing table, then perhaps we can find the local
	 * address of a router that can forward packets to the external network.
	 */

	ret = net_ipv6_router(ripaddr, router);
	if (ret >= 0) {
		/* Success... try to find the network device associated with the local
		 * router address
		 */

		dev = netdev_finddevice_ipv6addr(router);
		if (dev) {
			return dev;
		}
	}
#endif							/* CONFIG_NET_ROUTE */

	/* The above lookup will fail if the packet is being sent out of our
	 * out subnet to a router and there is no routing information.
	 */

#ifndef CONFIG_NETDEV_MULTINIC
	/* If there is only a single, registered network interface, then the
	 * decision is pretty easy.  Use that device and its default router
	 * address.
	 */

	dev = g_netdevices;
#endif

	/* If we will did not find the network device, then we might as well fail
	 * because we are not configured properly to determine the route to the
	 * destination.
	 */

	return dev;
}
#endif							/* CONFIG_NET_IPv6 */

#endif							/* CONFIG_NET && CONFIG_NSOCKET_DESCRIPTORS */

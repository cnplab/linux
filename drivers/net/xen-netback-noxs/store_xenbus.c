/*
 * io_xenbus.c
 *
 *  Created on: Sep 23, 2016
 *      Author: wolf
 */

#include "common.h"
#include "store.h"


int store_write_netback_probe_info(struct xenbus_device *dev, const struct xenbus_device_id *id)
{
	const char *message;
	struct xenbus_transaction xbt;
	int err;
	int sg;
	const char *script;

	sg = 1;

	do {
		err = xenbus_transaction_start(&xbt);
		if (err) {
			xenbus_dev_fatal(dev, err, "starting transaction");
			goto fail;
		}

		err = xenbus_printf(xbt, dev->nodename, "feature-sg", "%d", sg);
		if (err) {
			message = "writing feature-sg";
			goto abort_transaction;
		}

		err = xenbus_printf(xbt, dev->nodename, "feature-gso-tcpv4",
				    "%d", sg);
		if (err) {
			message = "writing feature-gso-tcpv4";
			goto abort_transaction;
		}

		err = xenbus_printf(xbt, dev->nodename, "feature-gso-tcpv6",
				    "%d", sg);
		if (err) {
			message = "writing feature-gso-tcpv6";
			goto abort_transaction;
		}

		/* We support partial checksum setup for IPv6 packets */
		err = xenbus_printf(xbt, dev->nodename,
				    "feature-ipv6-csum-offload",
				    "%d", 1);
		if (err) {
			message = "writing feature-ipv6-csum-offload";
			goto abort_transaction;
		}

		/* We support rx-copy path. */
		err = xenbus_printf(xbt, dev->nodename,
				    "feature-rx-copy", "%d", 1);
		if (err) {
			message = "writing feature-rx-copy";
			goto abort_transaction;
		}

		/*
		 * We don't support rx-flip path (except old guests who don't
		 * grok this feature flag).
		 */
		err = xenbus_printf(xbt, dev->nodename,
				    "feature-rx-flip", "%d", 0);
		if (err) {
			message = "writing feature-rx-flip";
			goto abort_transaction;
		}

		/* We support dynamic multicast-control. */
		err = xenbus_printf(xbt, dev->nodename,
				    "feature-multicast-control", "%d", 1);
		if (err) {
			message = "writing feature-multicast-control";
			goto abort_transaction;
		}

		err = xenbus_printf(xbt, dev->nodename,
				    "feature-dynamic-multicast-control",
				    "%d", 1);
		if (err) {
			message = "writing feature-dynamic-multicast-control";
			goto abort_transaction;
		}

		err = xenbus_transaction_end(xbt, 0);
	} while (err == -EAGAIN);

	if (err) {
		xenbus_dev_fatal(dev, err, "completing transaction");
		goto fail;
	}

	/*
	 * Split event channels support, this is optional so it is not
	 * put inside the above loop.
	 */
	err = xenbus_printf(XBT_NIL, dev->nodename,
			    "feature-split-event-channels",
			    "%u", separate_tx_rx_irq);
	if (err)
		pr_debug("Error writing feature-split-event-channels\n");

	/* Multi-queue support: This is an optional feature. */
	err = xenbus_printf(XBT_NIL, dev->nodename,
			    "multi-queue-max-queues", "%u", xenvif_max_queues);
	if (err)
		pr_debug("Error writing multi-queue-max-queues\n");

	err = xenbus_printf(XBT_NIL, dev->nodename,
			    "feature-ctrl-ring",
			    "%u", true);
	if (err)
		pr_debug("Error writing feature-ctrl-ring\n");

	script = xenbus_read(XBT_NIL, dev->nodename, "script", NULL);
	if (IS_ERR(script)) {
		err = PTR_ERR(script);
		xenbus_dev_fatal(dev, err, "reading script");
		goto fail;
	}

	return 0;

abort_transaction:
	xenbus_transaction_end(xbt, 1);
	xenbus_dev_fatal(dev, err, "%s", message);
fail:
	return err;
}

int store_read_handle(struct xenbus_device *xdev, long *handle)
{
	return xenbus_scanf(XBT_NIL, xdev->nodename, "handle", "%li", handle);
}

void store_read_rate(struct xenbus_device *dev, unsigned long *bytes, unsigned long *usec)
{
	char *s, *e;
	unsigned long b, u;
	char *ratestr;

	/* Default to unlimited bandwidth. */
	*bytes = ~0UL;
	*usec = 0;

	ratestr = xenbus_read(XBT_NIL, dev->nodename, "rate", NULL);
	if (IS_ERR(ratestr))
		return;

	s = ratestr;
	b = simple_strtoul(s, &e, 10);
	if ((s == e) || (*e != ','))
		goto fail;

	s = e + 1;
	u = simple_strtoul(s, &e, 10);
	if ((s == e) || (*e != '\0'))
		goto fail;

	*bytes = b;
	*usec = u;

	kfree(ratestr);
	return;

 fail:
	pr_warn("Failed to parse network rate limit. Traffic unlimited.\n");
	kfree(ratestr);
}

int store_read_mac(struct xenbus_device *dev, u8 mac[])
{
	char *s, *e, *macstr;
	int i;

	macstr = s = xenbus_read(XBT_NIL, dev->nodename, "mac", NULL);
	if (IS_ERR(macstr))
		return PTR_ERR(macstr);

	for (i = 0; i < ETH_ALEN; i++) {
		mac[i] = simple_strtoul(s, &e, 16);
		if ((s == e) || (*e != ((i == ETH_ALEN-1) ? '\0' : ':'))) {
			kfree(macstr);
			return -ENOENT;
		}
		s = e+1;
	}

	kfree(macstr);
	return 0;
}

int store_read_ctrl_ring_info(struct xenbus_device *dev, grant_ref_t *ring_ref, unsigned int *evtchn)
{
	unsigned int val;
	int err;

	err = xenbus_gather(XBT_NIL, dev->otherend,
			    "ctrl-ring-ref", "%u", &val, NULL);
	if (err) {
		*ring_ref = INVALID_GRANT_HANDLE;
		goto done; /* The frontend does not have a control ring */
	}

	*ring_ref = val;

	err = xenbus_gather(XBT_NIL, dev->otherend,
			    "event-channel-ctrl", "%u", &val, NULL);
	if (err) {
		xenbus_dev_fatal(dev, err,
				 "reading %s/event-channel-ctrl",
				 dev->otherend);
		goto fail;
	}

	*evtchn = val;

done:
	return 0;

fail:
	return err;
}

int store_read_num_queues(struct xenbus_device *dev, unsigned int *requested_num_queues)
{
	return xenbus_scanf(XBT_NIL, dev->otherend,
			   "multi-queue-num-queues",
			   "%u", requested_num_queues);
}


int store_read_data_rings_info(struct xenbus_device *dev, struct xenvif_queue *queue,
		unsigned long *tx_ring_ref, unsigned long *rx_ring_ref,
		unsigned int *tx_evtchn, unsigned int *rx_evtchn)
{
	unsigned int num_queues = queue->vif->num_queues;
	int err;
	char *xspath;
	size_t xspathsize;
	const size_t xenstore_path_ext_size = 11; /* sufficient for "/queue-NNN" */

	/* If the frontend requested 1 queue, or we have fallen back
	 * to single queue due to lack of frontend support for multi-
	 * queue, expect the remaining XenStore keys in the toplevel
	 * directory. Otherwise, expect them in a subdirectory called
	 * queue-N.
	 */
	if (num_queues == 1) {
		xspath = kzalloc(strlen(dev->otherend) + 1, GFP_KERNEL);
		if (!xspath) {
			xenbus_dev_fatal(dev, -ENOMEM,
					 "reading ring references");
			return -ENOMEM;
		}
		strcpy(xspath, dev->otherend);
	} else {
		xspathsize = strlen(dev->otherend) + xenstore_path_ext_size;
		xspath = kzalloc(xspathsize, GFP_KERNEL);
		if (!xspath) {
			xenbus_dev_fatal(dev, -ENOMEM,
					 "reading ring references");
			return -ENOMEM;
		}
		snprintf(xspath, xspathsize, "%s/queue-%u", dev->otherend,
			 queue->id);
	}

	err = xenbus_gather(XBT_NIL, xspath,
			    "tx-ring-ref", "%lu", tx_ring_ref,
			    "rx-ring-ref", "%lu", rx_ring_ref, NULL);
	if (err) {
		xenbus_dev_fatal(dev, err,
				 "reading %s/ring-ref",
				 xspath);
		goto err;
	}

	/* Try split event channels first, then single event channel. */
	err = xenbus_gather(XBT_NIL, xspath,
			    "event-channel-tx", "%u", tx_evtchn,
			    "event-channel-rx", "%u", rx_evtchn, NULL);
	if (err < 0) {
		err = xenbus_scanf(XBT_NIL, xspath,
				   "event-channel", "%u", tx_evtchn);
		if (err < 0) {
			xenbus_dev_fatal(dev, err,
					 "reading %s/event-channel(-tx/rx)",
					 xspath);
			goto err;
		}
		rx_evtchn = tx_evtchn;
	}

	err = 0;
err: /* Regular return falls through with err == 0 */
	kfree(xspath);
	return err;
}

int store_read_vif_flags(struct xenbus_device *dev, struct xenvif *vif)
{
	unsigned int rx_copy;
	int err, val;

	err = xenbus_scanf(XBT_NIL, dev->otherend, "request-rx-copy", "%u",
			   &rx_copy);
	if (err == -ENOENT) {
		err = 0;
		rx_copy = 0;
	}
	if (err < 0) {
		xenbus_dev_fatal(dev, err, "reading %s/request-rx-copy",
				 dev->otherend);
		return err;
	}
	if (!rx_copy)
		return -EOPNOTSUPP;

	if (xenbus_scanf(XBT_NIL, dev->otherend,
			 "feature-rx-notify", "%d", &val) < 0)
		val = 0;
	if (!val) {
		/* - Reduce drain timeout to poll more frequently for
		 *   Rx requests.
		 * - Disable Rx stall detection.
		 */
		vif->drain_timeout = msecs_to_jiffies(30);
		vif->stall_timeout = 0;
	}

	if (xenbus_scanf(XBT_NIL, dev->otherend, "feature-sg",
			 "%d", &val) < 0)
		val = 0;
	vif->can_sg = !!val;

	vif->gso_mask = 0;
	vif->gso_prefix_mask = 0;

	if (xenbus_scanf(XBT_NIL, dev->otherend, "feature-gso-tcpv4",
			 "%d", &val) < 0)
		val = 0;
	if (val)
		vif->gso_mask |= GSO_BIT(TCPV4);

	if (xenbus_scanf(XBT_NIL, dev->otherend, "feature-gso-tcpv4-prefix",
			 "%d", &val) < 0)
		val = 0;
	if (val)
		vif->gso_prefix_mask |= GSO_BIT(TCPV4);

	if (xenbus_scanf(XBT_NIL, dev->otherend, "feature-gso-tcpv6",
			 "%d", &val) < 0)
		val = 0;
	if (val)
		vif->gso_mask |= GSO_BIT(TCPV6);

	if (xenbus_scanf(XBT_NIL, dev->otherend, "feature-gso-tcpv6-prefix",
			 "%d", &val) < 0)
		val = 0;
	if (val)
		vif->gso_prefix_mask |= GSO_BIT(TCPV6);

	if (vif->gso_mask & vif->gso_prefix_mask) {
		xenbus_dev_fatal(dev, err,
				 "%s: gso and gso prefix flags are not "
				 "mutually exclusive",
				 dev->otherend);
		return -EOPNOTSUPP;
	}

	if (xenbus_scanf(XBT_NIL, dev->otherend, "feature-no-csum-offload",
			 "%d", &val) < 0)
		val = 0;
	vif->ip_csum = !val;

	if (xenbus_scanf(XBT_NIL, dev->otherend, "feature-ipv6-csum-offload",
			 "%d", &val) < 0)
		val = 0;
	vif->ipv6_csum = !!val;

	return 0;
}

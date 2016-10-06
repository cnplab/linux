/*
 * io_noxs.c
 *
 *  Created on: Sep 23, 2016
 *      Author: wolf
 */

#include "common.h"
#include "store.h"


int store_write_netback_probe_info(struct xenbus_device *xdev, const struct xenbus_device_id *id)
{
#if 0
	sg = 1;
		err = xenbus_printf(xbt, xdev->nodename, "feature-sg", "%d", sg);
		err = xenbus_printf(xbt, xdev->nodename, "feature-gso-tcpv4", "%d", sg);
		err = xenbus_printf(xbt, xdev->nodename, "feature-gso-tcpv6", "%d", sg);
		/* We support partial checksum setup for IPv6 packets */
		err = xenbus_printf(xbt, xdev->nodename, "feature-ipv6-csum-offload", "%d", 1);
		/* We support rx-copy path. */
		err = xenbus_printf(xbt, xdev->nodename, "feature-rx-copy", "%d", 1);
		/* We don't support rx-flip path (except old guests who don't grok this feature flag). */
		err = xenbus_printf(xbt, xdev->nodename, "feature-rx-flip", "%d", 0);
		/* We support dynamic multicast-control. */
		err = xenbus_printf(xbt, xdev->nodename, "feature-multicast-control", "%d", 1);
		err = xenbus_printf(xbt, xdev->nodename, "feature-dynamic-multicast-control", "%d", 1);

	/* Split event channels support, this is optional so it is not put inside the above loop. */
	err = xenbus_printf(XBT_NIL, xdev->nodename, "feature-split-event-channels", "%u", separate_tx_rx_irq);
	/* Multi-queue support: This is an optional feature. */
	err = xenbus_printf(XBT_NIL, xdev->nodename, "multi-queue-max-queues", "%u", xenvif_max_queues);
	err = xenbus_printf(XBT_NIL, xdev->nodename, "feature-ctrl-ring", "%u", true);
	script = xenbus_read(XBT_NIL, xdev->nodename, "script", NULL);
#endif
	noxs_vif_ctrl_page_t *page = xdev->ctrl_page;
	noxs_cfg_vif_t *cfg = xdev->dev_cfg;

	page->hdr.domid    = xdev->otherend_id;
	page->hdr.evtchn   = xdev->remote_port;
	page->hdr.be_state = XenbusStateUnknown;
	page->hdr.fe_state = XenbusStateUnknown;
	page->vifid    = xdev->id; //TODO vc->vif;

	memset(&page->feature, 0, sizeof(page->feature));//TODO needed?
	page->feature.sg = 1;
	page->feature.gso_tcpv4 = 1;
	page->feature.gso_tcpv6 = 1;
	page->feature.ipv6_csum_offload = 1;
	page->feature.rx_copy = 1;
	page->feature.rx_flip = 1;
	page->feature.multicast_control = 1;
	page->feature.dynamic_multicast_control = 1;

	page->feature.split_event_channels = 1;//TODO separate_tx_rx_irq
	page->feature.ctrl_ring = 1;

	page->multi_queue_max_queues = xenvif_max_queues;
	/* at least one, if more then overwritten by frontend: */
	page->multi_queue_num_queues = 1;

	page->tx_ring_ref = INVALID_GRANT_HANDLE;
	page->rx_ring_ref = INVALID_GRANT_HANDLE;
	page->ctrl_ring_ref = INVALID_GRANT_HANDLE;

	/* config */
	page->ip = cfg->ip;
	memcpy(page->mac, cfg->mac, sizeof(page->mac));
	/* TODO script?? */

	return 0;
}

int store_read_handle(struct xenbus_device *xdev, long *handle)
{
	*handle = xdev->id;
	return 1;
}

void store_read_rate(struct xenbus_device *dev, unsigned long *bytes, unsigned long *usec)
{
	/* Default to unlimited bandwidth. */
	*bytes = ~0UL;
	*usec = 0;
}

int store_read_mac(struct xenbus_device *dev, u8 mac[])
{
	struct noxs_vif_ctrl_page *vif_page = dev->ctrl_page;

	memcpy(mac, vif_page->mac, ETH_ALEN);
	return 0;
}


int store_read_ctrl_ring_info(struct xenbus_device *xdev, grant_ref_t *ring_ref, unsigned int *evtchn)
{
#if 0
	struct xenbus_device *dev = be_to_device(be);
	unsigned int val;
	int err;

	err = xenbus_gather(XBT_NIL, dev->otherend,
			    "ctrl-ring-ref", "%u", &val, NULL);
	if (err)
		goto done; /* The frontend does not have a control ring */

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
#endif
	struct noxs_vif_ctrl_page* page = xdev->ctrl_page;

	if (page->feature.ctrl_ring) {
		*ring_ref = page->ctrl_ring_ref;
		*evtchn = page->event_channel_ctrl;
	} else {
		*ring_ref = INVALID_GRANT_HANDLE;
		//TODO evtchn?
	}

	return 0;
}

int store_read_num_queues(struct xenbus_device *xdev, unsigned int *requested_num_queues)
{
#if 0
	struct xenbus_device *dev = be_to_device(be);

	return xenbus_scanf(XBT_NIL, dev->otherend,
			   "multi-queue-num-queues",
			   "%u", &requested_num_queues);
#endif
	struct noxs_vif_ctrl_page* page = xdev->ctrl_page;

	*requested_num_queues = page->multi_queue_num_queues;

	return 0;
}


int store_read_data_rings_info(struct xenbus_device *xdev, struct xenvif_queue *queue,
		unsigned long *tx_ring_ref, unsigned long *rx_ring_ref,
		unsigned int *tx_evtchn, unsigned int *rx_evtchn)
{
	struct noxs_vif_ctrl_page* page = xdev->ctrl_page;
	unsigned int num_queues = queue->vif->num_queues;

	/* If the frontend requested 1 queue, or we have fallen back
	 * to single queue due to lack of frontend support for multi-
	 * queue, expect the remaining XenStore keys in the toplevel
	 * directory. Otherwise, expect them in a subdirectory called
	 * queue-N.
	 */
	if (num_queues == 1) {
		*tx_ring_ref = page->tx_ring_ref;
		*rx_ring_ref = page->rx_ring_ref;

		*tx_evtchn = page->event_channel_tx;
		*rx_evtchn = page->event_channel_rx;

	} else {
		/* TODO: "%s/queue-%u", dev->otherend, queue->id */
	}

	return 0;
}

int store_read_vif_flags(struct xenbus_device *xdev, struct xenvif *vif)
{
	struct noxs_vif_ctrl_page* page = xdev->ctrl_page;
	unsigned int rx_copy;
	int err, val;

	rx_copy = page->request_rx_copy;
	if (!rx_copy)
		return -EOPNOTSUPP;

	val = page->feature.rx_notify;
	if (!val) {
		/* - Reduce drain timeout to poll more frequently for
		 *   Rx requests.
		 * - Disable Rx stall detection.
		 */
		vif->drain_timeout = msecs_to_jiffies(30);
		vif->stall_timeout = 0;
	}

	val = page->feature.sg;
	vif->can_sg = !!val;

	vif->gso_mask = 0;
	vif->gso_prefix_mask = 0;

	val = page->feature.gso_tcpv4;
	if (val)
		vif->gso_mask |= GSO_BIT(TCPV4);

	val = page->feature.gso_tcpv4_prefix;
	if (val)
		vif->gso_prefix_mask |= GSO_BIT(TCPV4);

	val = page->feature.gso_tcpv6;
	if (val)
		vif->gso_mask |= GSO_BIT(TCPV6);

	val = page->feature.gso_tcpv6_prefix;
	if (val)
		vif->gso_prefix_mask |= GSO_BIT(TCPV6);

	if (vif->gso_mask & vif->gso_prefix_mask) {
		xenbus_dev_fatal(xdev, err, "%s: gso and gso prefix flags are not ""mutually exclusive", to_xenbus_otherend(xdev));
		return -EOPNOTSUPP;
	}

	val = page->feature.no_csum_offload;
	vif->ip_csum = !val;

	val = page->feature.ipv6_csum_offload;
	vif->ipv6_csum = !!val;

	return 0;
}

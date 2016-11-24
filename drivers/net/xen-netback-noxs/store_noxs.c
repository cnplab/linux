/*
 * io_noxs.c
 *
 *  Created on: Sep 23, 2016
 *      Author: wolf
 */

#include "common.h"
#include "store.h"


int store_write_init_info(struct xenbus_device *xdev)
{
	noxs_vif_ctrl_page_t *page = xdev->ctrl_page;
	noxs_cfg_vif_t *cfg = xdev->dev_cfg;
	int sg = 1;

	page->hdr.be_state = XenbusStateUnknown;
	page->hdr.fe_state = XenbusStateUnknown;
	/*page->vifid = xdev->id; //TODO vc->vif;*/

	memset(&page->be_feat, 0, sizeof(page->be_feat));//TODO needed?
	page->be_feat.sg = sg;
	page->be_feat.gso_tcpv4 = sg;
	page->be_feat.gso_tcpv6 = sg;
	page->be_feat.ipv6_csum_offload = 1;
	page->be_feat.rx_copy = 1;
	page->be_feat.rx_flip = 0;
	page->be_feat.multicast_control = 1;
	page->be_feat.dynamic_multicast_control = 1;

	page->be_feat.split_event_channels = separate_tx_rx_irq;
	page->be_feat.ctrl_ring = 1;

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
	memcpy(page->bridge, cfg->bridge, IF_LEN);

	return 0;
}

/*********************************************************
 * TODO TEMPORARY, not a scalable solution:
 */

struct noxs_dev_counter {
	struct list_head list;
	domid_t fe_id;
	int next_id;
	int dev_num;
};

static LIST_HEAD(device_counters);

static int netback_alloc_id(struct xenbus_device *xdev)
{
	struct noxs_dev_counter *c;

	list_for_each_entry(c, &device_counters, list) {
		if (c->fe_id == xdev->otherend_id)
			goto out;
	}

	c = kzalloc(sizeof(struct noxs_dev_counter), GFP_KERNEL);
	if (!c) {
		xenbus_dev_fatal(xdev, -ENOMEM, "allocating device id");
		return -ENOMEM;
	}

	c->fe_id = xdev->otherend_id;
	list_add(&c->list, &device_counters);

out:
	xdev->id = c->next_id++;
	c->dev_num++;
	return 0;
}

static void netback_clear_ids(void)
{
	struct noxs_dev_counter *c, *tmp;

	list_for_each_entry_safe(c, tmp, &device_counters, list) {
		list_del(&c->list);
		kfree(c);
	}
}

int store_read_handle(struct xenbus_device *xdev,
		long *handle)
{
	noxs_vif_ctrl_page_t *page;
	int err;

	err = netback_alloc_id(xdev);
	if (err)
		return 0;

	*handle = xdev->id;

	/* update control page */
	page = xdev->ctrl_page;
	page->vifid = xdev->id;

	return 1;
}

void store_read_rate(struct xenbus_device *dev,
		unsigned long *bytes, unsigned long *usec)
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

int store_read_ctrl_ring_info(struct xenbus_device *xdev,
		grant_ref_t *ring_ref, unsigned int *evtchn)
{
	struct noxs_vif_ctrl_page* page = xdev->ctrl_page;

	if (page->fe_feat.ctrl_ring) {
		*ring_ref = page->ctrl_ring_ref;
		*evtchn = page->event_channel_ctrl;
	} else {
		*ring_ref = INVALID_GRANT_HANDLE;
		//TODO evtchn?
	}

	return 0;
}

int store_read_num_queues(struct xenbus_device *xdev,
		unsigned int *requested_num_queues)
{
	struct noxs_vif_ctrl_page* page = xdev->ctrl_page;

	*requested_num_queues = page->multi_queue_num_queues;
	return 0;
}


int store_read_data_rings_info(struct xenbus_device *xdev,
		struct xenvif_queue *queue,
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

int store_read_vif_flags(struct xenbus_device *xdev,
		struct xenvif *vif)
{
	struct noxs_vif_ctrl_page* page = xdev->ctrl_page;

	if (!page->request_rx_copy)
		return -EOPNOTSUPP;

	if (!page->fe_feat.rx_notify) {
		/* - Reduce drain timeout to poll more frequently for
		 *   Rx requests.
		 * - Disable Rx stall detection.
		 */
		vif->drain_timeout = msecs_to_jiffies(30);
		vif->stall_timeout = 0;
	}

	vif->can_sg = !!page->fe_feat.sg;

	vif->gso_mask = 0;
	vif->gso_prefix_mask = 0;

	if (page->fe_feat.gso_tcpv4)
		vif->gso_mask |= GSO_BIT(TCPV4);

	if (page->fe_feat.gso_tcpv4_prefix)
		vif->gso_prefix_mask |= GSO_BIT(TCPV4);

	if (page->fe_feat.gso_tcpv6)
		vif->gso_mask |= GSO_BIT(TCPV6);

	if (page->fe_feat.gso_tcpv6_prefix)
		vif->gso_prefix_mask |= GSO_BIT(TCPV6);

	if (vif->gso_mask & vif->gso_prefix_mask) {
		xenbus_dev_fatal(xdev, 0,
				"%s: gso and gso prefix flags are not ""mutually exclusive",
				to_xenbus_otherend(xdev));
		return -EOPNOTSUPP;
	}

	vif->ip_csum = !page->fe_feat.no_csum_offload;
	vif->ipv6_csum = !!page->fe_feat.ipv6_csum_offload;

	return 0;
}

const char *store_get_bridge(struct xenbus_device *xdev)
{
	struct noxs_vif_ctrl_page* page = xdev->ctrl_page;

	return page->bridge;
}

void store_destroy(void)
{
	netback_clear_ids();
}

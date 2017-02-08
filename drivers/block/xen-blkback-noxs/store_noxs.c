#include "common.h"
#include "store.h"


int store_write_init_info(struct xenbus_device *dev)
{
	noxs_vbd_ctrl_page_t *page = dev->ctrl_page;
	/*noxs_cfg_vif_t *cfg = xdev->dev_cfg; TODO */

	page->hdr.be_state = XenbusStateInitialising;
	page->hdr.fe_state = XenbusStateInitialising;
	/*page->hdr.devid = xdev->id; TODO */

#ifdef NOXS_TRACE_DEV_INIT
	getnstimeofday64((struct timespec64 *) &page->hdr.ts_create);
#endif

	/*page->vifid = xdev->id;//TODO redundant*/

	memset(&page->be_feat, 0, sizeof(page->be_feat));//TODO needed?
	page->be_feat.max_indirect_segments = MAX_INDIRECT_SEGMENTS;

	page->multi_queue_max_queues = xenblk_max_queues;
	/* at least one, if more then overwritten by frontend: */
	page->multi_queue_num_queues = 1;

	page->max_ring_page_order = xen_blkif_max_ring_order;

	/* config */
	//TODO

	return 0;
}

int store_watch_be(struct xenbus_device *dev, struct xenbus_watch *watch, void (*cb)(struct xenbus_watch *watch, const char **vec, unsigned int len))//TODO
{
	return xenbus_watch_pathfmt(dev, watch, cb,
			"%s/%s", dev->nodename, "physical-device");
}

int store_read_be_version(struct xenbus_device *dev, unsigned *major, unsigned *minor)
{
	noxs_vbd_ctrl_page_t *page = dev->ctrl_page;

	*major = page->major;
	*minor = page->minor;

	return 0;
}

int store_read_be_mode(struct xenbus_device *dev, char **mode)
{
	*mode = xenbus_read(XBT_NIL, dev->nodename, "mode", NULL);//TODO
	return 0;
}

int store_read_be_device_type(struct xenbus_device *dev, char **device_type)
{
	*device_type = xenbus_read(XBT_NIL, dev->otherend, "device-type", NULL);//TODO
	return 0;
}

int store_read_be_handle(struct xenbus_device *dev, unsigned long *handle)
{
	return kstrtoul(strrchr(dev->otherend, '/') + 1, 0, handle);//TODO
}


int store_write_be_feat_flush_cache(struct xenbus_device *dev, struct xenbus_transaction xbt,
				  int state)
{
	noxs_vbd_ctrl_page_t *page = dev->ctrl_page;

	page->be_feat.flush_cache = state;
	return 0;
}

static void xen_blkbk_discard(struct xenbus_device *dev, struct xenbus_transaction xbt,
		struct xen_blkif *blkif)
{
	noxs_vbd_ctrl_page_t *page = dev->ctrl_page;
	int state = 0;
	struct block_device *bdev = blkif->vbd.bdev;
	struct request_queue *q = bdev_get_queue(bdev);

	if (page->discard_enable)
		return;

	if (blk_queue_discard(q)) {
		page->discard_granularity = q->limits.discard_granularity;
		page->discard_alignment = q->limits.discard_alignment;
		state = 1;
		/* Optional. */
		page->discard_secure = blkif->vbd.discard_secure;
	}
	page->be_feat.discard = state;
}

int store_write_be_feat_barrier(struct xenbus_device *dev, struct xenbus_transaction xbt,
				  int state)
{
	noxs_vbd_ctrl_page_t *page = dev->ctrl_page;

	page->be_feat.barrier = state;
	return 0;
}


void store_write_provide_fe_info(struct xenbus_device *dev, struct xen_blkif *blkif)
{
	noxs_vbd_ctrl_page_t *page = dev->ctrl_page;
	int err;

	/* If we can't advertise it is OK. */
	store_write_be_feat_flush_cache(dev, XBT_NIL, blkif->vbd.flush_support);

	xen_blkbk_discard(dev, XBT_NIL, blkif);

	store_write_be_feat_barrier(dev, XBT_NIL, blkif->vbd.flush_support);

	page->be_feat.persistent = 1;

	page->sectors = (unsigned long long) vbd_sz(&blkif->vbd));

	/* FIXME: use a typename instead */
	err = xenbus_printf(xbt, dev->nodename, "info", "%u",
			   blkif->vbd.type |
			    (blkif->vbd.readonly ? VDISK_READONLY : 0));
	if (err) {
		xenbus_dev_fatal(dev, err, "writing %s/info",
				 dev->nodename);
		goto abort;
	}

	page->sector_size = (unsigned long) bdev_logical_block_size(blkif->vbd.bdev);
	page->physical_sector_size = bdev_physical_block_size(blkif->vbd.bdev);

	err = xenbus_switch_state(dev, XenbusStateConnected);
	if (err)
		xenbus_dev_fatal(dev, err, "%s: switching to Connected state",
				 dev->nodename);
}


int store_read_fe_protocol(struct xenbus_device *dev, char *protocol)
{
	noxs_vbd_ctrl_page_t *page = dev->ctrl_page;

	strncpy(protocol, page->protocol, 63);
	return 0;
}

int store_read_fe_persistent(struct xenbus_device *dev, unsigned int *pers_grants)
{
	noxs_vbd_ctrl_page_t *page = dev->ctrl_page;

	*pers_grants = page->fe_feat.persistent;
	return 0;
}

int store_read_num_queues(struct xenbus_device *dev, unsigned int *requested_num_queues)
{
	noxs_vbd_ctrl_page_t *page = dev->ctrl_page;

	*requested_num_queues = page->multi_queue_num_queues;
	return 0;
}

int store_ring_ref_init(struct xenbus_device *dev, struct store_ring_ref* ref, int ring_index)//TODO
{
	size_t xspathsize;
	const size_t xenstore_path_ext_size = 11; /* sufficient for "/queue-NNN" */

	if (ring_index < 0) {
		ref->store_address = kstrdup(dev->otherend, GFP_KERNEL);

	} else {
		xspathsize = strlen(dev->otherend) + xenstore_path_ext_size;
		ref->store_address = kmalloc(xspathsize, GFP_KERNEL);
		if (!ref->store_address) {
			xenbus_dev_fatal(dev, -ENOMEM, "reading ring references");
			return -ENOMEM;
		}

		memset(ref->store_address, 0, xspathsize);
		snprintf(ref->store_address, xspathsize, "%s/queue-%u", dev->otherend, ring_index);
	}

	ref->index = ring_index;

	return 0;
}

void store_ring_ref_clear(struct store_ring_ref* ref)//TODO
{
	if (ref->store_address) {
		kfree(ref->store_address);
	}
}

const char* store_ring_ref_str(const struct store_ring_ref* ref)//TODO
{
	return ref->store_address;
}

int store_read_event_channel(struct xenbus_device *dev, struct store_ring_ref* ref,
		unsigned int *evtchn)
{
	noxs_vbd_ctrl_page_t *page = dev->ctrl_page;

	*evtchn = page->evtchn;
	return 0;
}

int store_read_ring_page_order(struct xenbus_device *dev, struct store_ring_ref* ref,
		unsigned int *ring_page_order)
{
	noxs_vbd_ctrl_page_t *page = dev->ctrl_page;

	*ring_page_order = page->ring_page_order;
	return 0;
}

int store_read_ring_ref(struct xenbus_device *dev, struct store_ring_ref* ref,
		int ref_index, unsigned int *ring_ref)
{
	noxs_vbd_ctrl_page_t *page = dev->ctrl_page;

	if (ref_index < 0) {
		ref_index = 0;
	}

	//TODO check ref_index < max
	ring_ref = page->ring_ref[ref_index];

	return 0;
}

int store_write_sectors_num(struct xenbus_device *dev, unsigned long long sectors)
{
	noxs_vbd_ctrl_page_t *page = dev->ctrl_page;

	page->sectors = sectors;
	/*
	 * Write the current state; we will use this to synchronize
	 * the front-end. If the current state is "connected" the
	 * front-end will get the new size information online.
	 */
	//TODO
	err = xenbus_printf(xbt, dev->nodename, "state", "%d", dev->state);
	if (err) {
		pr_warn("Error writing the state\n");
		goto abort;
	}

	return 0;
}

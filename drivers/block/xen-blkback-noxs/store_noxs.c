#include <linux/kernel.h>
#include <xen/noxs.h>
#include "common.h"
#include "store.h"


int store_write_init_info(struct xenbus_device *xdev)
{
	noxs_vbd_ctrl_page_t *page = xdev->ctrl_page;
	noxs_cfg_vbd_t *cfg = xdev->dev_cfg;

	page->hdr.devid = xdev->id;
	page->hdr.be_state = XenbusStateInitialising;
	page->hdr.fe_state = XenbusStateInitialising;

#ifdef NOXS_TRACE_DEV_INIT
	getnstimeofday64((struct timespec64 *) &page->hdr.ts_create);
#endif

	memset(&page->be_feat, 0, sizeof(page->be_feat));//TODO needed?
	page->be_feat.max_indirect_segments = MAX_INDIRECT_SEGMENTS;

	/* Rings */
	page->max_rings = xenblk_max_queues;
	page->max_ring_page_order = xen_blkif_max_ring_order;

	/* at least one, if more then overwritten by frontend: */
	page->num_rings = 1;
	page->ring_page_order = 0;

	/* config */
	page->major = cfg->major;
	page->minor = cfg->minor;
	/* TODO this is hackish: */
	page->type = cfg->type;
	page->mode = cfg->mode;

	return 0;
}

int store_read_cfg(struct xenbus_device *xdev, void *cfg)
{
	noxs_vbd_ctrl_page_t *page = xdev->ctrl_page;
	noxs_cfg_vbd_t *vbd_cfg = cfg;

	vbd_cfg->major = page->major;
	vbd_cfg->minor = page->minor;

	vbd_cfg->type = page->type;
	vbd_cfg->mode = page->mode;

	return 0;
}

int store_watch_be(struct xenbus_device *xdev, struct xenbus_watch *watch, void (*cb)(struct xenbus_watch *watch, const char **vec, unsigned int len))//TODO
{
	(*cb)(watch, NULL, 0);//TODO

	return 0;
}

int store_read_be_node(struct xenbus_device *xdev, unsigned *major, unsigned *minor)
{
	noxs_vbd_ctrl_page_t *page = xdev->ctrl_page;

	*major = page->major;
	*minor = page->minor;

	/* Return number of read values */
	return 2;
}

int store_read_be_devname(struct xenbus_device *xdev, struct xen_blkif *blkif, char *buf)//TODO check params
{
	noxs_vbd_ctrl_page_t *page = xdev->ctrl_page;
	struct block_device *bdev = blkif->vbd.bdev;

	bdevname(bdev, buf);

	return 0;
}

int store_read_be_mode(struct xenbus_device *xdev, char **mode)//TODO check again if needed allocation
{
	noxs_vbd_ctrl_page_t *page = xdev->ctrl_page;

	if (page->mode == noxs_vbd_mode_rdwr)
		*mode = kstrdup("w", GFP_KERNEL);
	else if (page->mode == noxs_vbd_mode_rdonly)
		*mode = kstrdup("r", GFP_KERNEL);
	/* else TODO */

	return 0;
}

int store_read_be_device_type(struct xenbus_device *xdev, char **device_type)
{
	*device_type = kstrdup("disk", GFP_KERNEL);//TODO

	return 0;
}

int store_read_be_handle(struct xenbus_device *xdev, unsigned long *handle)
{
	noxs_vbd_ctrl_page_t *page = xdev->ctrl_page;

	*handle = page->hdr.devid;

	return 0;
}


int store_write_be_feat_flush_cache(struct xenbus_device *xdev, struct xenbus_transaction xbt,
				  int state)
{
	noxs_vbd_ctrl_page_t *page = xdev->ctrl_page;

	page->be_feat.flush_cache = state;

	return 0;
}

static void xen_blkbk_discard(struct xenbus_device *xdev, struct xenbus_transaction xbt,
		struct xen_blkif *blkif)
{
	noxs_vbd_ctrl_page_t *page = xdev->ctrl_page;
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

int store_write_be_feat_barrier(struct xenbus_device *xdev, struct xenbus_transaction xbt,
				  int state)
{
	noxs_vbd_ctrl_page_t *page = xdev->ctrl_page;

	page->be_feat.barrier = state;

	return 0;
}


int store_write_provide_fe_info(struct xenbus_device *xdev, struct xen_blkif *blkif)
{
	noxs_vbd_ctrl_page_t *page = xdev->ctrl_page;

	/* If we can't advertise it is OK. */
	store_write_be_feat_flush_cache(xdev, XBT_NIL, blkif->vbd.flush_support);

	xen_blkbk_discard(xdev, XBT_NIL, blkif);

	store_write_be_feat_barrier(xdev, XBT_NIL, blkif->vbd.flush_support);

	page->be_feat.persistent = 1;

	page->sectors = (unsigned long long) vbd_sz(&blkif->vbd);

	page->info = (blkif->vbd.type |
				    (blkif->vbd.readonly ? VDISK_READONLY : 0));

	page->sector_size = (unsigned long) bdev_logical_block_size(blkif->vbd.bdev);
	page->sector_size_physical = bdev_physical_block_size(blkif->vbd.bdev);

	return 0;
}


int store_read_fe_protocol(struct xenbus_device *xdev, char *protocol)
{
	noxs_vbd_ctrl_page_t *page = xdev->ctrl_page;

	strncpy(protocol, page->protocol, 63);

	return 1;
}

int store_read_fe_persistent(struct xenbus_device *xdev, unsigned int *pers_grants)
{
	noxs_vbd_ctrl_page_t *page = xdev->ctrl_page;

	*pers_grants = page->fe_feat.persistent;

	return 0;
}

int store_read_num_queues(struct xenbus_device *xdev, unsigned int *num_queues)
{
	noxs_vbd_ctrl_page_t *page = xdev->ctrl_page;

	*num_queues = page->num_rings;

	return 0;
}

int store_write_sectors_num(struct xenbus_device *xdev, unsigned long long sectors)
{
	noxs_vbd_ctrl_page_t *page = xdev->ctrl_page;

	page->sectors = sectors;
	/*
	 * Write the current state; we will use this to synchronize
	 * the front-end. If the current state is "connected" the
	 * front-end will get the new size information online.
	 */
	page->hdr.be_state = xdev->state;

	return 0;
}

int store_read_ring_page_order(struct xenbus_device *xdev, unsigned int *ring_page_order)
{
	noxs_vbd_ctrl_page_t *page = xdev->ctrl_page;

	*ring_page_order = page->ring_page_order;

	return (page->ring_page_order > 0);
}

int store_ring_ref_init(struct store_ring_ref* srref,
		struct xenbus_device *xdev, int ring_index)//TODO
{
	noxs_vbd_ctrl_page_t *page = xdev->ctrl_page;
	noxs_ring_t *rings_addr = &page->rings_start_addr[0];

	if (ring_index < 0) {
		srref->store_address = rings_addr;

	} else {
		unsigned int ring_struct_size = sizeof(noxs_ring_t) +
				(1 << page->ring_page_order) * sizeof(grant_ref_t);

		srref->store_address = rings_addr + ring_index * ring_struct_size;
	}

	srref->index = ring_index;

	return 0;
}

void store_ring_ref_clear(struct store_ring_ref* srref)//TODO
{
	srref->store_address = NULL;
}

const char* store_ring_ref_str(const struct store_ring_ref* srref)//TODO
{
	return srref->store_address;
}

int store_read_event_channel(struct store_ring_ref* srref,
		unsigned int *evtchn)
{
	noxs_ring_t *ring = srref->store_address;

	*evtchn = ring->evtchn;

	return 1;
}

int store_read_ring_ref(struct store_ring_ref* srref,
		int ref_index, unsigned int *ring_ref)
{
	noxs_ring_t *ring = srref->store_address;

	if (ref_index < 0) {
		ref_index = 0;
	}

	//TODO check ref_index < max
	*ring_ref = ring->refs[ref_index];

	return 1;
}

/*  Xenbus code for blkif backend
    Copyright (C) 2005 Rusty Russell <rusty@rustcorp.com.au>
    Copyright (C) 2005 XenSource Ltd

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

*/

#define pr_fmt(fmt) "xen-blkback: " fmt

#include <stdarg.h>
#include <linux/module.h>
#include <linux/kthread.h>
#include <xen/events.h>
#include <xen/grant_table.h>
#include "common.h"
#include "store.h"

struct backend_info {
	struct xenbus_device	*dev;
	struct xen_blkif	*blkif;
	struct xenbus_watch	backend_watch;
	unsigned		major;
	unsigned		minor;
	char			*mode;
};

static struct kmem_cache *xen_blkif_cachep;
static void connect(struct backend_info *);
static int connect_ring(struct backend_info *);
static void backend_changed(struct xenbus_watch *, const char **,
			    unsigned int);
static void xen_blkif_free(struct xen_blkif *blkif);
static void xen_vbd_free(struct xen_vbd *vbd);

struct xenbus_device *xen_blkbk_xenbus(struct backend_info *be)
{
	return be->dev;
}

/*
 * The last request could free the device from softirq context and
 * xen_blkif_free() can sleep.
 */
static void xen_blkif_deferred_free(struct work_struct *work)
{
	struct xen_blkif *blkif;

	blkif = container_of(work, struct xen_blkif, free_work);
	xen_blkif_free(blkif);
}

static int blkback_name(struct xen_blkif *blkif, char *buf)
{
	return store_read_be_devname(NULL, blkif, buf);//TODO revisit call
}

static void xen_update_blkif_status(struct xen_blkif *blkif)
{
	int err;
	char name[TASK_COMM_LEN];
	struct xen_blkif_ring *ring;
	int i;

	/* Not ready to connect? */
	if (!blkif->rings || !blkif->rings[0].irq || !blkif->vbd.bdev)
		return;

	/* Already connected? */
	if (blkif->be->dev->state == XenbusStateConnected)
		return;

	/* Attempt to connect: exit if we fail to. */
	connect(blkif->be);
	if (blkif->be->dev->state != XenbusStateConnected)
		return;

	err = blkback_name(blkif, name);
	if (err) {
		xenbus_dev_error(blkif->be->dev, err, "get blkback dev name");
		return;
	}

	err = filemap_write_and_wait(blkif->vbd.bdev->bd_inode->i_mapping);
	if (err) {
		xenbus_dev_error(blkif->be->dev, err, "block flush");
		return;
	}
	invalidate_inode_pages2(blkif->vbd.bdev->bd_inode->i_mapping);

	for (i = 0; i < blkif->nr_rings; i++) {
		ring = &blkif->rings[i];
		ring->xenblkd = kthread_run(xen_blkif_schedule, ring, "%s-%d", name, i);
		if (IS_ERR(ring->xenblkd)) {
			err = PTR_ERR(ring->xenblkd);
			ring->xenblkd = NULL;
			xenbus_dev_fatal(blkif->be->dev, err,
					"start %s-%d xenblkd", name, i);
			goto out;
		}
	}
	return;

out:
	while (--i >= 0) {
		ring = &blkif->rings[i];
		kthread_stop(ring->xenblkd);
	}
	return;
}

static int xen_blkif_alloc_rings(struct xen_blkif *blkif)
{
	unsigned int r;

	blkif->rings = kzalloc(blkif->nr_rings * sizeof(struct xen_blkif_ring), GFP_KERNEL);
	if (!blkif->rings)
		return -ENOMEM;

	for (r = 0; r < blkif->nr_rings; r++) {
		struct xen_blkif_ring *ring = &blkif->rings[r];

		spin_lock_init(&ring->blk_ring_lock);
		init_waitqueue_head(&ring->wq);
		INIT_LIST_HEAD(&ring->pending_free);
		INIT_LIST_HEAD(&ring->persistent_purge_list);
		INIT_WORK(&ring->persistent_purge_work, xen_blkbk_unmap_purged_grants);
		spin_lock_init(&ring->free_pages_lock);
		INIT_LIST_HEAD(&ring->free_pages);

		spin_lock_init(&ring->pending_free_lock);
		init_waitqueue_head(&ring->pending_free_wq);
		init_waitqueue_head(&ring->shutdown_wq);
		ring->blkif = blkif;
		ring->st_print = jiffies;
		xen_blkif_get(blkif);
	}

	return 0;
}

static struct xen_blkif *xen_blkif_alloc(domid_t domid)
{
	struct xen_blkif *blkif;

	BUILD_BUG_ON(MAX_INDIRECT_PAGES > BLKIF_MAX_INDIRECT_PAGES_PER_REQUEST);

	blkif = kmem_cache_zalloc(xen_blkif_cachep, GFP_KERNEL);
	if (!blkif)
		return ERR_PTR(-ENOMEM);

	blkif->domid = domid;
	atomic_set(&blkif->refcnt, 1);
	init_completion(&blkif->drain_complete);
	INIT_WORK(&blkif->free_work, xen_blkif_deferred_free);

	return blkif;
}

static int xen_blkif_map(struct xen_blkif_ring *ring, grant_ref_t *gref,
			 unsigned int nr_grefs, unsigned int evtchn)
{
	int err;
	struct xen_blkif *blkif = ring->blkif;

	/* Already connected through? */
	if (ring->irq)
		return 0;

	err = xenbus_map_ring_valloc(blkif->be->dev, gref, nr_grefs,
				     &ring->blk_ring);
	if (err < 0)
		return err;

	switch (blkif->blk_protocol) {
	case BLKIF_PROTOCOL_NATIVE:
	{
		struct blkif_sring *sring;
		sring = (struct blkif_sring *)ring->blk_ring;
		BACK_RING_INIT(&ring->blk_rings.native, sring,
			       XEN_PAGE_SIZE * nr_grefs);
		break;
	}
	case BLKIF_PROTOCOL_X86_32:
	{
		struct blkif_x86_32_sring *sring_x86_32;
		sring_x86_32 = (struct blkif_x86_32_sring *)ring->blk_ring;
		BACK_RING_INIT(&ring->blk_rings.x86_32, sring_x86_32,
			       XEN_PAGE_SIZE * nr_grefs);
		break;
	}
	case BLKIF_PROTOCOL_X86_64:
	{
		struct blkif_x86_64_sring *sring_x86_64;
		sring_x86_64 = (struct blkif_x86_64_sring *)ring->blk_ring;
		BACK_RING_INIT(&ring->blk_rings.x86_64, sring_x86_64,
			       XEN_PAGE_SIZE * nr_grefs);
		break;
	}
	default:
		BUG();
	}

	err = bind_interdomain_evtchn_to_irqhandler(blkif->domid, evtchn,
						    xen_blkif_be_int, 0,
						    "blkif-backend", ring);
	if (err < 0) {
		xenbus_unmap_ring_vfree(blkif->be->dev, ring->blk_ring);
		ring->blk_rings.common.sring = NULL;
		return err;
	}
	ring->irq = err;

	return 0;
}

static int xen_blkif_disconnect(struct xen_blkif *blkif)
{
	struct pending_req *req, *n;
	unsigned int j, r;

	for (r = 0; r < blkif->nr_rings; r++) {
		struct xen_blkif_ring *ring = &blkif->rings[r];
		unsigned int i = 0;

		if (ring->xenblkd) {
			kthread_stop(ring->xenblkd);
			wake_up(&ring->shutdown_wq);
			ring->xenblkd = NULL;
		}

		/* The above kthread_stop() guarantees that at this point we
		 * don't have any discard_io or other_io requests. So, checking
		 * for inflight IO is enough.
		 */
		if (atomic_read(&ring->inflight) > 0)
			return -EBUSY;

		if (ring->irq) {
			unbind_from_irqhandler(ring->irq, ring);
			ring->irq = 0;
		}

		if (ring->blk_rings.common.sring) {
			xenbus_unmap_ring_vfree(blkif->be->dev, ring->blk_ring);
			ring->blk_rings.common.sring = NULL;
		}

		/* Remove all persistent grants and the cache of ballooned pages. */
		xen_blkbk_free_caches(ring);

		/* Check that there is no request in use */
		list_for_each_entry_safe(req, n, &ring->pending_free, free_list) {
			list_del(&req->free_list);

			for (j = 0; j < MAX_INDIRECT_SEGMENTS; j++)
				kfree(req->segments[j]);

			for (j = 0; j < MAX_INDIRECT_PAGES; j++)
				kfree(req->indirect_pages[j]);

			kfree(req);
			i++;
		}

		BUG_ON(atomic_read(&ring->persistent_gnt_in_use) != 0);
		BUG_ON(!list_empty(&ring->persistent_purge_list));
		BUG_ON(!RB_EMPTY_ROOT(&ring->persistent_gnts));
		BUG_ON(!list_empty(&ring->free_pages));
		BUG_ON(ring->free_pages_num != 0);
		BUG_ON(ring->persistent_gnt_c != 0);
		WARN_ON(i != (XEN_BLKIF_REQS_PER_PAGE * blkif->nr_ring_pages));
		xen_blkif_put(blkif);
	}
	blkif->nr_ring_pages = 0;
	/*
	 * blkif->rings was allocated in connect_ring, so we should free it in
	 * here.
	 */
	kfree(blkif->rings);
	blkif->rings = NULL;
	blkif->nr_rings = 0;

	return 0;
}

static void xen_blkif_free(struct xen_blkif *blkif)
{

	xen_blkif_disconnect(blkif);
	xen_vbd_free(&blkif->vbd);

	/* Make sure everything is drained before shutting down */
	kmem_cache_free(xen_blkif_cachep, blkif);
}

int __init xen_blkif_interface_init(void)
{
	xen_blkif_cachep = kmem_cache_create("blkif_cache",
					     sizeof(struct xen_blkif),
					     0, 0, NULL);
	if (!xen_blkif_cachep)
		return -ENOMEM;

	return 0;
}

/*
 *  sysfs interface for VBD I/O requests
 */

#define VBD_SHOW_ALLRING(name, format)					\
	static ssize_t show_##name(struct device *_dev,			\
				   struct device_attribute *attr,	\
				   char *buf)				\
	{								\
		struct xenbus_device *dev = to_xenbus_device(_dev);	\
		struct backend_info *be = dev_get_drvdata(&dev->dev);	\
		struct xen_blkif *blkif = be->blkif;			\
		unsigned int i;						\
		unsigned long long result = 0;				\
									\
		if (!blkif->rings)				\
			goto out;					\
									\
		for (i = 0; i < blkif->nr_rings; i++) {		\
			struct xen_blkif_ring *ring = &blkif->rings[i];	\
									\
			result += ring->st_##name;			\
		}							\
									\
out:									\
		return sprintf(buf, format, result);			\
	}								\
	static DEVICE_ATTR(name, S_IRUGO, show_##name, NULL)

VBD_SHOW_ALLRING(oo_req,  "%llu\n");
VBD_SHOW_ALLRING(rd_req,  "%llu\n");
VBD_SHOW_ALLRING(wr_req,  "%llu\n");
VBD_SHOW_ALLRING(f_req,  "%llu\n");
VBD_SHOW_ALLRING(ds_req,  "%llu\n");
VBD_SHOW_ALLRING(rd_sect, "%llu\n");
VBD_SHOW_ALLRING(wr_sect, "%llu\n");

static struct attribute *xen_vbdstat_attrs[] = {
	&dev_attr_oo_req.attr,
	&dev_attr_rd_req.attr,
	&dev_attr_wr_req.attr,
	&dev_attr_f_req.attr,
	&dev_attr_ds_req.attr,
	&dev_attr_rd_sect.attr,
	&dev_attr_wr_sect.attr,
	NULL
};

static const struct attribute_group xen_vbdstat_group = {
	.name = "statistics",
	.attrs = xen_vbdstat_attrs,
};

#define VBD_SHOW(name, format, args...)					\
	static ssize_t show_##name(struct device *_dev,			\
				   struct device_attribute *attr,	\
				   char *buf)				\
	{								\
		struct xenbus_device *dev = to_xenbus_device(_dev);	\
		struct backend_info *be = dev_get_drvdata(&dev->dev);	\
									\
		return sprintf(buf, format, ##args);			\
	}								\
	static DEVICE_ATTR(name, S_IRUGO, show_##name, NULL)

VBD_SHOW(physical_device, "%x:%x\n", be->major, be->minor);
VBD_SHOW(mode, "%s\n", be->mode);

static int xenvbd_sysfs_addif(struct xenbus_device *dev)
{
	int error;

	error = device_create_file(&dev->dev, &dev_attr_physical_device);
	if (error)
		goto fail1;

	error = device_create_file(&dev->dev, &dev_attr_mode);
	if (error)
		goto fail2;

	error = sysfs_create_group(&dev->dev.kobj, &xen_vbdstat_group);
	if (error)
		goto fail3;

	return 0;

fail3:	sysfs_remove_group(&dev->dev.kobj, &xen_vbdstat_group);
fail2:	device_remove_file(&dev->dev, &dev_attr_mode);
fail1:	device_remove_file(&dev->dev, &dev_attr_physical_device);
	return error;
}

static void xenvbd_sysfs_delif(struct xenbus_device *dev)
{
	sysfs_remove_group(&dev->dev.kobj, &xen_vbdstat_group);
	device_remove_file(&dev->dev, &dev_attr_mode);
	device_remove_file(&dev->dev, &dev_attr_physical_device);
}


static void xen_vbd_free(struct xen_vbd *vbd)
{
	if (vbd->bdev)
		blkdev_put(vbd->bdev, vbd->readonly ? FMODE_READ : FMODE_WRITE);
	vbd->bdev = NULL;
}

static int xen_vbd_create(struct xen_blkif *blkif, blkif_vdev_t handle,
			  unsigned major, unsigned minor, int readonly,
			  int cdrom)
{
	struct xen_vbd *vbd;
	struct block_device *bdev;
	struct request_queue *q;

	vbd = &blkif->vbd;
	vbd->handle   = handle;
	vbd->readonly = readonly;
	vbd->type     = 0;

	vbd->pdevice  = MKDEV(major, minor);

	bdev = blkdev_get_by_dev(vbd->pdevice, vbd->readonly ?
				 FMODE_READ : FMODE_WRITE, NULL);

	if (IS_ERR(bdev)) {
		pr_warn("xen_vbd_create: device %08x could not be opened\n",
			vbd->pdevice);
		return -ENOENT;
	}

	vbd->bdev = bdev;
	if (vbd->bdev->bd_disk == NULL) {
		pr_warn("xen_vbd_create: device %08x doesn't exist\n",
			vbd->pdevice);
		xen_vbd_free(vbd);
		return -ENOENT;
	}
	vbd->size = vbd_sz(vbd);

	if (vbd->bdev->bd_disk->flags & GENHD_FL_CD || cdrom)
		vbd->type |= VDISK_CDROM;
	if (vbd->bdev->bd_disk->flags & GENHD_FL_REMOVABLE)
		vbd->type |= VDISK_REMOVABLE;

	q = bdev_get_queue(bdev);
	if (q && test_bit(QUEUE_FLAG_WC, &q->queue_flags))
		vbd->flush_support = true;

	if (q && blk_queue_secure_erase(q))
		vbd->discard_secure = true;

	pr_debug("Successful creation of handle=%04x (dom=%u)\n",
		handle, blkif->domid);
	return 0;
}
static int xen_blkbk_remove(struct xenbus_device *dev)
{
	struct backend_info *be = dev_get_drvdata(&dev->dev);

	pr_debug("%s %p %d\n", __func__, dev, dev->otherend_id);

	if (be->major || be->minor)
		xenvbd_sysfs_delif(dev);

	if (be->backend_watch.node) {
		//TODO unregister_xenbus_watch(&be->backend_watch);
		kfree(be->backend_watch.node);
		be->backend_watch.node = NULL;
	}

	dev_set_drvdata(&dev->dev, NULL);

	if (be->blkif)
		xen_blkif_disconnect(be->blkif);

	/* Put the reference we set in xen_blkif_alloc(). */
	xen_blkif_put(be->blkif);
	kfree(be->mode);
	kfree(be);
	return 0;
}

int xen_blkbk_flush_diskcache(struct xenbus_transaction xbt,
			      struct backend_info *be, int state)
{
	struct xenbus_device *dev = be->dev;

	return store_write_be_feat_flush_cache(dev, xbt, state);
}

int xen_blkbk_barrier(struct xenbus_transaction xbt,
		      struct backend_info *be, int state)
{
	struct xenbus_device *dev = be->dev;

	return store_write_be_feat_barrier(dev, xbt, state);
}

/*
 * Entry point to this code when a new device is created.  Allocate the basic
 * structures, and watch the store waiting for the hotplug scripts to tell us
 * the device's physical major and minor numbers.  Switch to InitWait.
 */
static int xen_blkbk_probe(struct xenbus_device *dev,
			   const struct xenbus_device_id *id)
{
	int err;
	struct backend_info *be = kzalloc(sizeof(struct backend_info),
					  GFP_KERNEL);

	/* match the pr_debug in xen_blkbk_remove */
	pr_debug("%s %p %d\n", __func__, dev, dev->otherend_id);

	if (!be) {
		xenbus_dev_fatal(dev, -ENOMEM,
				 "allocating backend structure");
		return -ENOMEM;
	}
	be->dev = dev;
	dev_set_drvdata(&dev->dev, be);

	be->blkif = xen_blkif_alloc(dev->otherend_id);
	if (IS_ERR(be->blkif)) {
		err = PTR_ERR(be->blkif);
		be->blkif = NULL;
		xenbus_dev_fatal(dev, err, "creating block interface");
		goto fail;
	}

	err = store_write_init_info(dev);
	if (err)
		goto fail;

	/* setup back pointer */
	be->blkif->be = be;

	err = store_watch_be(dev, &be->backend_watch, backend_changed);
	if (err)
		goto fail;

	err = xenbus_switch_state(dev, XenbusStateInitWait);
	if (err)
		goto fail;

	return 0;

fail:
	pr_warn("%s failed\n", __func__);
	xen_blkbk_remove(dev);
	return err;
}


/*
 * Callback received when the hotplug scripts have placed the physical-device
 * node.  Read it and the mode node, and create a vbd.  If the frontend is
 * ready, connect.
 */
static void backend_changed(struct xenbus_watch *watch,
			    const char **vec, unsigned int len)
{
	int err;
	unsigned major;
	unsigned minor;
	struct backend_info *be
		= container_of(watch, struct backend_info, backend_watch);
	struct xenbus_device *dev = be->dev;
	int cdrom = 0;
	unsigned long handle;
	char *device_type;

	pr_debug("%s %p %d\n", __func__, dev, dev->otherend_id);

	err = store_read_be_node(dev, &major, &minor);
	if (XENBUS_EXIST_ERR(err)) {
		/*
		 * Since this watch will fire once immediately after it is
		 * registered, we expect this.  Ignore it, and wait for the
		 * hotplug scripts.
		 */
		return;
	}
	if (err != 2) {
		xenbus_dev_fatal(dev, err, "reading physical-device");
		return;
	}

	if (be->major | be->minor) {
		if (be->major != major || be->minor != minor)
			pr_warn("changing physical device (from %x:%x to %x:%x) not supported.\n",
				be->major, be->minor, major, minor);
		return;
	}

	store_read_be_mode(dev, &be->mode);
	if (IS_ERR(be->mode)) {
		err = PTR_ERR(be->mode);
		be->mode = NULL;
		xenbus_dev_fatal(dev, err, "reading mode");
		return;
	}

	store_read_be_device_type(dev, &device_type);//TODO
	if (!IS_ERR(device_type)) {
		cdrom = strcmp(device_type, "cdrom") == 0;
		kfree(device_type);
	}

	/* Front end dir is a number, which is used as the handle. */
	err = store_read_be_handle(dev, &handle);
	if (err) {
		kfree(be->mode);
		be->mode = NULL;
		return;
	}

	be->major = major;
	be->minor = minor;

	err = xen_vbd_create(be->blkif, handle, major, minor,
			     !strchr(be->mode, 'w'), cdrom);

	if (err)
		xenbus_dev_fatal(dev, err, "creating vbd structure");
	else {
		err = xenvbd_sysfs_addif(dev);
		if (err) {
			xen_vbd_free(&be->blkif->vbd);
			xenbus_dev_fatal(dev, err, "creating sysfs entries");
		}
	}

	if (err) {
		kfree(be->mode);
		be->mode = NULL;
		be->major = 0;
		be->minor = 0;
	} else {
		/* We're potentially connected now */
		xen_update_blkif_status(be->blkif);
	}
}


/*
 * Callback received when the frontend's state changes.
 */
static void frontend_changed(struct xenbus_device *dev,
			     enum xenbus_state frontend_state)
{
	struct backend_info *be = dev_get_drvdata(&dev->dev);
	int err;

	pr_debug("%s %p %s\n", __func__, dev, xenbus_strstate(frontend_state));

	switch (frontend_state) {
	case XenbusStateInitialising:
		if (dev->state == XenbusStateClosed) {
			pr_info("%s: prepare for reconnect\n", to_xenbus_name(dev));
			xenbus_switch_state(dev, XenbusStateInitWait);
		}
		break;

	case XenbusStateInitialised:
	case XenbusStateConnected:
		/*
		 * Ensure we connect even when two watches fire in
		 * close succession and we miss the intermediate value
		 * of frontend_state.
		 */
		if (dev->state == XenbusStateConnected)
			break;

		/*
		 * Enforce precondition before potential leak point.
		 * xen_blkif_disconnect() is idempotent.
		 */
		err = xen_blkif_disconnect(be->blkif);
		if (err) {
			xenbus_dev_fatal(dev, err, "pending I/O");
			break;
		}

		err = connect_ring(be);
		if (err) {
			/*
			 * Clean up so that memory resources can be used by
			 * other devices. connect_ring reported already error.
			 */
			xen_blkif_disconnect(be->blkif);
			break;
		}
		xen_update_blkif_status(be->blkif);
		break;

	case XenbusStateClosing:
		xenbus_switch_state(dev, XenbusStateClosing);
		break;

	case XenbusStateClosed:
		xen_blkif_disconnect(be->blkif);
		xenbus_switch_state(dev, XenbusStateClosed);
		if (xenbus_dev_is_online(dev))
			break;
		/* fall through if not online */
	case XenbusStateUnknown:
		/* implies xen_blkif_disconnect() via xen_blkbk_remove() */
		device_unregister(&dev->dev);
		break;

	default:
		xenbus_dev_fatal(dev, -EINVAL, "saw state %d at frontend",
				 frontend_state);
		break;
	}
}


/* ** Connection ** */


/*
 * Write the physical details regarding the block device to the store, and
 * switch to Connected state.
 */
static void connect(struct backend_info *be)
{
	struct xenbus_device *dev = be->dev;
	int err;

	pr_debug("%s %s\n", __func__, to_xenbus_otherend(dev));

	/* Supply the information about the device the frontend needs */
	err = store_write_provide_fe_info(dev, be->blkif);
	if (err)
		return;

	err = xenbus_switch_state(dev, XenbusStateConnected);
	if (err)
		xenbus_dev_fatal(dev, err, "%s: switching to Connected state",
				 to_xenbus_name(dev));
}

/*
 * Each ring may have multi pages, depends on "ring-page-order".
 */
static int read_per_ring_refs(struct xen_blkif_ring *ring, struct store_ring_ref *srref)
{
	unsigned int ring_ref[XENBUS_MAX_RING_GRANTS];
	struct pending_req *req, *n;
	int err, i, j;
	struct xen_blkif *blkif = ring->blkif;
	struct xenbus_device *dev = blkif->be->dev;
	unsigned int ring_page_order, nr_grefs, evtchn;

	err = store_read_event_channel(srref, &evtchn);
	if (err != 1) {
		err = -EINVAL;
		xenbus_dev_fatal(dev, err, "reading %s/event-channel",
				store_ring_ref_str(srref));
		return err;
	}

	err = store_read_ring_page_order(dev, &ring_page_order);
	if (err != 1) {
		err = store_read_ring_ref(srref, -1, &ring_ref[0]);
		if (err != 1) {
			err = -EINVAL;
			xenbus_dev_fatal(dev, err, "reading %s/ring-ref",
					store_ring_ref_str(srref));
			return err;
		}
		nr_grefs = 1;
	} else {
		unsigned int i;

		if (ring_page_order > xen_blkif_max_ring_order) {
			err = -EINVAL;
			xenbus_dev_fatal(dev, err, "%s/request %d ring page order exceed max:%d",
					 store_ring_ref_str(srref), ring_page_order,
					 xen_blkif_max_ring_order);
			return err;
		}

		nr_grefs = 1 << ring_page_order;
		for (i = 0; i < nr_grefs; i++) {
			err = store_read_ring_ref(srref, i, &ring_ref[i]);
			if (err != 1) {
				err = -EINVAL;
				xenbus_dev_fatal(dev, err, "reading %s/ring-ref%u",
						 store_ring_ref_str(srref), i);
				return err;
			}
		}
	}
	blkif->nr_ring_pages = nr_grefs;

	for (i = 0; i < nr_grefs * XEN_BLKIF_REQS_PER_PAGE; i++) {
		req = kzalloc(sizeof(*req), GFP_KERNEL);
		if (!req)
			goto fail;
		list_add_tail(&req->free_list, &ring->pending_free);
		for (j = 0; j < MAX_INDIRECT_SEGMENTS; j++) {
			req->segments[j] = kzalloc(sizeof(*req->segments[0]), GFP_KERNEL);
			if (!req->segments[j])
				goto fail;
		}
		for (j = 0; j < MAX_INDIRECT_PAGES; j++) {
			req->indirect_pages[j] = kzalloc(sizeof(*req->indirect_pages[0]),
							 GFP_KERNEL);
			if (!req->indirect_pages[j])
				goto fail;
		}
	}

	/* Map the shared frame, irq etc. */
	err = xen_blkif_map(ring, ring_ref, nr_grefs, evtchn);
	if (err) {
		xenbus_dev_fatal(dev, err, "mapping ring-ref port %u", evtchn);
		return err;
	}

	return 0;

fail:
	list_for_each_entry_safe(req, n, &ring->pending_free, free_list) {
		list_del(&req->free_list);
		for (j = 0; j < MAX_INDIRECT_SEGMENTS; j++) {
			if (!req->segments[j])
				break;
			kfree(req->segments[j]);
		}
		for (j = 0; j < MAX_INDIRECT_PAGES; j++) {
			if (!req->indirect_pages[j])
				break;
			kfree(req->indirect_pages[j]);
		}
		kfree(req);
	}
	return -ENOMEM;

}

static int connect_ring(struct backend_info *be)
{
	struct xenbus_device *dev = be->dev;
	unsigned int pers_grants;
	char protocol[64] = "";
	int err, i;
	unsigned int requested_num_queues = 0;
	struct store_ring_ref store_ring_ref;

	pr_debug("%s %s\n", __func__, to_xenbus_otherend(dev));

	be->blkif->blk_protocol = BLKIF_PROTOCOL_DEFAULT;
	err = store_read_fe_protocol(dev, protocol);
	if (err <= 0)
		strcpy(protocol, "unspecified, assuming default");
	else if (0 == strcmp(protocol, XEN_IO_PROTO_ABI_NATIVE))
		be->blkif->blk_protocol = BLKIF_PROTOCOL_NATIVE;
	else if (0 == strcmp(protocol, XEN_IO_PROTO_ABI_X86_32))
		be->blkif->blk_protocol = BLKIF_PROTOCOL_X86_32;
	else if (0 == strcmp(protocol, XEN_IO_PROTO_ABI_X86_64))
		be->blkif->blk_protocol = BLKIF_PROTOCOL_X86_64;
	else {
		xenbus_dev_fatal(dev, err, "unknown fe protocol %s", protocol);
		return -ENOSYS;
	}
	err = store_read_fe_persistent(dev, &pers_grants);
	if (err <= 0)
		pers_grants = 0;

	be->blkif->vbd.feature_gnt_persistent = pers_grants;
	be->blkif->vbd.overflow_max_grants = 0;

	/*
	 * Read the number of hardware queues from frontend.
	 */
	err = store_read_num_queues(dev, &requested_num_queues);
	if (err < 0) {
		requested_num_queues = 1;
	} else {
		if (requested_num_queues > xenblk_max_queues
		    || requested_num_queues == 0) {
			/* Buggy or malicious guest. */
			xenbus_dev_fatal(dev, err,
					"guest requested %u queues, exceeding the maximum of %u.",
					requested_num_queues, xenblk_max_queues);
			return -ENOSYS;
		}
	}
	be->blkif->nr_rings = requested_num_queues;
	if (xen_blkif_alloc_rings(be->blkif))
		return -ENOMEM;

	pr_info("%s: using %d queues, protocol %d (%s) %s\n", to_xenbus_name(dev),
		 be->blkif->nr_rings, be->blkif->blk_protocol, protocol,
		 pers_grants ? "persistent grants" : "");

	if (be->blkif->nr_rings == 1) {
		store_ring_ref_init(&store_ring_ref, dev, -1);
		err = read_per_ring_refs(&be->blkif->rings[0], &store_ring_ref);
		store_ring_ref_clear(&store_ring_ref);
	}
	else {
		for (i = 0; i < be->blkif->nr_rings; i++) {
			store_ring_ref_init(&store_ring_ref, dev, i);
			err = read_per_ring_refs(&be->blkif->rings[i], &store_ring_ref);
			store_ring_ref_clear(&store_ring_ref);

			if (err)
				break;
		}
	}
	return err;
}

static int blkback_device_info(struct xenbus_device *xdev, void *out)
{
	return store_read_cfg(xdev, (noxs_cfg_vbd_t *) out);
}

static const struct xenbus_device_id xen_blkbk_ids[] = {
	{ "vbd" },
	{ "" }
};

static struct xenbus_driver xen_blkbk_driver = {
	.ids  = xen_blkbk_ids,
	.probe = xen_blkbk_probe,
	.remove = xen_blkbk_remove,
	.otherend_changed = frontend_changed,
	.device_info = blkback_device_info,
};

int xen_blkif_xenbus_init(void)
{
	return xenbus_register_backend(&xen_blkbk_driver);
}

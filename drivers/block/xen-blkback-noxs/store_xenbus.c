#include "common.h"
#include "store.h"

/* On the XenBus the max length of 'ring-ref%u'. */
#define RINGREF_NAME_LEN (20)


int store_write_init_info(struct xenbus_device *xdev)
{
	int err;

	err = xenbus_printf(XBT_NIL, xdev->nodename,
			    "feature-max-indirect-segments", "%u",
			    MAX_INDIRECT_SEGMENTS);
	if (err)
		dev_warn(&xdev->dev,
			 "writing %s/feature-max-indirect-segments (%d)",
			 xdev->nodename, err);

	/* Multi-queue: advertise how many queues are supported by us.*/
	err = xenbus_printf(XBT_NIL, xdev->nodename,
			    "multi-queue-max-queues", "%u", xenblk_max_queues);
	if (err)
		pr_warn("Error writing multi-queue-max-queues\n");

	err = xenbus_printf(XBT_NIL, xdev->nodename, "max-ring-page-order", "%u",
			    xen_blkif_max_ring_order);
	if (err)
		pr_warn("%s write out 'max-ring-page-order' failed\n", __func__);

	return 0;
}

int store_read_cfg(struct xenbus_device *xdev, void *cfg)
{
	return -1;
}

int store_watch_be(struct xenbus_device *xdev, struct xenbus_watch *watch, void (*cb)(struct xenbus_watch *watch, const char **vec, unsigned int len))//TODO
{
	return xenbus_watch_pathfmt(xdev, watch, cb,
			"%s/%s", xdev->nodename, "physical-device");
}

int store_read_be_node(struct xenbus_device *xdev, unsigned *major, unsigned *minor)
{
	return xenbus_scanf(XBT_NIL, xdev->nodename,
			"physical-device", "%x:%x", major, minor);
}

int store_read_be_devname(/*struct xenbus_device *dev,*/ struct xen_blkif *blkif, char *buf)//TODO check params
{
	char *devpath, *devname;
	struct xenbus_device *xdev = blkif->be->dev;

	devpath = xenbus_read(XBT_NIL, xdev->nodename, "dev", NULL);
	if (IS_ERR(devpath))
		return PTR_ERR(devpath);

	devname = strstr(devpath, "/dev/");
	if (devname != NULL)
		devname += strlen("/dev/");
	else
		devname  = devpath;

	snprintf(buf, TASK_COMM_LEN, "%d.%s", blkif->domid, devname);
	kfree(devpath);

	return 0;
}

int store_read_be_mode(struct xenbus_device *xdev, char **mode)
{
	*mode = xenbus_read(XBT_NIL, xdev->nodename, "mode", NULL);
	return 0;
}

int store_read_be_device_type(struct xenbus_device *xdev, char **device_type)
{
	*device_type = xenbus_read(XBT_NIL, xdev->otherend, "device-type", NULL);
	return 0;
}

int store_read_be_handle(struct xenbus_device *xdev, unsigned long *handle)
{
	return kstrtoul(strrchr(xdev->otherend, '/') + 1, 0, handle);
}


int store_write_be_feat_flush_cache(struct xenbus_device *xdev, struct xenbus_transaction xbt,
				  int state)
{
	int err;

	err = xenbus_printf(xbt, xdev->nodename, "feature-flush-cache",
				"%d", state);
	if (err)
		dev_warn(&xdev->dev, "writing feature-flush-cache (%d)", err);

	return err;
}

static void xen_blkbk_discard(struct xenbus_device *xdev, struct xenbus_transaction xbt,
		struct xen_blkif *blkif)
{
	int err;
	int state = 0, discard_enable;
	struct block_device *bdev = blkif->vbd.bdev;
	struct request_queue *q = bdev_get_queue(bdev);

	err = xenbus_scanf(XBT_NIL, xdev->nodename, "discard-enable", "%d",
			   &discard_enable);
	if (err == 1 && !discard_enable)
		return;

	if (blk_queue_discard(q)) {
		err = xenbus_printf(xbt, xdev->nodename,
			"discard-granularity", "%u",
			q->limits.discard_granularity);
		if (err) {
			dev_warn(&xdev->dev, "writing discard-granularity (%d)", err);
			return;
		}
		err = xenbus_printf(xbt, xdev->nodename,
			"discard-alignment", "%u",
			q->limits.discard_alignment);
		if (err) {
			dev_warn(&xdev->dev, "writing discard-alignment (%d)", err);
			return;
		}
		state = 1;
		/* Optional. */
		err = xenbus_printf(xbt, xdev->nodename,
				    "discard-secure", "%d",
				    blkif->vbd.discard_secure);
		if (err) {
			dev_warn(&xdev->dev, "writing discard-secure (%d)", err);
			return;
		}
	}
	err = xenbus_printf(xbt, xdev->nodename, "feature-discard",
			    "%d", state);
	if (err)
		dev_warn(&xdev->dev, "writing feature-discard (%d)", err);
}

int store_write_be_feat_barrier(struct xenbus_device *xdev, struct xenbus_transaction xbt,
				  int state)
{
	int err;

	err = xenbus_printf(xbt, xdev->nodename, "feature-barrier",
			    "%d", state);
	if (err)
		dev_warn(&xdev->dev, "writing feature-barrier (%d)", err);

	return err;
}


int store_write_provide_fe_info(struct xenbus_device *xdev, struct xen_blkif *blkif)
{
	struct xenbus_transaction xbt;
	int err;

	/* Supply the information about the device the frontend needs */
again:
	err = xenbus_transaction_start(&xbt);
	if (err) {
		xenbus_dev_fatal(xdev, err, "starting transaction");
		return err;
	}

	/* If we can't advertise it is OK. */
	store_write_be_feat_flush_cache(xdev, xbt, blkif->vbd.flush_support);

	xen_blkbk_discard(xdev, xbt, blkif);

	store_write_be_feat_barrier(xdev, xbt, blkif->vbd.flush_support);

	err = xenbus_printf(xbt, xdev->nodename, "feature-persistent", "%u", 1);
	if (err) {
		xenbus_dev_fatal(xdev, err, "writing %s/feature-persistent",
				 xdev->nodename);
		goto abort;
	}

	err = xenbus_printf(xbt, xdev->nodename, "sectors", "%llu",
			    (unsigned long long) vbd_sz(&blkif->vbd));
	if (err) {
		xenbus_dev_fatal(xdev, err, "writing %s/sectors",
				 xdev->nodename);
		goto abort;
	}

	/* FIXME: use a typename instead */
	err = xenbus_printf(xbt, xdev->nodename, "info", "%u",
			   blkif->vbd.type |
			    (blkif->vbd.readonly ? VDISK_READONLY : 0));
	if (err) {
		xenbus_dev_fatal(xdev, err, "writing %s/info",
				 xdev->nodename);
		goto abort;
	}
	err = xenbus_printf(xbt, xdev->nodename, "sector-size", "%lu",
			    (unsigned long)
			    bdev_logical_block_size(blkif->vbd.bdev));
	if (err) {
		xenbus_dev_fatal(xdev, err, "writing %s/sector-size",
				 xdev->nodename);
		goto abort;
	}
	err = xenbus_printf(xbt, xdev->nodename, "physical-sector-size", "%u",
			    bdev_physical_block_size(blkif->vbd.bdev));
	if (err)
		xenbus_dev_error(xdev, err, "writing %s/physical-sector-size",
				 xdev->nodename);

	err = xenbus_transaction_end(xbt, 0);
	if (err == -EAGAIN)
		goto again;
	if (err)
		xenbus_dev_fatal(xdev, err, "ending transaction");

	return err;

 abort:
	xenbus_transaction_end(xbt, 1);

	return err;
}


int store_read_fe_protocol(struct xenbus_device *xdev, char *protocol)
{
	return xenbus_scanf(XBT_NIL, xdev->otherend,
			"protocol", "%63s", protocol);
}

int store_read_fe_persistent(struct xenbus_device *xdev, unsigned int *pers_grants)
{
	return xenbus_scanf(XBT_NIL, xdev->otherend,
			"feature-persistent", "%u", pers_grants);
}

int store_read_num_queues(struct xenbus_device *xdev, unsigned int *num_queues)
{
	return xenbus_scanf(XBT_NIL, xdev->otherend,
			"multi-queue-num-queues", "%u", num_queues);
}

int store_write_sectors_num(struct xenbus_device *xdev, unsigned long long sectors)
{
	struct xenbus_transaction xbt;
	int err;

again:
	err = xenbus_transaction_start(&xbt);
	if (err) {
		pr_warn("Error starting transaction\n");
		return err;
	}
	err = xenbus_printf(xbt, xdev->nodename, "sectors", "%llu", sectors);
	if (err) {
		pr_warn("Error writing new size\n");
		goto abort;
	}
	/*
	 * Write the current state; we will use this to synchronize
	 * the front-end. If the current state is "connected" the
	 * front-end will get the new size information online.
	 */
	err = xenbus_printf(xbt, xdev->nodename, "state", "%d", xdev->state);
	if (err) {
		pr_warn("Error writing the state\n");
		goto abort;
	}

	err = xenbus_transaction_end(xbt, 0);
	if (err == -EAGAIN)
		goto again;
	if (err)
		pr_warn("Error ending transaction\n");
	return 0;
abort:
	xenbus_transaction_end(xbt, 1);
	return err;
}

int store_read_ring_page_order(struct xenbus_device *xdev, unsigned int *ring_page_order)
{
	return xenbus_scanf(XBT_NIL, xdev->otherend,
			"ring-page-order", "%u", ring_page_order);
}

int store_ring_ref_init(struct store_ring_ref* srref,
		struct xenbus_device *xdev, int ring_index)
{
	size_t xspathsize;
	const size_t xenstore_path_ext_size = 11; /* sufficient for "/queue-NNN" */

	if (ring_index < 0) {
		srref->store_address = kstrdup(xdev->otherend, GFP_KERNEL);//TODO do we need it?

	} else {
		xspathsize = strlen(xdev->otherend) + xenstore_path_ext_size;
		srref->store_address = kmalloc(xspathsize, GFP_KERNEL);
		if (!srref->store_address) {
			xenbus_dev_fatal(xdev, -ENOMEM, "reading ring references");
			return -ENOMEM;
		}

		memset(srref->store_address, 0, xspathsize);
		snprintf(srref->store_address, xspathsize, "%s/queue-%u", xdev->otherend, ring_index);
	}

	srref->index = ring_index;

	return 0;
}

void store_ring_ref_clear(struct store_ring_ref* srref)
{
	if (srref->store_address) {
		kfree(srref->store_address);
		srref->store_address = NULL;
	}
}

const char* store_ring_ref_str(const struct store_ring_ref* srref)
{
	return srref->store_address;
}

int store_read_event_channel(struct store_ring_ref* srref,
		unsigned int *evtchn)
{
	const char *dir = srref->store_address;

	return xenbus_scanf(XBT_NIL, dir, "event-channel", "%u", evtchn);
}

int store_read_ring_ref(struct store_ring_ref* srref,
		int ref_index, unsigned int *ring_ref)
{
	int err;
	const char *dir = srref->store_address;
	char ring_ref_name[RINGREF_NAME_LEN];

	if (ref_index < 0) {
		err = xenbus_scanf(XBT_NIL, dir,
				"ring-ref", "%u", &ring_ref[0]);

	} else {
		snprintf(ring_ref_name, RINGREF_NAME_LEN, "ring-ref%u", ref_index);
		err = xenbus_scanf(XBT_NIL, dir,
				ring_ref_name, "%u", &ring_ref[ref_index]);
	}

	return 0;
}

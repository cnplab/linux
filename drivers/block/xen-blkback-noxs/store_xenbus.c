#include "common.h"
#include "store.h"

/* On the XenBus the max length of 'ring-ref%u'. */
#define RINGREF_NAME_LEN (20)


int store_write_init_info(struct xenbus_device *dev)
{
	int err;

	err = xenbus_printf(XBT_NIL, dev->nodename,
			    "feature-max-indirect-segments", "%u",
			    MAX_INDIRECT_SEGMENTS);
	if (err)
		dev_warn(&dev->dev,
			 "writing %s/feature-max-indirect-segments (%d)",
			 dev->nodename, err);

	/* Multi-queue: advertise how many queues are supported by us.*/
	err = xenbus_printf(XBT_NIL, dev->nodename,
			    "multi-queue-max-queues", "%u", xenblk_max_queues);
	if (err)
		pr_warn("Error writing multi-queue-max-queues\n");

	err = xenbus_printf(XBT_NIL, dev->nodename, "max-ring-page-order", "%u",
			    xen_blkif_max_ring_order);
	if (err)
		pr_warn("%s write out 'max-ring-page-order' failed\n", __func__);

	return 0;
}

int store_watch_be(struct xenbus_device *dev, struct xenbus_watch *watch, void (*cb)(struct xenbus_watch *watch, const char **vec, unsigned int len))//TODO
{
	return xenbus_watch_pathfmt(dev, watch, cb,
			"%s/%s", dev->nodename, "physical-device");
}

int store_read_be_version(struct xenbus_device *dev, unsigned *major, unsigned *minor)
{
	return xenbus_scanf(XBT_NIL, dev->nodename,
			"physical-device", "%x:%x", major, minor);
}

int store_read_be_mode(struct xenbus_device *dev, char **mode)
{
	*mode = xenbus_read(XBT_NIL, dev->nodename, "mode", NULL);
	return 0;
}

int store_read_be_device_type(struct xenbus_device *dev, char **device_type)
{
	*device_type = xenbus_read(XBT_NIL, dev->otherend, "device-type", NULL);
	return 0;
}

int store_read_be_handle(struct xenbus_device *dev, unsigned long *handle)
{
	return kstrtoul(strrchr(dev->otherend, '/') + 1, 0, handle);
}


int store_write_be_feat_flush_cache(struct xenbus_device *dev, struct xenbus_transaction xbt,
				  int state)
{
	int err;

	err = xenbus_printf(xbt, dev->nodename, "feature-flush-cache",
				"%d", state);
	if (err)
		dev_warn(&dev->dev, "writing feature-flush-cache (%d)", err);

	return err;
}

static void xen_blkbk_discard(struct xenbus_device *dev, struct xenbus_transaction xbt,
		struct xen_blkif *blkif)
{
	int err;
	int state = 0, discard_enable;
	struct block_device *bdev = blkif->vbd.bdev;
	struct request_queue *q = bdev_get_queue(bdev);

	err = xenbus_scanf(XBT_NIL, dev->nodename, "discard-enable", "%d",
			   &discard_enable);
	if (err == 1 && !discard_enable)
		return;

	if (blk_queue_discard(q)) {
		err = xenbus_printf(xbt, dev->nodename,
			"discard-granularity", "%u",
			q->limits.discard_granularity);
		if (err) {
			dev_warn(&dev->dev, "writing discard-granularity (%d)", err);
			return;
		}
		err = xenbus_printf(xbt, dev->nodename,
			"discard-alignment", "%u",
			q->limits.discard_alignment);
		if (err) {
			dev_warn(&dev->dev, "writing discard-alignment (%d)", err);
			return;
		}
		state = 1;
		/* Optional. */
		err = xenbus_printf(xbt, dev->nodename,
				    "discard-secure", "%d",
				    blkif->vbd.discard_secure);
		if (err) {
			dev_warn(&dev->dev, "writing discard-secure (%d)", err);
			return;
		}
	}
	err = xenbus_printf(xbt, dev->nodename, "feature-discard",
			    "%d", state);
	if (err)
		dev_warn(&dev->dev, "writing feature-discard (%d)", err);
}

int store_write_be_feat_barrier(struct xenbus_device *dev, struct xenbus_transaction xbt,
				  int state)
{
	int err;

	err = xenbus_printf(xbt, dev->nodename, "feature-barrier",
			    "%d", state);
	if (err)
		dev_warn(&dev->dev, "writing feature-barrier (%d)", err);

	return err;
}


void store_write_provide_fe_info(struct xenbus_device *dev, struct xen_blkif *blkif)
{
	struct xenbus_transaction xbt;
	int err;

	/* Supply the information about the device the frontend needs */
again:
	err = xenbus_transaction_start(&xbt);
	if (err) {
		xenbus_dev_fatal(dev, err, "starting transaction");
		return;
	}

	/* If we can't advertise it is OK. */
	store_write_be_feat_flush_cache(dev, xbt, blkif->vbd.flush_support);

	xen_blkbk_discard(dev, xbt, blkif);

	store_write_be_feat_barrier(dev, xbt, blkif->vbd.flush_support);

	err = xenbus_printf(xbt, dev->nodename, "feature-persistent", "%u", 1);
	if (err) {
		xenbus_dev_fatal(dev, err, "writing %s/feature-persistent",
				 dev->nodename);
		goto abort;
	}

	err = xenbus_printf(xbt, dev->nodename, "sectors", "%llu",
			    (unsigned long long) vbd_sz(&blkif->vbd));
	if (err) {
		xenbus_dev_fatal(dev, err, "writing %s/sectors",
				 dev->nodename);
		goto abort;
	}

	/* FIXME: use a typename instead */
	err = xenbus_printf(xbt, dev->nodename, "info", "%u",
			   blkif->vbd.type |
			    (blkif->vbd.readonly ? VDISK_READONLY : 0));
	if (err) {
		xenbus_dev_fatal(dev, err, "writing %s/info",
				 dev->nodename);
		goto abort;
	}
	err = xenbus_printf(xbt, dev->nodename, "sector-size", "%lu",
			    (unsigned long)
			    bdev_logical_block_size(blkif->vbd.bdev));
	if (err) {
		xenbus_dev_fatal(dev, err, "writing %s/sector-size",
				 dev->nodename);
		goto abort;
	}
	err = xenbus_printf(xbt, dev->nodename, "physical-sector-size", "%u",
			    bdev_physical_block_size(blkif->vbd.bdev));
	if (err)
		xenbus_dev_error(dev, err, "writing %s/physical-sector-size",
				 dev->nodename);

	err = xenbus_transaction_end(xbt, 0);
	if (err == -EAGAIN)
		goto again;
	if (err)
		xenbus_dev_fatal(dev, err, "ending transaction");

	err = xenbus_switch_state(dev, XenbusStateConnected);
	if (err)
		xenbus_dev_fatal(dev, err, "%s: switching to Connected state",
				 dev->nodename);

	return;

 abort:
	xenbus_transaction_end(xbt, 1);
}


int store_read_fe_protocol(struct xenbus_device *dev, char *protocol)
{
	return xenbus_scanf(XBT_NIL, dev->otherend,
			"protocol", "%63s", protocol);
}

int store_read_fe_persistent(struct xenbus_device *dev, unsigned int *pers_grants)
{
	return xenbus_scanf(XBT_NIL, dev->otherend,
			"feature-persistent", "%u", pers_grants);
}

int store_read_num_queues(struct xenbus_device *dev, unsigned int *requested_num_queues)
{
	return xenbus_scanf(XBT_NIL, dev->otherend,
			"multi-queue-num-queues", "%u", requested_num_queues);
}

int store_ring_ref_init(struct xenbus_device *dev, struct store_ring_ref* ref, int ring_index)
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

void store_ring_ref_clear(struct store_ring_ref* ref)
{
	if (ref->store_address) {
		kfree(ref->store_address);
	}
}

const char* store_ring_ref_str(const struct store_ring_ref* ref)
{
	return ref->store_address;
}

int store_read_event_channel(struct xenbus_device *dev, struct store_ring_ref* ref, unsigned int *evtchn)
{
	int err;
	const char *dir = ref->store_address;

	err = xenbus_scanf(XBT_NIL, dir,
			"event-channel", "%u", evtchn);
	if (err != 1) {
		err = -EINVAL;
		xenbus_dev_fatal(dev, err, "reading %s/event-channel", dir);
		return err;
	}

	return err;
}

int store_read_ring_page_order(struct xenbus_device *dev, struct store_ring_ref* ref, unsigned int *ring_page_order)
{
	return xenbus_scanf(XBT_NIL, dev->otherend,
			"ring-page-order", "%u", ring_page_order);
}

int store_read_ring_ref(struct xenbus_device *dev, struct store_ring_ref* ref, int ref_index, unsigned int *ring_ref)
{
	int err;
	const char *dir = ref->store_address;
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

int store_write_sectors_num(struct xenbus_device *dev, unsigned long long sectors)
{
	struct xenbus_transaction xbt;
	int err;

again:
	err = xenbus_transaction_start(&xbt);
	if (err) {
		pr_warn("Error starting transaction\n");
		return err;
	}
	err = xenbus_printf(xbt, dev->nodename, "sectors", "%llu", sectors);
	if (err) {
		pr_warn("Error writing new size\n");
		goto abort;
	}
	/*
	 * Write the current state; we will use this to synchronize
	 * the front-end. If the current state is "connected" the
	 * front-end will get the new size information online.
	 */
	err = xenbus_printf(xbt, dev->nodename, "state", "%d", dev->state);
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

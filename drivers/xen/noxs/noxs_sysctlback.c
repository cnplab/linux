#define pr_fmt(fmt) KBUILD_MODNAME ":%s: " fmt, __func__
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/device.h>
#include <linux/slab.h>

#include <xen/noxs.h>


struct backend_info {
	struct xenbus_device *dev;
	wait_queue_head_t ack_waitq;
};

//TODO move
static
int store_write_init_info(struct xenbus_device *xdev)
{
	noxs_sysctl_ctrl_page_t *page;

	page = xdev->ctrl_page;
	page->hdr.be_state = XenbusStateInitialising;
	page->hdr.fe_state = XenbusStateInitialising;

	return 0;
}


static int sysctlback_remove(struct xenbus_device *dev)
{
	struct backend_info *be = dev_get_drvdata(&dev->dev);

	kfree(be);
	dev_set_drvdata(&dev->dev, NULL);
	return 0;
}

/**
 * Entry point to this code when a new device is created.  Allocate the basic
 * structures and switch to InitWait.
 */
static int sysctlback_probe(struct xenbus_device *dev,
			 const struct xenbus_device_id *id)
{
	int err;
	struct backend_info *be = kzalloc(sizeof(struct backend_info),
					  GFP_KERNEL);
	if (!be) {
		xenbus_dev_fatal(dev, -ENOMEM,
				 "allocating backend structure");
		return -ENOMEM;
	}

	be->dev = dev;
	dev_set_drvdata(&dev->dev, be);

	init_waitqueue_head(&be->ack_waitq);

	err = store_write_init_info(dev);
	if (err)
		goto fail;

	err = xenbus_switch_state(dev, XenbusStateInitWait);
	if (err)
		goto fail;

	return 0;

fail:
	pr_debug("failed\n");
	sysctlback_remove(dev);
	return err;
}

/**
 * Callback received when the frontend's state changes.
 */
static void frontend_changed(struct xenbus_device *dev,
			     enum xenbus_state frontend_state)
{
	struct backend_info *be = dev_get_drvdata(&dev->dev);

	pr_debug("%s %p %s\n", __func__, dev, xenbus_strstate(frontend_state));

	wake_up(&be->ack_waitq);

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

		xenbus_switch_state(dev, XenbusStateConnected);
		break;

	case XenbusStateClosing:
		xenbus_switch_state(dev, XenbusStateClosing);
		break;

	case XenbusStateClosed:
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

enum noxs_shutdown_type {
	noxs_sd_none = 0 ,
	noxs_sd_poweroff ,
	noxs_sd_suspend
};

static int sysctlback_command(struct xenbus_device *dev,
		unsigned long cmd, void *arg)
{
	struct backend_info *be = dev_get_drvdata(&dev->dev);
	noxs_sysctl_ctrl_page_t *page;
	enum noxs_shutdown_type *sd_type;//TODO no user stuff here
	int ret;

	if (arg == NULL)
		return -EINVAL;

	sd_type = (enum noxs_shutdown_type *) arg;

	page = dev->ctrl_page;

	if (*sd_type == noxs_sd_poweroff)
		page->bits.poweroff = 1;
	else if (*sd_type == noxs_sd_suspend)
		page->bits.suspend = 1;
	else
		return -EINVAL;

	noxs_notify_otherend(dev);

	while (page->hdr.fe_state < XenbusStateClosed)
		ret = wait_event_interruptible(be->ack_waitq, page->hdr.fe_state == XenbusStateClosed);

	return ret;
}

static const struct xenbus_device_id sysctlback_ids[] = {
	{ "sysctl" },
	{ "" }
};

static struct xenbus_driver sysctlback_driver = {
	.ids = sysctlback_ids,
	.probe = sysctlback_probe,
	.remove = sysctlback_remove,
	.otherend_changed = frontend_changed,
	.driver_cmd = sysctlback_command
};

static int __init sysctlback_init(void)
{
	if (!xen_domain())
		return -ENODEV;

	return xenbus_register_backend(&sysctlback_driver);
}
module_init(sysctlback_init);

static void __exit sysctlback_fini(void)
{
	return xenbus_unregister_driver(&sysctlback_driver);
}
module_exit(sysctlback_fini);

MODULE_LICENSE("Dual BSD/GPL");
MODULE_ALIAS("xen-backend:sysctl");

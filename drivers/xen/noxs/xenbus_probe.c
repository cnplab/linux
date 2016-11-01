/******************************************************************************
 * Talks to Xen Store to figure out what devices we have.
 *
 * Copyright (C) 2005 Rusty Russell, IBM Corporation
 * Copyright (C) 2005 Mike Wray, Hewlett-Packard
 * Copyright (C) 2005, 2006 XenSource Ltd
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License version 2
 * as published by the Free Software Foundation; or, when distributed
 * separately from the Linux kernel or incorporated into other
 * software packages, subject to the following license:
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this source file (the "Software"), to deal in the Software without
 * restriction, including without limitation the rights to use, copy, modify,
 * merge, publish, distribute, sublicense, and/or sell copies of the Software,
 * and to permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#define DPRINTK(fmt, args...)				\
	pr_debug("xenbus_probe (%s:%d) " fmt ".\n",	\
		 __func__, __LINE__, ##args)

#include <linux/kernel.h>
#include <linux/err.h>
#include <linux/string.h>
#include <linux/ctype.h>
#include <linux/fcntl.h>
#include <linux/mm.h>
#include <linux/proc_fs.h>
#include <linux/notifier.h>
#include <linux/kthread.h>
#include <linux/mutex.h>
#include <linux/io.h>
#include <linux/slab.h>
#include <linux/module.h>

#include <asm/page.h>
#include <asm/pgtable.h>
#include <asm/xen/hypervisor.h>

#include <xen/xen.h>
#include <xen/noxs.h>
#include <xen/events.h>
#include <xen/xen-ops.h>
#include <xen/page.h>

#include <xen/hvm.h>

#include "noxs_comms.h"
#include "xenbus_probe.h"


enum xenstore_init noxs_domain_type;
EXPORT_SYMBOL_GPL(noxs_domain_type);

static BLOCKING_NOTIFIER_HEAD(xenstore_chain);

/* If something in array of ids matches this device, return it. */
static const struct xenbus_device_id *
match_device(const struct xenbus_device_id *arr, struct xenbus_device *dev)
{
	for (; *arr->devicetype != '\0'; arr++) {
		if (!strcmp(arr->devicetype, noxs_dev_type_to_str(dev->devicetype)))
			return arr;
	}
	return NULL;
}

int xenbus_match(struct device *_dev, struct device_driver *_drv)
{
	struct xenbus_driver *drv = to_xenbus_driver(_drv);

	if (!drv->ids)
		return 0;

	return match_device(drv->ids, to_xenbus_device(_dev)) != NULL;
}
EXPORT_SYMBOL_GPL(xenbus_match);


static void free_otherend_details(struct xenbus_device *dev)
{
#ifndef CONFIG_XEN_BACKEND_NOXS
	kfree(dev->otherend);
	dev->otherend = NULL;
#endif
}


static void free_otherend_watch(struct xenbus_device *dev)
{
#ifdef CONFIG_XEN_BACKEND_NOXS
	noxs_comm_free_otherend_watch(dev);
#else
	if (dev->otherend_watch.node) {
		unregister_xenbus_watch(&dev->otherend_watch);
		kfree(dev->otherend_watch.node);
		dev->otherend_watch.node = NULL;
	}
#endif
}


#ifndef CONFIG_XEN_BACKEND_NOXS
static int talk_to_otherend(struct xenbus_device *dev)
{
	struct xenbus_driver *drv = to_xenbus_driver(dev->dev.driver);

	free_otherend_watch(dev);
	free_otherend_details(dev);

	return drv->read_otherend_details(dev);
}
#endif



static int watch_otherend(struct xenbus_device *dev)
{
	struct xen_bus_type *bus =
		container_of(dev->dev.bus, struct xen_bus_type, bus);

	return noxs_comm_watch_otherend(dev, bus->otherend_changed);
}


int xenbus_read_otherend_details(struct xenbus_device *xendev,
				 char *id_node, char *path_node)//this reads domid and path
{
#ifndef CONFIG_XEN_BACKEND_NOXS
	int err = xenbus_gather(XBT_NIL, xendev->nodename,
				id_node, "%i", &xendev->otherend_id,
				path_node, NULL, &xendev->otherend,
				NULL);
	if (err) {
		xenbus_dev_fatal(xendev, err,
				 "reading other end details from %s",
				 xendev->nodename);
		return err;
	}
	if (strlen(xendev->otherend) == 0 ||
	    !xenbus_exists(XBT_NIL, xendev->otherend, "")) {
		xenbus_dev_fatal(xendev, -ENOENT,
				 "unable to read other end from %s.  "
				 "missing or inaccessible.",
				 xendev->nodename);
		free_otherend_details(xendev);
		return -ENOENT;
	}
#endif

	return 0;
}
EXPORT_SYMBOL_GPL(xenbus_read_otherend_details);

void xenbus_otherend_changed(struct xenbus_watch *watch,
			     int ignore_on_shutdown)
{
	struct xenbus_device *dev =
		container_of(watch, struct xenbus_device, otherend_watch);
	struct xenbus_driver *drv = to_xenbus_driver(dev->dev.driver);
	noxs_ctrl_hdr_t *ctrl_hdr = dev->ctrl_page;
	enum xenbus_state state = ctrl_hdr->fe_state;//TODO generalization as otherend state

	/*
	 * Ignore xenbus transitions during shutdown. This prevents us doing
	 * work that can fail e.g., when the rootfs is gone.
	 */
	if (system_state > SYSTEM_RUNNING) {
		if (ignore_on_shutdown && (state == XenbusStateClosing))
			xenbus_frontend_closed(dev);
		return;
	}

	if (drv->otherend_changed)
		drv->otherend_changed(dev, state);
}
EXPORT_SYMBOL_GPL(xenbus_otherend_changed);

int xenbus_dev_probe(struct device *_dev)
{
	struct xenbus_device *dev = to_xenbus_device(_dev);
	struct xenbus_driver *drv = to_xenbus_driver(_dev->driver);
	const struct xenbus_device_id *id;
	int err;

	DPRINTK("%s", to_xenbus_name(dev));

	if (!drv->probe) {
		err = -ENODEV;
		goto fail;
	}

	id = match_device(drv->ids, dev);
	if (!id) {
		err = -ENODEV;
		goto fail;
	}

#ifndef CONFIG_XEN_BACKEND_NOXS
	err = talk_to_otherend(dev);
	if (err) {
		dev_warn(&dev->dev, "talk_to_otherend on %s failed.\n",
			 dev->nodename);
		return err;
	}
#endif

	err = drv->probe(dev, id);
	if (err)
		goto fail;

	err = watch_otherend(dev);
	if (err) {
		dev_warn(&dev->dev, "watch_otherend on %s failed.\n",
		       to_xenbus_name(dev));
		return err;
	}

	return 0;
fail:
	xenbus_dev_error(dev, err, "xenbus_dev_probe on %s", to_xenbus_name(dev));
	xenbus_switch_state(dev, XenbusStateClosed);
	return err;
}
EXPORT_SYMBOL_GPL(xenbus_dev_probe);

int xenbus_dev_remove(struct device *_dev)
{
	struct xenbus_device *dev = to_xenbus_device(_dev);
	struct xenbus_driver *drv = to_xenbus_driver(_dev->driver);

	DPRINTK("%s", to_xenbus_name(dev));

	free_otherend_watch(dev);

	if (drv->remove)
		drv->remove(dev);

	free_otherend_details(dev);

	xenbus_switch_state(dev, XenbusStateClosed);
	return 0;
}
EXPORT_SYMBOL_GPL(xenbus_dev_remove);

void xenbus_dev_shutdown(struct device *_dev)
{
	struct xenbus_device *dev = to_xenbus_device(_dev);
	unsigned long timeout = 5*HZ;

	DPRINTK("%s", to_xenbus_name(dev));

	get_device(&dev->dev);
	if (dev->state != XenbusStateConnected) {
		pr_info("%s: %s: %s != Connected, skipping\n",
			__func__, to_xenbus_name(dev), xenbus_strstate(dev->state));
		goto out;
	}
	xenbus_switch_state(dev, XenbusStateClosing);
	timeout = wait_for_completion_timeout(&dev->down, timeout);
	if (!timeout)
		pr_info("%s: %s timeout closing device\n",
			__func__, to_xenbus_name(dev));
 out:
	put_device(&dev->dev);
}
EXPORT_SYMBOL_GPL(xenbus_dev_shutdown);

int xenbus_register_driver_common(struct xenbus_driver *drv,
				  struct xen_bus_type *bus,
				  struct module *owner, const char *mod_name)
{
	drv->driver.name = drv->name ? drv->name : drv->ids[0].devicetype;
	drv->driver.bus = &bus->bus;
	drv->driver.owner = owner;
	drv->driver.mod_name = mod_name;

	return driver_register(&drv->driver);
}
EXPORT_SYMBOL_GPL(xenbus_register_driver_common);

void xenbus_unregister_driver(struct xenbus_driver *drv)
{
	driver_unregister(&drv->driver);
}
EXPORT_SYMBOL_GPL(xenbus_unregister_driver);

struct noxs_find_info {
	struct xenbus_device *dev;
	noxs_dev_key_t key;//TODO make pointer
};

static int cmp_dev(struct device *dev, void *data)
{
	struct xenbus_device *xendev = to_xenbus_device(dev);
	struct noxs_find_info *info = data;

	if (xendev->devicetype == info->key.type &&
	    xendev->otherend_id == info->key.fe_id &&
	    /*0  TODO  == info->key.be_id &&*/
	    xendev->id == info->key.devid) {
		info->dev = xendev;
		get_device(dev);
		return 1;
	}
	return 0;
}

#if 0//TODO do we still need this?
static struct xenbus_device *xenbus_device_find(noxs_dev_key_t *key,
						struct bus_type *bus)
{
	struct noxs_find_info info = { .dev = NULL, .key = *key };

	bus_for_each_dev(bus, NULL, &info, cmp_dev);
	return info.dev;
}
#endif

struct noxs_query_info {
	noxs_dev_key_t key;
	uint32_t count;
	struct xenbus_device *devs[NOXS_DEV_COUNT_MAX];
};

static int cmp_dev_query(struct device *dev, void *data)
{
	struct xenbus_device *xendev = to_xenbus_device(dev);
	struct noxs_query_info *info = data;

	printk("query fe_id=%d, type=%d\n", xendev->otherend_id, xendev->devicetype);

	if (xendev->otherend_id == info->key.fe_id && 0 /*TODO*/ == info->key.be_id && xendev->devicetype == info->key.type) {//TODO type&co
		info->devs[info->count++] = xendev;
		return 1;
	}
	return 0;
}

#if 0//TODO remove
static int cleanup_dev(struct device *dev, void *data)
{
	struct xenbus_device *xendev = to_xenbus_device(dev);
	struct noxs_find_info *info = data;
	int len = strlen(info->nodename);

	DPRINTK("%s", info->nodename);

	/* Match the info->nodename path, or any subdirectory of that path. */
	if (strncmp(xendev->nodename, info->nodename, len))
		return 0;

	/* If the node name is longer, ensure it really is a subdirectory. */
	if ((strlen(xendev->nodename) > len) && (xendev->nodename[len] != '/'))
		return 0;

	info->dev = xendev;
	get_device(dev);
	return 1;
}
#endif

static void xenbus_cleanup_devices(noxs_dev_key_t *key, struct bus_type *bus)
{
	struct noxs_find_info info = { .dev = NULL, .key = *key };

	do {
		info.dev = NULL;
		bus_for_each_dev(bus, NULL, &info, cmp_dev);
		if (info.dev) {
			device_unregister(&info.dev->dev);
			put_device(&info.dev->dev);
		}
	} while (info.dev);
}

static void xenbus_dev_release(struct device *dev)
{
	struct xenbus_device *xdev;

	if (dev) {
		xdev = to_xenbus_device(dev);
		noxs_comm_free(xdev);
		kfree(xdev);
	}
}

#if 1//ndef CONFIG_XEN_BACKEND_NOXS//TODO what are those?
static ssize_t nodename_show(struct device *dev,
			     struct device_attribute *attr, char *buf)
{
	return sprintf(buf, "%s\n", to_xenbus_name(to_xenbus_device(dev)));
}
static DEVICE_ATTR_RO(nodename);

static ssize_t devtype_show(struct device *dev,
			    struct device_attribute *attr, char *buf)
{
	return sprintf(buf, "%s\n", noxs_dev_type_to_str(to_xenbus_device(dev)->devicetype));
}
static DEVICE_ATTR_RO(devtype);

static ssize_t modalias_show(struct device *dev,
			     struct device_attribute *attr, char *buf)
{
	return sprintf(buf, "%s:%s\n", dev->bus->name,
		       noxs_dev_type_to_str(to_xenbus_device(dev)->devicetype));
}
static DEVICE_ATTR_RO(modalias);

static struct attribute *xenbus_dev_attrs[] = {
	&dev_attr_nodename.attr,
	&dev_attr_devtype.attr,
	&dev_attr_modalias.attr,
	NULL,
};

static const struct attribute_group xenbus_dev_group = {
	.attrs = xenbus_dev_attrs,
};
#endif

const struct attribute_group *xenbus_dev_groups[] = {
	&xenbus_dev_group,
	NULL,
};
EXPORT_SYMBOL_GPL(xenbus_dev_groups);

int xenbus_probe_node(struct xen_bus_type *bus,
		      noxs_dev_key_t* key, void *cfg,//TODO rename noxs_
		      struct xenbus_device **out_xdev)
{
	char devname[XEN_BUS_ID_SIZE];
	int err;
	struct xenbus_device *xendev;

	xendev = kzalloc(sizeof(*xendev), GFP_KERNEL);
	if (!xendev)
		return -ENOMEM;

	xendev->otherend_id = key->fe_id;
	xendev->state = XenbusStateInitialising;
	xendev->devicetype = key->type;
	init_completion(&xendev->down);

	xendev->dev.bus = &bus->bus;
	xendev->dev.release = xenbus_dev_release;

	err = noxs_comm_init(xendev);
	if (err)
		goto fail;

	err = bus->get_bus_id(devname, key);
	if (err)
		goto fail;

	dev_set_name(&xendev->dev, "%s", devname);

	xendev->dev_cfg = cfg;//TODO hackish

	printk("registering\n");

	/* Register with generic device framework. */
	err = device_register(&xendev->dev);
	if (err)
		goto fail;

	printk("registered\n");

	*out_xdev = xendev;

	return 0;
fail:
	kfree(xendev);
	return err;
}
EXPORT_SYMBOL_GPL(xenbus_probe_node);

#ifndef CONFIG_XEN_BACKEND_NOXS
static int xenbus_probe_device_type(struct xen_bus_type *bus, const char *type)
{
	int err = 0;
	char **dir;
	unsigned int dir_n = 0;
	int i;

	dir = xenbus_directory(XBT_NIL, bus->root, type, &dir_n);
	if (IS_ERR(dir))
		return PTR_ERR(dir);

	for (i = 0; i < dir_n; i++) {
		err = bus->probe(bus, type, dir[i]);
		if (err)
			break;
	}

	kfree(dir);
	return err;
}
#else
static int xenbus_probe_device_type(struct xen_bus_type *bus, enum noxs_dev_type type)
{
#if 0//TODO
	int err = 0;
	char **dir;
	unsigned int dir_n = 0;
	int i;

	dir = xenbus_directory(XBT_NIL, bus->root, type, &dir_n);
	if (IS_ERR(dir))
		return PTR_ERR(dir);

	for (i = 0; i < dir_n; i++) {
		err = bus->probe(bus, type, dir[i]);
		if (err)
			break;
	}

	kfree(dir);
	return err;
#endif
	return 0;
}
#endif

int xenbus_probe_devices(struct xen_bus_type *bus)
{
#ifndef CONFIG_XEN_BACKEND_NOXS
	int err = 0;
	char **dir;
	unsigned int i, dir_n;

	dir = xenbus_directory(XBT_NIL, bus->root, "", &dir_n);
	if (IS_ERR(dir))
		return PTR_ERR(dir);

	for (i = 0; i < dir_n; i++) {
		err = xenbus_probe_device_type(bus, dir[i]);
		if (err)
			break;
	}

	kfree(dir);
	return err;
#else
	int err = 0;
	enum noxs_dev_type types[] = { noxs_dev_console, noxs_dev_vif };
	unsigned int i;

	for (i = 0; i < sizeof(types) / sizeof(enum noxs_dev_type); i++) {
		err = xenbus_probe_device_type(bus, types[i]);
		if (err)
			break;
	}

	return err;
#endif
}
EXPORT_SYMBOL_GPL(xenbus_probe_devices);

struct xenbus_device *xenbus_dev_create(noxs_dev_key_t *key, void *cfg, struct xen_bus_type *bus)//TODO rename
{
	struct xenbus_device *xdev;

	xenbus_probe_node(bus, key, cfg, &xdev);//TODO check return
	return xdev;
}
EXPORT_SYMBOL_GPL(xenbus_dev_create);//TODO static

void xenbus_dev_destroy(noxs_dev_key_t *key, struct xen_bus_type *bus)//TODO rename
{
#if 0
	struct xenbus_device *xdev;

	xdev = xenbus_device_find(key, &bus->bus);
	if (!xdev)
		printk("device not found\n");//TODO

	put_device(&xdev->dev);
#else
	xenbus_cleanup_devices(key, &bus->bus);
#endif
}
EXPORT_SYMBOL_GPL(xenbus_dev_destroy);//TODO static

int xenbus_dev_suspend(struct device *dev)
{
	int err = 0;
	struct xenbus_driver *drv;
	struct xenbus_device *xdev
		= container_of(dev, struct xenbus_device, dev);

	DPRINTK("%s", to_xenbus_name(xdev));

	if (dev->driver == NULL)
		return 0;
	drv = to_xenbus_driver(dev->driver);
	if (drv->suspend)
		err = drv->suspend(xdev);
	if (err)
		pr_warn("suspend %s failed: %i\n", dev_name(dev), err);
	return 0;
}
EXPORT_SYMBOL_GPL(xenbus_dev_suspend);

int xenbus_dev_resume(struct device *dev)
{
	int err;
	struct xenbus_driver *drv;
	struct xenbus_device *xdev
		= container_of(dev, struct xenbus_device, dev);

	DPRINTK("%s", to_xenbus_name(xdev));

	if (dev->driver == NULL)
		return 0;
	drv = to_xenbus_driver(dev->driver);
#ifndef CONFIG_XEN_BACKEND_NOXS//TODO
	err = talk_to_otherend(xdev);
	if (err) {
		pr_warn("resume (talk_to_otherend) %s failed: %i\n",
			dev_name(dev), err);
		return err;
	}
#endif

	xdev->state = XenbusStateInitialising;

	if (drv->resume) {
		err = drv->resume(xdev);
		if (err) {
			pr_warn("resume %s failed: %i\n", dev_name(dev), err);
			return err;
		}
	}

	err = watch_otherend(xdev);
	if (err) {
		pr_warn("resume (watch_otherend) %s failed: %d.\n",
			dev_name(dev), err);
		return err;
	}

	return 0;
}
EXPORT_SYMBOL_GPL(xenbus_dev_resume);

int xenbus_dev_cancel(struct device *dev)
{
	/* Do nothing */
	DPRINTK("cancel");
	return 0;
}
EXPORT_SYMBOL_GPL(xenbus_dev_cancel);

void xenbus_dev_list(noxs_dev_key_t *key, struct xen_bus_type *bus, uint32_t *out_num, noxs_dev_id_t out_ids[])//TODO rename
{
	int i;
	struct noxs_query_info info = { .key = *key, .count = 0 };

	bus_for_each_dev(&bus->bus, NULL, &info, cmp_dev_query);

	for (i = 0; i < info.count; i++) {
		out_ids[i] = info.devs[i]->id;
	}
	*out_num = info.count;
}
EXPORT_SYMBOL_GPL(xenbus_dev_list);//TODO static

void xenbus_guest_close(domid_t domid, struct xen_bus_type *bus)//TODO rename
{
	struct xenbus_driver *drv;
	struct xenbus_device *xdev;
	struct noxs_query_info info = { .count = 0 };

	info.key.be_id = 0;//TODO
	info.key.fe_id = domid;
	info.key.type = noxs_dev_sysctl;

	bus_for_each_dev(&bus->bus, NULL, &info, cmp_dev_query);
	if (info.count != 1) {
		DPRINTK("No sysctl device for domid=%d\n", domid);
		return;
	}

	xdev = info.devs[0];
	drv = to_xenbus_driver(xdev->dev.driver);

	if (drv->driver_cmd)
		drv->driver_cmd(xdev, 0, NULL);
}
EXPORT_SYMBOL_GPL(xenbus_guest_close);//TODO static


static const char* noxs_dev_type_names[] = {
		"<none>",
		"sysctl",
		"console",
		"vif"
};

const char* noxs_dev_type_to_str(noxs_dev_type_t type)
{
	return noxs_dev_type_names[type];
}


/* A flag to determine if xenstored is 'ready' (i.e. has started) */
int xenstored_ready;


int register_xenstore_notifier(struct notifier_block *nb)
{
	int ret = 0;

	if (xenstored_ready > 0)
		ret = nb->notifier_call(nb, 0, NULL);
	else
		blocking_notifier_chain_register(&xenstore_chain, nb);

	return ret;
}
EXPORT_SYMBOL_GPL(register_xenstore_notifier);

void unregister_xenstore_notifier(struct notifier_block *nb)
{
	blocking_notifier_chain_unregister(&xenstore_chain, nb);
}
EXPORT_SYMBOL_GPL(unregister_xenstore_notifier);

void noxs_probe(struct work_struct *unused)//TODO whats this?
{
	xenstored_ready = 1;

	/* Notify others that xenstore is up */
	blocking_notifier_call_chain(&xenstore_chain, 0, NULL);
}
EXPORT_SYMBOL_GPL(noxs_probe);

static int __init xenbus_probe_initcall(void)
{
	if (!xen_domain())
		return -ENODEV;

	if (xen_initial_domain() || xen_hvm_domain())
		return 0;

	noxs_probe(NULL);
	return 0;
}

device_initcall(xenbus_probe_initcall);

static int xenbus_resume_cb(struct notifier_block *nb,
			    unsigned long action, void *data)
{
	int err = 0;

#ifndef CONFIG_XEN_BACKEND_NOXS
	if (xen_hvm_domain()) {
		uint64_t v = 0;

		err = hvm_get_parameter(HVM_PARAM_STORE_EVTCHN, &v);
		if (!err && v)
			xen_store_evtchn = v;
		else
			pr_warn("Cannot update xenstore event channel: %d\n",
				err);
	} else
		xen_store_evtchn = xen_start_info->store_evtchn;

#endif
	return err;
}

static struct notifier_block xenbus_resume_nb = {
	.notifier_call = xenbus_resume_cb,
};

static int __init noxs_init(void)
{
	noxs_domain_type = XS_UNKNOWN;

	if (!xen_domain())
		return -ENODEV;

	xenbus_ring_ops_init();

	if (xen_pv_domain())
		noxs_domain_type = XS_PV;
	if (xen_hvm_domain())
		noxs_domain_type = XS_HVM;
	if (xen_hvm_domain() && xen_initial_domain())
		noxs_domain_type = XS_LOCAL;
	if (xen_pv_domain() && !xen_start_info->store_evtchn)
		noxs_domain_type = XS_LOCAL;
	if (xen_pv_domain() && xen_start_info->store_evtchn)
		xenstored_ready = 1;

	if ((noxs_domain_type != XS_LOCAL) &&
	    (noxs_domain_type != XS_UNKNOWN))
		xen_resume_notifier_register(&xenbus_resume_nb);

	return 0;
}

postcore_initcall(noxs_init);

MODULE_LICENSE("GPL");

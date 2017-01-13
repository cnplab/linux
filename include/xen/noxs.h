/******************************************************************************
 * xenbus.h
 *
 * Talks to Xen Store to figure out what devices we have.
 *
 * Copyright (C) 2005 Rusty Russell, IBM Corporation
 * Copyright (C) 2005 XenSource Ltd.
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

#ifndef _XEN_XENBUS_H
#define _XEN_XENBUS_H

#include <linux/device.h>
#include <linux/notifier.h>
#include <linux/mutex.h>
#include <linux/export.h>
#include <linux/completion.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <xen/interface/xen.h>
#include <xen/interface/grant_table.h>
#include <xen/interface/io/xenbus.h>
#include <xen/interface/noxs.h>
#include <xen/interface/io/xs_wire.h>

#define CONFIG_XEN_BACKEND_NOXS 1
#if 1
//TODO temporary: to avoid multiple definition link error
#define xenbus_alloc_evtchn noxs_alloc_evtchn
#define xenbus_dev_error noxs_dev_error
#define xenbus_dev_fatal noxs_dev_fatal
#define xenbus_dev_is_online noxs_dev_is_online
#define xenbus_free_evtchn noxs_free_evtchn
#define xenbus_frontend_closed noxs_frontend_closed
#define xenbus_grant_ring noxs_grant_ring
#define xenbus_map_ring noxs_map_ring
#define xenbus_map_ring_valloc noxs_map_ring_valloc
#define xenbus_read_driver_state noxs_read_driver_state
#define xenbus_ring_ops_init noxs_ring_ops_init
#define xenbus_strstate noxs_strstate
#define xenbus_switch_state noxs_switch_state
#define xenbus_unmap_ring noxs_unmap_ring
#define xenbus_unmap_ring_vfree noxs_unmap_ring_vfree
#define xenbus_watch_path noxs_watch_path
#define xenbus_watch_pathfmt noxs_watch_pathfmt

#define noxs_domain_type noxs_xen_store_domain_type
#define xenstored_ready noxs_xenstored_ready
#define xen_store_evtchn noxs_xen_store_evtchn
#define xen_store_interface noxs_xen_store_interface

#define register_xenbus_watch noxs_register_xenbus_watch
#define unregister_xenbus_watch noxs_unregister_xenbus_watch
#define xenbus_dev_request_and_reply noxs_xenbus_dev_request_and_reply
#define xenbus_directory noxs_xenbus_directory
#define xenbus_exists noxs_xenbus_exists
#define xenbus_gather noxs_xenbus_gather
#define xenbus_mkdir noxs_xenbus_mkdir
#define xenbus_printf noxs_xenbus_printf
#define xenbus_read noxs_xenbus_read
#define xenbus_rm noxs_xenbus_rm
#define xenbus_scanf noxs_xenbus_scanf
#define xenbus_transaction_end noxs_xenbus_transaction_end
#define xenbus_transaction_start noxs_xenbus_transaction_start
#define xenbus_write noxs_xenbus_write
#define xs_init noxs_xs_init
#define xs_resume noxs_xs_resume
#define xs_suspend noxs_xs_suspend
#define xs_suspend_cancel noxs_xs_suspend_cancel

#define xenbus_dev_changed noxs_xenbus_dev_changed
#define xenbus_dev_groups noxs_xenbus_dev_groups
#define xenbus_dev_probe noxs_xenbus_dev_probe
#define xenbus_dev_remove noxs_xenbus_dev_remove
#define xenbus_dev_shutdown noxs_xenbus_dev_shutdown
#define xenbus_match noxs_xenbus_match
#define xenbus_otherend_changed noxs_xenbus_otherend_changed
#define xenbus_probe_devices noxs_xenbus_probe_devices
#define xenbus_probe_node noxs_xenbus_probe_node
#define xenbus_read_otherend_details noxs_xenbus_read_otherend_details
#define xenbus_register_driver_common noxs_xenbus_register_driver_common
#define xenbus_unregister_driver noxs_xenbus_unregister_driver

#define register_xenstore_notifier noxs_register_xenstore_notifier
#define unregister_xenstore_notifier noxs_unregister_xenstore_notifier
#define xenbus_dev_cancel noxs_xenbus_dev_cancel
#define xenbus_dev_resume noxs_xenbus_dev_resume
#define xenbus_dev_suspend noxs_xenbus_dev_suspend
#endif


#define XENBUS_MAX_RING_GRANT_ORDER 4
#define XENBUS_MAX_RING_GRANTS      (1U << XENBUS_MAX_RING_GRANT_ORDER)
#define INVALID_GRANT_HANDLE       (~0U)


enum noxs_dev_query_type {
	noxs_dev_query_none,
	noxs_dev_query_id,
	noxs_dev_query_cfg,
};


/* Register callback to watch this node. */
struct xenbus_watch
{
	struct list_head list;

	/* Path being watched. */
	const char *node;//TODO remove

	/* Callback (executed in a process context with no locks held). */
	void *callback;//TODO remove

	int (*create)(noxs_dev_key_t *key, void *cfg, noxs_dev_comm_t *out_res);
	int (*destroy)(noxs_dev_key_t *key);
	int (*query)(enum noxs_dev_query_type qtype, noxs_dev_key_t *key, uint32_t *out_num, void *out_info);
	int (*guest_cmd)(domid_t domid, unsigned long cmd, void *arg);
};


/* A xenbus device. */
struct xenbus_device {
	/*const char *devicetype;*/
	/*const char *nodename;
	const char *otherend;*/
	int otherend_id;
	int id;
	enum noxs_dev_type devicetype;
	struct xenbus_watch otherend_watch;//TODO not needed; not many watches per device, but only 1
	struct device dev;
	enum xenbus_state state;
	struct completion down;//TODO do we need this?
	struct work_struct work;

	void *ctrl_page;
	grant_ref_t grant;
	int irq;
	evtchn_port_t remote_port;

	bool comm_initialized;

	void *dev_cfg;
};

static inline struct xenbus_device *to_xenbus_device(struct device *dev)
{
	return container_of(dev, struct xenbus_device, dev);
}

static inline const char *to_xenbus_name(struct xenbus_device *xdev)
{
	return dev_name(&xdev->dev);
}

static inline const char *to_xenbus_type_string(struct xenbus_device *xdev)
{
	return "dummy-devicetype";//TODO
}

static inline const char *to_xenbus_otherend(struct xenbus_device *xdev)
{
	return "dummy-otherend";//TODO
}

struct xenbus_device_id
{
	/* .../device/<device_type>/<identifier> */
	char devicetype[32]; 	/* General class of device. TODO enum */
	//enum noxs_dev_type devicetype;
};

/* A xenbus driver. */
struct xenbus_driver {
	const char *name;       /* defaults to ids[0].devicetype */
	const struct xenbus_device_id *ids;
	struct noxs_thread *noxs_thread;
	int (*probe)(struct xenbus_device *dev,
		     const struct xenbus_device_id *id);
	void (*otherend_changed)(struct xenbus_device *dev,
				 enum xenbus_state backend_state);
	int (*remove)(struct xenbus_device *dev);
	int (*suspend)(struct xenbus_device *dev);
	int (*resume)(struct xenbus_device *dev);
	int (*uevent)(struct xenbus_device *, struct kobj_uevent_env *);
	struct device_driver driver;
#ifndef CONFIG_XEN_BACKEND_NOXS
	int (*read_otherend_details)(struct xenbus_device *dev);
#endif
	int (*is_ready)(struct xenbus_device *dev);
	int (*driver_cmd)(struct xenbus_device *dev, unsigned long cmd, void *arg);
	int (*device_info)(struct xenbus_device *dev, void *arg);
};

static inline struct xenbus_driver *to_xenbus_driver(struct device_driver *drv)
{
	return container_of(drv, struct xenbus_driver, driver);
}

int __must_check __xenbus2_register_frontend(struct xenbus_driver *drv,
					    struct module *owner,
					    const char *mod_name);
int __must_check __noxs_register_backend(struct xenbus_driver *drv,
					   struct module *owner,
					   const char *mod_name);

#define xenbus2_register_frontend(drv) \
	__xenbus2_register_frontend(drv, THIS_MODULE, KBUILD_MODNAME)
#define xenbus_register_backend(drv) \
	__noxs_register_backend(drv, THIS_MODULE, KBUILD_MODNAME)

void xenbus_unregister_driver(struct xenbus_driver *drv);

/* notifer routines for when the xenstore comes up */
extern int xenstored_ready;
int register_xenstore_notifier(struct notifier_block *nb);
void unregister_xenstore_notifier(struct notifier_block *nb);

//int register_xenbus2_watch(struct xenbus2_watch *watch);
//void unregister_xenbus2_watch(struct xenbus2_watch *watch);
void noxs_backend_register_watch(struct xenbus_watch* in);
void xs_suspend(void);
void xs_resume(void);
void xs_suspend_cancel(void);

struct work_struct;

void noxs_probe(struct work_struct *);
void noxs_notify_otherend(struct xenbus_device *dev);
int xenbus_switch_state(struct xenbus_device *dev, enum xenbus_state new_state);
int xenbus_grant_ring(struct xenbus_device *dev, void *vaddr,
		      unsigned int nr_pages, grant_ref_t *grefs);
int xenbus_map_ring_valloc(struct xenbus_device *dev, grant_ref_t *gnt_refs,
			   unsigned int nr_grefs, void **vaddr);
int xenbus_map_ring(struct xenbus_device *dev,
		    grant_ref_t *gnt_refs, unsigned int nr_grefs,
		    grant_handle_t *handles, unsigned long *vaddrs,
		    bool *leaked);

int xenbus_unmap_ring_vfree(struct xenbus_device *dev, void *vaddr);
int xenbus_unmap_ring(struct xenbus_device *dev,
		      grant_handle_t *handles, unsigned int nr_handles,
		      unsigned long *vaddrs);

int xenbus_alloc_evtchn(struct xenbus_device *dev, int *port);
int xenbus_alloc_evtchn_remote(struct xenbus_device *dev, int *port);
int xenbus_free_evtchn(struct xenbus_device *dev, int port);

enum xenbus_state xenbus_read_driver_state(const char *path);

__printf(3, 4)
void xenbus_dev_error(struct xenbus_device *dev, int err, const char *fmt, ...);
__printf(3, 4)
void xenbus_dev_fatal(struct xenbus_device *dev, int err, const char *fmt, ...);

const char *xenbus_strstate(enum xenbus_state state);
int xenbus_dev_is_online(struct xenbus_device *dev);
int xenbus_frontend_closed(struct xenbus_device *dev);


#endif /* _XEN_XENBUS_H */

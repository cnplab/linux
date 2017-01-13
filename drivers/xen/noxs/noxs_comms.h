/*
 * noxs_thread.h
 *
 *  Created on: Sep 29, 2016
 *      Author: wolf
 */

#ifndef DRIVERS_XEN_NOXS_NOXS_COMMS_H_
#define DRIVERS_XEN_NOXS_NOXS_COMMS_H_

#include <linux/types.h>
#include <linux/wait.h>
#include <xen/noxs.h>


int  noxs_comm_init(struct xenbus_device *xdev);
void noxs_comm_free(struct xenbus_device *xdev);



struct noxs_watch_event {
	struct list_head list;
	struct xenbus_device *xendev;
};

struct noxs_thread {
	char name[16];
	bool active;
	struct mutex mutex;
	wait_queue_head_t events_waitq;

	struct list_head events;
	spinlock_t events_lock;

	struct task_struct *task;

	void (*otherend_changed)(struct xenbus_watch *watch);
};

struct noxs_thread *noxs_comm_thread_create(const char *name, void (*cb)(struct xenbus_watch *watch));//TODO typedef for cb
void noxs_comm_thread_destroy(struct noxs_thread *t);
int  noxs_comm_thread_run(struct noxs_thread *t);

int noxs_comm_watch_otherend(struct xenbus_device *dev, void (*cb)(struct xenbus_watch *watch));
void noxs_comm_free_otherend_watch(struct xenbus_device *xdev);


#endif /* DRIVERS_XEN_NOXS_NOXS_COMMS_H_ */

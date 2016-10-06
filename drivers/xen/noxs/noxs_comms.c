/*
 * noxs_thread.c
 *
 *  Created on: Sep 29, 2016
 *      Author: wolf
 */


#include <linux/kernel.h>
#include <linux/kthread.h>
#include <linux/sched.h>
#include <linux/wait.h>
#include <linux/fs.h>
#include <linux/slab.h>
#include <linux/list.h>

#include <xen/events.h>

#include "noxs_comms.h"




static int noxs_thread_create(struct noxs_thread **out_thread,
		void (*cb)(struct xenbus_watch *watch))
{
	struct noxs_thread *t;

	t = kzalloc(sizeof(*t), GFP_KERNEL);
	if (!t)
		return -ENOMEM;//TODO

	mutex_init(&t->mutex);
	init_waitqueue_head(&t->events_waitq);
	INIT_LIST_HEAD(&t->events);
	spin_lock_init(&t->events_lock);

	t->otherend_changed = cb;

	*out_thread = t;

	return 0;
}

static void noxs_thread_destroy(struct noxs_thread *t)
{
	struct list_head list;

	list_add_tail(&list, &t->events);

	kthread_stop(t->task);
	wake_up(&t->events_waitq);

	kfree(t);
}


static int noxs_thread_func(void *arg)
{
	struct noxs_thread *t;
	struct list_head *ent;
	struct noxs_watch_event *event;
	noxs_ctrl_hdr_t *ctrl_hdr;

	int count = 0;//TODO remove

	t = (struct noxs_thread *) arg;

	for (;;) {
		wait_event_interruptible(t->events_waitq, !list_empty(&t->events));

		if (kthread_should_stop())
			break;

		while (!list_empty(&t->events)) {

			printk("----noxs event recvd %d\n", count);

			mutex_lock(&t->mutex);

			spin_lock(&t->events_lock);
			ent = t->events.next;
			list_del(ent);
			spin_unlock(&t->events_lock);

			event = list_entry(ent, struct noxs_watch_event, list);
			ctrl_hdr = event->xendev->ctrl_page;

			if (1)//ctrl_hdr->be_watch_state != noxs_watch_none)
				t->otherend_changed(&event->xendev->otherend_watch);
			else
				printk("unrequested watch event: %s.\n", xenbus_strstate(ctrl_hdr->fe_state));

			kfree(event);

			mutex_unlock(&t->mutex);

			count++;
		}
	}

	return 0;
}

static int noxs_thread_run(struct noxs_thread *t)
{
	//TODO check t

	t->task = kthread_run(noxs_thread_func, t, "noxs");//TODO name
	if (IS_ERR(t->task))
		return PTR_ERR(t->task);
	//xennoxs_pid = task->pid;

	return 0;
}

static irqreturn_t wake_waiting(int irq, void *unused)
{
	struct xenbus_device *xdev;
	struct noxs_watch_event *event;

	xdev = (struct xenbus_device *) unused;

	if (unlikely(xdev->comm_initialized == false))
		xdev->comm_initialized = true;

	else {
		event = kzalloc(sizeof(struct noxs_watch_event), GFP_KERNEL);//TODO check
		event->xendev = xdev;

		list_add_tail(&event->list, &xdev->thread->events);

		wake_up(&xdev->thread->events_waitq);
	}

	return IRQ_HANDLED;
}

int noxs_comm_watch_otherend(struct xenbus_device *xdev,
		void (*cb)(struct xenbus_watch *watch))
{
	int err;

	err = noxs_thread_create(&xdev->thread, cb);
	if (err) {
		printk("noxs_thread_create failed\n");
		goto out_err;
	}

	err = noxs_thread_run(xdev->thread);
	if (err) {
		printk("noxs_thread_run failed\n");
		goto fail;
	}

	err = bind_interdomain_evtchn_to_irqhandler(xdev->otherend_id,
			xdev->remote_port, wake_waiting, 0, "noxs", xdev);
	if (err < 0) {
		printk("bind_interdomain_evtchn_to_irqhandler failed\n");
		goto fail;
	}

	xdev->irq = err;

	return 0;

fail:
	noxs_thread_destroy(xdev->thread);//TODO make thread encapsulated
out_err:
	return err;
}


int noxs_comm_init(struct xenbus_device *xdev)
{
	int err;
	void *page;

	/* Shared page */
	page = (void *) get_zeroed_page(GFP_KERNEL);
	if (page == NULL) {
		err = -ENOMEM;
		goto out_err;
	}

	err = gnttab_grant_foreign_access(xdev->otherend_id, virt_to_gfn(page), 0);
	if (err < 0)
		goto free_page;

	xdev->ctrl_page = page;
	xdev->grant = err;

	/* Event channel */
	err = xenbus_alloc_evtchn_remote(xdev, &xdev->remote_port);
	if (err != 0)
		goto free_grant;

	return 0;

free_grant:
	/* This also frees the page */
	gnttab_end_foreign_access(xdev->grant, 0, (unsigned long) page);
	page = NULL;
free_page:
	/* if not freed above */
	if (page)
		free_page((unsigned long) page);
out_err:
	return err;
}

int noxs_comm_free(struct xenbus_device *xdev)
{
	int ret;

	printk("xenbus_noxs_destroy\n");

	ret = xenbus_free_evtchn(xdev, xdev->remote_port);//TODO do we need to do smth with ret?

	/* This also frees the page */
	gnttab_end_foreign_access(xdev->grant, 0, (unsigned long) xdev->ctrl_page);

	xdev->grant = INVALID_GRANT_HANDLE;
	xdev->ctrl_page = NULL;

	printk("xenbus_noxs_destroyed\n");

	return ret;
}

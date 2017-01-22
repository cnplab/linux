#include <linux/kernel.h>
#include <linux/kthread.h>
#include <linux/sched.h>
#include <linux/wait.h>
#include <linux/fs.h>
#include <linux/slab.h>
#include <linux/list.h>
#include <xen/events.h>
#include "noxs_comms.h"


struct noxs_thread *noxs_comm_thread_create(const char *name, void (*cb)(struct xenbus_watch *watch))
{
	struct noxs_thread *t;

	t = kzalloc(sizeof(*t), GFP_KERNEL);
	if (!t)
		return NULL;

	sprintf(t->name, "noxs-%s", name);

	mutex_init(&t->mutex);
	init_waitqueue_head(&t->events_waitq);
	INIT_LIST_HEAD(&t->events);
	spin_lock_init(&t->events_lock);

	t->otherend_changed = cb;

	return t;
}

void noxs_comm_thread_destroy(struct noxs_thread *t)
{
	mutex_lock(&t->mutex);
	if (t->active)
		kthread_stop(t->task);
	mutex_unlock(&t->mutex);

	kfree(t);
}

static int noxs_comm_thread_func(void *arg)
{
	struct noxs_thread *t;
	struct list_head *ent;
	struct noxs_watch_event *event;
	noxs_ctrl_hdr_t *ctrl_hdr;

	t = (struct noxs_thread *) arg;

	mutex_lock(&t->mutex);
	t->active = true;
	mutex_unlock(&t->mutex);

	for (;;) {
		wait_event_interruptible(t->events_waitq,
				!list_empty(&t->events) || kthread_should_stop());

		if (kthread_should_stop())
			break;

		mutex_lock(&t->mutex);
		while (!list_empty(&t->events)) {
			spin_lock(&t->events_lock);
			ent = t->events.next;
			list_del(ent);
			spin_unlock(&t->events_lock);

			event = list_entry(ent, struct noxs_watch_event, list);
			ctrl_hdr = event->xendev->ctrl_page;

			/* TODO: TBD if we need to check the watch state
			 * ctrl_hdr->be_watch_state == noxs_watch_requested ?
			 */
			if (t->otherend_changed)
				t->otherend_changed(&event->xendev->otherend_watch);

			kfree(event);
		}
		mutex_unlock(&t->mutex);
	}

	return 0;
}

int noxs_comm_thread_run(struct noxs_thread *t)
{
	t->task = kthread_run(noxs_comm_thread_func, t, t->name);
	if (IS_ERR(t->task))
		return PTR_ERR(t->task);

	return 0;
}

static irqreturn_t wake_waiting(int irq, void *unused)
{
	struct xenbus_device *xdev;
	struct xenbus_driver *drv;
	struct noxs_watch_event *event;

	xdev = (struct xenbus_device *) unused;
	drv = to_xenbus_driver(xdev->dev.driver);

	if (unlikely(xdev->comm_initialized == false))
		xdev->comm_initialized = true;

	event = kzalloc(sizeof(struct noxs_watch_event), GFP_KERNEL);//TODO check
	event->xendev = xdev;

	list_add_tail(&event->list, &drv->noxs_thread->events);

	wake_up(&drv->noxs_thread->events_waitq);

	return IRQ_HANDLED;
}

int noxs_comm_watch_otherend(struct xenbus_device *xdev,
		void (*cb)(struct xenbus_watch *watch))
{
	int err;

	err = bind_evtchn_to_irqhandler(xdev->local_port,
			wake_waiting, 0, "noxs", xdev);
	if (err < 0) {
		printk("bind_interdomain_evtchn_to_irqhandler failed\n");
		goto fail;
	}

	xdev->irq = err;

	return 0;

fail:
	return err;
}

void noxs_comm_free_otherend_watch(struct xenbus_device *xdev)
{
	unbind_from_irqhandler(xdev->irq, xdev);
	xdev->comm_initialized = false;
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
	err = xenbus_alloc_evtchn(xdev, &xdev->local_port);
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

void noxs_comm_free(struct xenbus_device *xdev)
{
	/* This also frees the page */
	gnttab_end_foreign_access(xdev->grant, 0, (unsigned long) xdev->ctrl_page);

	xdev->grant = INVALID_GRANT_HANDLE;
	xdev->ctrl_page = NULL;
}

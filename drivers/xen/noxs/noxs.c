/*
 * noxs.c
 *
 *  Created on: Sep 15, 2016
 *      Author: wolf
 */


#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/kthread.h>
#include <linux/sched.h>
#include <linux/wait.h>
#include <linux/fs.h>
#include <linux/miscdevice.h>
#include <linux/slab.h>
#include <uapi/xen/noxs.h>

#include <xen/xen.h>
#include <xen/events.h>
#include <xen/evtchn.h>
#include <xen/grant_table.h>


struct domain
{
	struct list_head list;

	/* The id of this domain */
	unsigned int domid;

	/* Event channel port */
	evtchn_port_t port;

	/* The remote end of the event channel, used only to validate
	   repeated domain introductions. */
	evtchn_port_t remote_port;

	/* The mfn associated with the event channel, used only to validate
	   repeated domain introductions. */
	unsigned long mfn;

	/* Shared page. */
	struct noxs_dev_page *interface;

	/* The connection associated with this. */
	//struct connection *conn;

	/* Have we noticed that this domain is shutdown? */
	int shutdown;

	/* number of entry from this domain in the store */
	int nbentry;

	/* number of watch for this domain */
	int nbwatch;
};

static LIST_HEAD(domains);



static DEFINE_MUTEX(noxs_mutex);
static DECLARE_WAIT_QUEUE_HEAD(noxs_events_waitq);

static LIST_HEAD(noxs_events);
static DEFINE_SPINLOCK(noxs_events_lock);



static void *map_interface(domid_t domid, unsigned long mfn)
{
/*	if (*xgt_handle != NULL) {
		 this is the preferred method
		return xengnttab_map_grant_ref(*xgt_handle, domid,
			GNTTAB_RESERVED_XENSTORE, PROT_READ|PROT_WRITE);
	} else {
		return xc_map_foreign_range(*xc_handle, domid,
			XC_PAGE_SIZE, PROT_READ|PROT_WRITE, mfn);
	}*/
	int rc;

	rc = gnttab_grant_foreign_access(domid, mfn, 0);
	if (rc < 0) {
		printk("error mapping gnt");
	}
	else
	printk("noxs map success\n");

	return rc;
}

static void unmap_interface(void *interface)
{
/*	if (*xgt_handle != NULL)
		xengnttab_unmap(*xgt_handle, interface, 1);
	else
		munmap(interface, XC_PAGE_SIZE);*/
}

static irqreturn_t wake_waiting(int irq, void *unused)
{
	/*if (unlikely(xenstored_ready == 0)) {
		xenstored_ready = 1;
		schedule_work(&probe_work);
	}*/
	struct list_head *list;
	printk("noxs event recvd\n");

	list = kzalloc(sizeof(struct list_head), GFP_KERNEL);
	list_add_tail(list, &noxs_events);

	wake_up(&noxs_events_waitq);
	return IRQ_HANDLED;
}

static int evtchnn_bind_interdomain(unsigned int domid, evtchn_port_t port)
{
	int rc;


	int err;

	err = bind_interdomain_evtchn_to_irqhandler(domid, port, wake_waiting,
						    0, "noxs", &noxs_events_waitq);
	if (err < 0) {
		printk("bind_evtchn_to_irqhandler failed %i\n", err);
		return err;
	}

	//TODO xenbus_irq = err;

	return rc;
}

static int enevtchn_unbind(evtchn_port_t port)
{
#if 0
	struct ioctl_evtchn_unbind unbind;
	struct user_evtchn *evtchn;

	rc = -EFAULT;
	if (copy_from_user(&unbind, uarg, sizeof(unbind)))
		break;

	rc = -EINVAL;
	if (unbind.port >= xen_evtchn_nr_channels())
		break;

	rc = -ENOTCONN;
	evtchn = find_evtchn(u, unbind.port);
	if (!evtchn)
		break;

	disable_irq(irq_from_evtchn(unbind.port));
	evtchn_unbind_from_user(u, evtchn);
	rc = 0;
	break;
#endif
	return 0;
}



static int destroy_domain(void *_domain)
{
	struct domain *domain = _domain;

	list_del(&domain->list);

	if (domain->port) {
		if (enevtchn_unbind(domain->port) == -1)
			printk(KERN_INFO"> Unbinding port %i failed!\n", domain->port);
	}

	if (domain->interface) {
		/* Domain 0 was mapped by dom0_init, so it must be unmapped
		   using munmap() and not the grant unmap call. */
		if (domain->domid == 0)
			/*unmap_xenbus(domain->interface);*/
			return -1;//TODO
		else
			unmap_interface(domain->interface);
	}

	return 0;
}

static struct domain *new_domain(void *context, unsigned int domid,
				 int port)
{
	struct domain *domain;
	int rc;

	domain = kzalloc(sizeof(struct domain), GFP_KERNEL);
	domain->port = 0;
	domain->shutdown = 0;
	domain->domid = domid;

	list_add(&domain->list, &domains);
	//talloc_set_destructor(domain, destroy_domain);

	/* Tell kernel we're interested in this event. */
	//rc = xenevtchn_bind_interdomain(xce_handle, domid, port);
	rc = evtchnn_bind_interdomain(domid, port);

	if (rc == -1)
	    return NULL;
	domain->port = rc;

	/*domain->conn = new_connection(writechn, readchn);
	domain->conn->domain = domain;
	domain->conn->id = domid;*/

	domain->remote_port = port;
	domain->nbentry = 0;
	domain->nbwatch = 0;

	return domain;
}

static long noxs_ioctl_devpage_reg(void __user *udata)
{
	struct noxs_devpage_reg devpagereg;
	int rc = 0;

	struct domain *domain;
	struct noxs_dev_page *interface;

	if (copy_from_user(&devpagereg, udata, sizeof(devpagereg)))
		return -EFAULT;

	interface = map_interface(devpagereg.domid, devpagereg.mfn);
	if (!interface) {
		//TODO send_error(conn, errno);
		return -6;
	}

	/* Hang domain off "in" until we're finished. */
	domain = new_domain(NULL, devpagereg.domid, devpagereg.port);
	if (!domain) {
		unmap_interface(interface);
		//TODO send_error(conn, errno);
		return -6;
	}
	domain->interface = interface;
	domain->mfn = devpagereg.mfn;

	return rc;
}

static long noxs_ioctl_devreq(void __user *udata)
{
#if 0
	struct privcmd_mmap mmapcmd;
	struct mm_struct *mm = current->mm;
	struct vm_area_struct *vma;
	int rc;
	LIST_HEAD(pagelist);
	struct mmap_gfn_state state;

	/* We only support privcmd_ioctl_mmap_batch for auto translated. */
	if (xen_feature(XENFEAT_auto_translated_physmap))
		return -ENOSYS;

	if (copy_from_user(&mmapcmd, udata, sizeof(mmapcmd)))
		return -EFAULT;

	rc = gather_array(&pagelist,
			  mmapcmd.num, sizeof(struct privcmd_mmap_entry),
			  mmapcmd.entry);

	if (rc || list_empty(&pagelist))
		goto out;

	down_write(&mm->mmap_sem);

	{
		struct page *page = list_first_entry(&pagelist,
						     struct page, lru);
		struct privcmd_mmap_entry *msg = page_address(page);

		vma = find_vma(mm, msg->va);
		rc = -EINVAL;

		if (!vma || (msg->va != vma->vm_start) || vma->vm_private_data)
			goto out_up;
		vma->vm_private_data = PRIV_VMA_LOCKED;
	}

	state.va = vma->vm_start;
	state.vma = vma;
	state.domain = mmapcmd.dom;

	rc = traverse_pages(mmapcmd.num, sizeof(struct privcmd_mmap_entry),
			    &pagelist,
			    mmap_gfn_range, &state);


out_up:
	up_write(&mm->mmap_sem);

out:
	free_page_list(&pagelist);

	return rc;
#endif
	return 0;
}

static long noxs_ioctl(struct file *file,
			  unsigned int cmd, unsigned long data)
{
	int ret = -ENOSYS;
	void __user *udata = (void __user *) data;

	switch (cmd) {
	case IOCTL_NOXS_DEVPAGE_REG:
		ret = noxs_ioctl_devpage_reg(udata);
		break;
	case IOCTL_NOXS_DEVREQ:
		ret = noxs_ioctl_devreq(udata);
		break;
#if 0
		ret = privcmd_ioctl_hypercall(udata);
		break;

	case IOCTL_PRIVCMD_MMAP:
		ret = privcmd_ioctl_mmap(udata);
		break;

	case IOCTL_PRIVCMD_MMAPBATCH:
		ret = privcmd_ioctl_mmap_batch(udata, 1);
		break;

	case IOCTL_PRIVCMD_MMAPBATCH_V2:
		ret = privcmd_ioctl_mmap_batch(udata, 2);
		break;
#endif

	default:
		ret = -EINVAL;
		break;
	}

	return ret;
}


static struct task_struct *task = NULL;
const struct file_operations xen_noxs_fops = {
	.owner = THIS_MODULE,
	.unlocked_ioctl = noxs_ioctl,
};
EXPORT_SYMBOL_GPL(xen_noxs_fops);

static struct miscdevice privcmd_dev = {
	.minor = MISC_DYNAMIC_MINOR,
	.name = "xen/noxs",
	.fops = &xen_noxs_fops,
};

static int noxs_thread(void *unused)
{
	struct list_head *ent, *i;

	int count;

	for (;;) {
		wait_event_interruptible(noxs_events_waitq,
					 !list_empty(&noxs_events));

		count = 0;
		list_for_each(i, &noxs_events)
			count++;

		printk("----noxs event recvd %d\n", count);

		mutex_lock(&noxs_mutex);

		spin_lock(&noxs_events_lock);
		ent = noxs_events.next;
		if (ent != &noxs_events)
			list_del(ent);
		spin_unlock(&noxs_events_lock);

		if (ent != &noxs_events) {
			/*msg = list_entry(ent, struct xs_stored_msg, list);
			pr_debug("watched\n");
			msg->u.watch.handle->callback(
				msg->u.watch.handle,
				(const char **)msg->u.watch.vec,
				msg->u.watch.vec_size);
			kfree(msg->u.watch.vec);
			kfree(msg);*/
			kfree(ent);
		}

		mutex_unlock(&noxs_mutex);

		if (kthread_should_stop())
			break;
	}

	return 0;
}

static int __init noxs_init(void)
{
	int rc = 0;

	printk("noxs_init");

	//int err;

	task = kthread_run(noxs_thread, NULL, "noxs");
	if (IS_ERR(task))
		return PTR_ERR(task);
	//xennoxs_pid = task->pid;
	rc = misc_register(&privcmd_dev);
	if (rc != 0) {
		pr_err("Could not register Xen noxenbus device\n");
		return rc;
	}

	return 0;

failed_init:
	return rc;
}

module_init(noxs_init);

//#define __exit
static void __exit noxs_fini(void)
{
	struct list_head list;

	printk("noxs_fini");


	list_add_tail(&list, &noxs_events);

	kthread_stop(task);
	wake_up(&noxs_events_waitq);


	misc_deregister(&privcmd_dev);
}
module_exit(noxs_fini);

MODULE_LICENSE("GPL");
MODULE_ALIAS("noxs");

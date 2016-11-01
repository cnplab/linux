#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt
#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/miscdevice.h>
#include <uapi/xen/noxs.h>

#include <xen/noxs.h>


static struct xenbus_watch* watch;


void noxs_backend_register_watch(struct xenbus_watch* in)//TODO GPL
{
	watch = in;
}

static long noxs_backend_ioctl_dev_create(void __user *udata)
{
	struct noxs_ioctl_dev_create dev_create;
	noxs_dev_key_t k;
	noxs_dev_comm_t comm;
	int rc;

	if (copy_from_user(&dev_create, udata, sizeof(dev_create)))
		return -EFAULT;

	k.type = dev_create.type;
	k.be_id = dev_create.be_id;
	k.fe_id = dev_create.fe_id;

	rc = watch->create(&k, &dev_create.cfg, &comm);
	if (rc)
		goto out;

	dev_create.devid = k.devid;
	dev_create.grant = comm.grant;
	dev_create.evtchn = comm.evtchn;

	if (copy_to_user(udata, &dev_create, sizeof(dev_create)))
		return -EFAULT;

out:
	return rc;
}

static long noxs_backend_ioctl_dev_destroy(void __user *udata)
{
	struct noxs_ioctl_dev_destroy dev_destroy;
	noxs_dev_key_t k;

	if (copy_from_user(&dev_destroy, udata, sizeof(dev_destroy)))
		return -EFAULT;

	k.type = dev_destroy.type;
	k.be_id = dev_destroy.be_id;
	k.fe_id = dev_destroy.fe_id;
	k.devid = dev_destroy.devid;

	return watch->destroy(&k);
}

static long noxs_backend_ioctl_dev_list(void __user *udata)
{
	struct noxs_ioctl_dev_list dev_list;
	noxs_dev_key_t k;
	int rc;

	if (copy_from_user(&dev_list, udata, sizeof(dev_list)))
		return -EFAULT;

	k.type = dev_list.type;
	k.be_id = dev_list.be_id;
	k.fe_id = dev_list.fe_id;

	rc = watch->query(&k, &dev_list.count, dev_list.ids);
	if (rc)
		goto out;

	if (copy_to_user(udata, &dev_list, sizeof(dev_list)))
		return -EFAULT;

out:
	return rc;
}

static long noxs_backend_ioctl_close(void __user *udata)
{
	struct noxs_ioctl_guest_close guest_close;
	noxs_dev_key_t k;
	int rc;

	if (copy_from_user(&guest_close, udata, sizeof(guest_close)))
		return -EFAULT;

	k.type = guest_close.type;
	k.fe_id = guest_close.domid;

	rc = watch->guest_cmd(guest_close.domid, 0);//TODO create command and add support for other shutdown reasons (command argument)
	if (rc)
		goto out;

	if (copy_to_user(udata, &guest_close, sizeof(guest_close)))
		return -EFAULT;

out:
	return rc;
}

static long noxs_backend_ioctl(struct file *file,
			  unsigned int cmd, unsigned long data)
{
	int ret;
	void __user *udata;

	if (!capable(CAP_SYS_ADMIN))
		return -EPERM;

	udata = (void __user *) data;

	switch (cmd) {
	case IOCTL_NOXS_DEV_CREATE:
		ret = noxs_backend_ioctl_dev_create(udata);
		break;
	case IOCTL_NOXS_DEV_DESTROY:
		ret = noxs_backend_ioctl_dev_destroy(udata);
		break;
	case IOCTL_NOXS_DEV_LIST:
		ret = noxs_backend_ioctl_dev_list(udata);
		break;
	case IOCTL_NOXS_GUEST_CLOSE:
		ret = noxs_backend_ioctl_close(udata);
		break;
	default:
		ret = -EINVAL;
		break;
	}

	return ret;
}

const struct file_operations xen_noxs_fops = {
	.owner = THIS_MODULE,
	.unlocked_ioctl = noxs_backend_ioctl,
};

static struct miscdevice devcmd_dev = {
	.minor = MISC_DYNAMIC_MINOR,
	.name = "xen/noxs_backend",
	.fops = &xen_noxs_fops,
};

static int __init noxs_backend_init(void)
{
	int rc = 0;

	rc = misc_register(&devcmd_dev);
	if (rc != 0) {
		pr_err("Could not register noxs backend device: %d\n", rc);
	}

	return rc;
}
device_initcall(noxs_backend_init);

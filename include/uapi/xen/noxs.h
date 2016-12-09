/*
 * noxenbus.h
 *
 *  Created on: Sep 15, 2016
 *      Author: wolf
 */

#ifndef INCLUDE_UAPI_XEN_NOXS_H_
#define INCLUDE_UAPI_XEN_NOXS_H_

#include <linux/ioctl.h>
#include <linux/if.h>
#include <linux/if_ether.h>


#define NOXS_USER_DEV_MAX 32

enum noxs_user_dev_type {
	noxs_user_dev_none = 0,
	noxs_user_dev_sysctl,
	noxs_user_dev_console,
	noxs_user_dev_vif,
};


/*
 * Device configuration
 */
struct noxs_user_cfg_vif {
	__u8 mac[ETH_ALEN];
	__be32 ip;
	char bridge[IFNAMSIZ];
};


/*
 *
 */

struct noxs_ioctl_dev_create {
	/* IN */
	enum noxs_user_dev_type type;
	__u16 be_id;
	__u16 fe_id;

	union { /* device specific */
		struct noxs_user_cfg_vif vif;
	} cfg;

	/* OUT */
	__u32 devid;
	__u32 grant;
	__u32 evtchn;
};

struct noxs_ioctl_dev_destroy {
	/* IN */
	enum noxs_user_dev_type type;
	__u16 be_id;
	__u16 fe_id;
	__u32 devid;
};

struct noxs_ioctl_dev_list {
	/* IN */
	enum noxs_user_dev_type type;
	__u16 be_id;
	__u16 fe_id;
	__u32 devid;

	/* OUT */
	__u32 count;
	__u32 ids[NOXS_USER_DEV_MAX];
};

struct noxs_ioctl_dev_query_cfg {/* TODO this just has to work; optimization later */
	/* IN */
	enum noxs_user_dev_type type;
	__u16 be_id;
	__u16 fe_id;
	__u32 devid;

	union { /* device specific */
		struct noxs_user_cfg_vif vif;
	} cfg;
};

enum noxs_user_shutdown_type {
	noxs_user_sd_none = 0 ,
	noxs_user_sd_poweroff ,
	noxs_user_sd_suspend
};

struct noxs_ioctl_guest_close {
	enum noxs_user_shutdown_type type;
	__u16 domid;
};

#define IOCTL_NOXS_DEV_CREATE \
	_IOC(_IOC_NONE, 'P', 0, sizeof(struct noxs_ioctl_dev_create))
#define IOCTL_NOXS_DEV_DESTROY \
	_IOC(_IOC_NONE, 'P', 1, sizeof(struct noxs_ioctl_dev_destroy))
#define IOCTL_NOXS_DEV_LIST \
	_IOC(_IOC_NONE, 'P', 2, sizeof(struct noxs_ioctl_dev_list))
#define IOCTL_NOXS_DEV_QUERY_CFG \
	_IOC(_IOC_NONE, 'P', 3, sizeof(struct noxs_ioctl_dev_query_cfg))
#define IOCTL_NOXS_GUEST_CLOSE \
	_IOC(_IOC_NONE, 'P', 5, sizeof(struct noxs_ioctl_guest_close))

#endif /* INCLUDE_UAPI_XEN_NOXS_H_ */

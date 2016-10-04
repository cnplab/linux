/*
 * noxenbus.h
 *
 *  Created on: Sep 15, 2016
 *      Author: wolf
 */

#ifndef INCLUDE_UAPI_XEN_NOXS_H_
#define INCLUDE_UAPI_XEN_NOXS_H_

#include <linux/ioctl.h>

#include <xen/interface/noxs.h>

struct noxs_ioctl_dev_create {
	/* IN */
	noxs_dev_key_t key;

	union { /* device specific */
		noxs_cfg_vif_t vif;
	} cfg;

	/* OUT */
	noxs_dev_comm_t comm;
};

struct noxs_ioctl_dev_destroy {
	/* IN */
	noxs_dev_key_t key;
};

struct noxs_ioctl_dev_list {
	/* IN */
	noxs_dev_key_t key;

	/* OUT */
	uint32_t count;
	noxs_dev_id_t ids[NOXS_DEV_COUNT_MAX];
};


#define IOCTL_NOXS_DEV_CREATE \
	_IOC(_IOC_NONE, 'P', 0, sizeof(struct noxs_ioctl_dev_create))
#define IOCTL_NOXS_DEV_DESTROY \
	_IOC(_IOC_NONE, 'P', 1, sizeof(struct noxs_ioctl_dev_destroy))
#define IOCTL_NOXS_DEV_LIST \
	_IOC(_IOC_NONE, 'P', 2, sizeof(struct noxs_ioctl_dev_list))

#endif /* INCLUDE_UAPI_XEN_NOXS_H_ */

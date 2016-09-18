/*
 * noxenbus.h
 *
 *  Created on: Sep 15, 2016
 *      Author: wolf
 */

#ifndef INCLUDE_UAPI_XEN_NOXS_H_
#define INCLUDE_UAPI_XEN_NOXS_H_

#include <linux/types.h>
#include <linux/compiler.h>

#if 0
#define _IOC(dir,type,nr,size) \
	(((dir)  << _IOC_DIRSHIFT) | \
	 ((type) << _IOC_TYPESHIFT) | \
	 ((nr)   << _IOC_NRSHIFT) | \
	 ((size) << _IOC_SIZESHIFT))
#endif

enum noxs_dev_type {
	noxs_dev_vif,
};

struct noxenbus_dev_req {
	enum noxs_dev_type type;
};

struct noxs_devpage_reg {
	unsigned int domid;
	evtchn_port_t port;
	unsigned int mfn;
};

#define IOCTL_NOXS_DEVPAGE_REG					\
	_IOC(_IOC_NONE, 'P', 0, sizeof(struct noxs_devpage_reg))
#define IOCTL_NOXS_DEVREQ					\
	_IOC(_IOC_NONE, 'P', 1, sizeof(struct noxenbus_dev_req))
#define IOCTL_NOXENBUS_MMAP					\
	_IOC(_IOC_NONE, 'P', 2, sizeof(struct privcmd_mmap))
#define IOCTL_NOXENBUS_MMAPBATCH					\
	_IOC(_IOC_NONE, 'P', 3, sizeof(struct privcmd_mmapbatch))
#define IOCTL_NOXENBUS_MMAPBATCH_V2				\
	_IOC(_IOC_NONE, 'P', 4, sizeof(struct privcmd_mmapbatch_v2))

#endif /* INCLUDE_UAPI_XEN_NOXS_H_ */

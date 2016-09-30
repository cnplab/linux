/*
 * noxenbus.h
 *
 *  Created on: Sep 15, 2016
 *      Author: wolf
 */

#ifndef INCLUDE_UAPI_XEN_NOXS_H_
#define INCLUDE_UAPI_XEN_NOXS_H_

//#include <linux/types.h>
//#include <linux/compiler.h>

#include <xen/interface/noxs.h>



typedef unsigned int evtchn_port_t;//TODO remove

/*
struct noxs_devpage_reg {
	unsigned int domid;
	evtchn_port_t port;
	unsigned int mfn;
};
*/

struct noxs_user_vif {
	/* IN */
	__u8 mac[ETH_ALEN];
	__be32 ip;
	/* OUT */
};

struct noxs_ioctl_devreq {
	/* IN */
	enum noxs_dev_type type;
	unsigned int domid;
	unsigned int devid; /* WIP */

	union { /* device specific */
		struct noxs_user_vif vif;
	} dev;

	/* OUT */
	unsigned int mfn;//TODO rename
	evtchn_port_t evtchn;
};


#define IOCTL_NOXS_DEVREQ \
	_IOC(_IOC_NONE, 'P', 1, sizeof(struct noxs_ioctl_devreq))

#endif /* INCLUDE_UAPI_XEN_NOXS_H_ */

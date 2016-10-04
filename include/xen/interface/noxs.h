/*
 * noxs.h
 *
 *  Created on: Sep 16, 2016
 *      Author: wolf
 */

#ifndef XEN_PUBLIC_IO_NOXS_H_
#define XEN_PUBLIC_IO_NOXS_H_

#include "event_channel.h"
#include "../grant_table.h"
#include "io/xenbus.h"/*TODO temporary, for XenbusState only*/


#define NOXS_DEV_COUNT_MAX 32/*TODO this is defined at hyp level*/


typedef uint32_t noxs_dev_id_t;


enum noxs_dev_type {
	noxs_dev_none = 0,
	noxs_dev_console,
	noxs_dev_vif,
};
typedef enum noxs_dev_type noxs_dev_type_t;


struct noxs_dev_key {
	noxs_dev_type_t type;
	domid_t be_id;
	domid_t fe_id;
	noxs_dev_id_t devid;
};
typedef struct noxs_dev_key noxs_dev_key_t;


struct noxs_dev_comm {
	grant_ref_t grant;
	evtchn_port_t evtchn;
};
typedef struct noxs_dev_comm noxs_dev_comm_t;


struct noxs_dev_page_entry {
	noxs_dev_type_t type;
	domid_t be_id;
	noxs_dev_comm_t comm;
};
typedef struct noxs_dev_page_entry noxs_dev_page_entry_t;


struct noxs_dev_page {
	uint32_t version;
	uint32_t dev_count;
	noxs_dev_page_entry_t devs[NOXS_DEV_COUNT_MAX];
};
typedef struct noxs_dev_page noxs_dev_page_t;


struct noxs_dev_entry {
	unsigned int idx;
	struct noxs_dev_page_entry *dev_info;
};
typedef struct noxs_dev_entry noxs_dev_entry_t;


enum noxs_watch_state {
	noxs_watch_none = 0,
	noxs_watch_requested,
	noxs_watch_updated
};
typedef enum noxs_watch_state noxs_watch_state_t;


struct noxs_ctrl_hdr {
	uint16_t domid;
	int devid;
	int grant;/*TODO???*/
	int evtchn;/*TODO??? redundancy*/
	int be_state;
	int fe_state;

	noxs_watch_state_t fe_watch_state;
	noxs_watch_state_t be_watch_state;
};
typedef struct noxs_ctrl_hdr noxs_ctrl_hdr_t;


/*
 * DEVICE SPECIFIC
 */

struct vif_features {
	__u8 rx_notify:1;
	__u8 sg:1;
	__u8 gso_tcpv4:1;
	__u8 gso_tcpv4_prefix:1;
	__u8 gso_tcpv6:1;
	__u8 gso_tcpv6_prefix:1;
	__u8 no_csum_offload:1;
	__u8 ipv6_csum_offload:1;
	__u8 rx_copy:1;
	__u8 rx_flip:1;
	__u8 multicast_control:1;
	__u8 dynamic_multicast_control:1;
	__u8 split_event_channels:1;
	__u8 ctrl_ring:1;
};

#define ETH_ALEN    6       /* Octets in one ethernet addr TODO temporary   */

struct noxs_vif_ctrl_page {
	noxs_ctrl_hdr_t hdr;
	int vifid;
	struct vif_features feature;
	int multi_queue_max_queues;
	int multi_queue_num_queues;

	grant_ref_t tx_ring_ref;
	grant_ref_t rx_ring_ref;
	evtchn_port_t event_channel_tx;
	evtchn_port_t event_channel_rx;

	unsigned int request_rx_copy;

	grant_ref_t ctrl_ring_ref;
	evtchn_port_t event_channel_ctrl;

	__u8 mac[ETH_ALEN];
	__be32 ip;
};
typedef struct noxs_vif_ctrl_page noxs_vif_ctrl_page_t;


/*
 * CONFIG
 */

struct noxs_cfg_vif {
	__u8 mac[ETH_ALEN];
	__be32 ip;
};
typedef struct noxs_cfg_vif noxs_cfg_vif_t;



#endif /* XEN_PUBLIC_IO_NOXS_H_ */

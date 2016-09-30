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
#include "io/xenbus.h"


#define NOXS_DEV_COUNT_MAX 32/*TODO this is defined at hyp level*/


enum noxs_dev_type {
	noxs_dev_none = 0,
	noxs_dev_console,
	noxs_dev_vif,
};

enum noxs_watch_state {
	noxs_watch_none = 0,
	noxs_watch_requested,
	noxs_watch_updated
};

struct noxs_dev_page_entry {
	enum noxs_dev_type type;

	domid_t be_id;

	unsigned int mfn;
	evtchn_port_t port;
};

struct noxs_dev_page {
	uint32_t version;
	uint32_t dev_count;
	struct noxs_dev_page_entry devs[NOXS_DEV_COUNT_MAX];
};

struct noxs_dev_entry {
	unsigned int idx;
	struct noxs_dev_page_entry *dev_info;
};

struct noxs_ctrl_hdr {
	uint16_t domid;
	int devid;
	int grant;
	int evtchn;
	int be_state;
	int fe_state;

	enum noxs_watch_state fe_watch_state;
	enum noxs_watch_state be_watch_state;
};

struct vif_features {
	__u8 rx_notify :1;
	__u8 sg :1;
	__u8 gso_tcpv4 :1;
	__u8 gso_tcpv4_prefix :1;
	__u8 gso_tcpv6 :1;
	__u8 gso_tcpv6_prefix :1;
	__u8 no_csum_offload :1;
	__u8 ipv6_csum_offload :1;
	__u8 rx_copy :1;
	__u8 rx_flip :1;
	__u8 multicast_control :1;
	__u8 dynamic_multicast_control :1;
	__u8 split_event_channels :1;
	__u8 ctrl_ring :1;
};

#define ETH_ALEN    6       /* Octets in one ethernet addr   */

struct noxs_vif_ctrl_page {
	struct noxs_ctrl_hdr hdr;
	__u8 mac[ETH_ALEN];
	__be32 ip;
	int vifid;
	struct vif_features feature;
	int multi_queue_max_queues;
	int multi_queue_num_queues;

	unsigned int tx_ring_ref;
	unsigned int rx_ring_ref;
	unsigned int event_channel_tx;
	unsigned int event_channel_rx;
	unsigned int request_rx_copy;

	int ctrl_ring; //bool
	unsigned int ctrl_ring_ref;
	int event_channel_ctrl;
};


#endif /* XEN_PUBLIC_IO_NOXS_H_ */

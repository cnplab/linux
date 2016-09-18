/*
 * noxs.h
 *
 *  Created on: Sep 16, 2016
 *      Author: wolf
 */

#ifndef XEN_PUBLIC_IO_NOXS_H_
#define XEN_PUBLIC_IO_NOXS_H_

#include "../event_channel.h"
#include "../grant_table.h"
#include "xenbus.h"


#define NOXS_DEV_COUNT_MAX 32/*TODO this is defined at hyp level*/


enum noxs_dev_type {
    noxs_dev_none = 0,
    noxs_dev_console,
    noxs_dev_vif,
};

struct noxs_dev_info {
    enum noxs_dev_type type;

    domid_t be_id;
    uint32_t be_state;
    uint32_t fe_state;

    unsigned int mfn;
    evtchn_port_t port;
};

struct noxs_dev_page {
    uint32_t version;
    uint32_t dev_count;
    struct noxs_dev_info devs[NOXS_DEV_COUNT_MAX];
};

struct noxs_dev_entry {
    unsigned int idx;
    struct noxs_dev_info *dev_info;
};

struct noxs_dev_vif {
    uint32_t version;

    char mac[18];

    evtchn_port_t port;
    unsigned int tx_ring_ref;
    unsigned int rx_ring_ref;

    struct {
        uint32_t sg;
        uint32_t gso_ipv4;
        uint32_t gso_ipv6;
    } features;
};

#endif /* XEN_PUBLIC_IO_NOXS_H_ */

/*
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License version 2
 * as published by the Free Software Foundation; or, when distributed
 * separately from the Linux kernel or incorporated into other
 * software packages, subject to the following license:
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this source file (the "Software"), to deal in the Software without
 * restriction, including without limitation the rights to use, copy, modify,
 * merge, publish, distribute, sublicense, and/or sell copies of the Software,
 * and to permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 */

#ifndef __XEN_BLKBACK__STORE_H__
#define __XEN_BLKBACK__STORE_H__


int store_write_init_info(struct xenbus_device *xdev);
int store_read_cfg(struct xenbus_device *xdev, void *cfg);

int store_watch_be(struct xenbus_device *xdev, struct xenbus_watch *watch, void (*cb)(struct xenbus_watch *watch, const char **vec, unsigned int len));

int store_read_be_node(struct xenbus_device *xdev, unsigned *major, unsigned *minor);
int store_read_be_devname(struct xenbus_device *xdev, struct xen_blkif *blkif, char *buf);//TODO check params
int store_read_be_mode(struct xenbus_device *xdev, char **mode);
int store_read_be_device_type(struct xenbus_device *xdev, char **device_type);
int store_read_be_handle(struct xenbus_device *xdev, unsigned long *handle);

int store_write_be_feat_flush_cache(struct xenbus_device *xdev, struct xenbus_transaction xbt,
				  int state);
//static void xen_blkbk_discard(struct xenbus_transaction xbt, struct backend_info *be);

int store_write_be_feat_barrier(struct xenbus_device *xdev, struct xenbus_transaction xbt,
				  int state);
int store_write_provide_fe_info(struct xenbus_device *xdev, struct xen_blkif *blkif);

int store_read_fe_protocol(struct xenbus_device *xdev, char *protocol);
int store_read_fe_persistent(struct xenbus_device *xdev, unsigned int *pers_grants);
int store_read_num_queues(struct xenbus_device *xdev, unsigned int *requested_num_queues);


int store_write_sectors_num(struct xenbus_device *xdev, unsigned long long sectors);

int store_read_ring_page_order(struct xenbus_device *xdev, unsigned int *ring_page_order);

struct store_ring_ref {
	int index;
	void *store_address;
};

int store_ring_ref_init(struct store_ring_ref* srref,
		struct xenbus_device *xdev, int ring_index);
void store_ring_ref_clear(struct store_ring_ref* srref);
const char* store_ring_ref_str(const struct store_ring_ref* srref);

int store_read_event_channel(struct store_ring_ref* ring_ref, unsigned int *evtchn);
int store_read_ring_ref(struct store_ring_ref* srref, int ref_index, unsigned int *ring_ref);

#endif /* __XEN_BLKBACK__STORE_H__ */

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

struct store_ring_ref {
	int index;
	void *store_address;
};


int store_write_init_info(struct xenbus_device *dev);

int store_watch_be(struct xenbus_device *dev, struct xenbus_watch *watch, void (*cb)(struct xenbus_watch *watch, const char **vec, unsigned int len));

int store_read_be_version(struct xenbus_device *dev, unsigned *major, unsigned *minor);
int store_read_be_mode(struct xenbus_device *dev, char **mode);
int store_read_be_device_type(struct xenbus_device *dev, char **device_type);
int store_read_be_handle(struct xenbus_device *dev, unsigned long *handle);

int store_write_be_feat_flush_cache(struct xenbus_device *dev, struct xenbus_transaction xbt,
				  int state);
//static void xen_blkbk_discard(struct xenbus_transaction xbt, struct backend_info *be);

int store_write_be_feat_barrier(struct xenbus_device *dev, struct xenbus_transaction xbt,
				  int state);
void store_write_provide_fe_info(struct xenbus_device *dev, struct xen_blkif *blkif);

int store_read_fe_protocol(struct xenbus_device *dev, char *protocol);
int store_read_fe_persistent(struct xenbus_device *dev, unsigned int *pers_grants);
int store_read_num_queues(struct xenbus_device *dev, unsigned int *requested_num_queues);

int store_ring_ref_init(struct xenbus_device *dev, struct store_ring_ref* ref, int ring_index);
void store_ring_ref_clear(struct store_ring_ref* ref);
const char* store_ring_ref_str(const struct store_ring_ref* ref);

int store_read_event_channel(struct xenbus_device *dev, struct store_ring_ref* ref, unsigned int *evtchn);
int store_read_ring_page_order(struct xenbus_device *dev, struct store_ring_ref* ref, unsigned int *ring_page_order);
int store_read_ring_ref(struct xenbus_device *dev, struct store_ring_ref* ref, int ref_index, unsigned int *ring_ref);

int store_write_sectors_num(struct xenbus_device *dev, unsigned long long sectors);

#endif /* __XEN_BLKBACK__STORE_H__ */

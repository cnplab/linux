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

#ifndef __XEN_NETBACK__STORE_H__
#define __XEN_NETBACK__STORE_H__


int store_write_init_info(struct xenbus_device *xdev);
int store_read_handle(struct xenbus_device *xdev,
		long *handle);
void store_read_rate(struct xenbus_device *dev,
		unsigned long *bytes, unsigned long *usec);
int store_read_mac(struct xenbus_device *dev,
		u8 mac[]);
int store_read_ctrl_ring_info(struct xenbus_device *xdev,
		grant_ref_t *ring_ref, unsigned int *evtchn);
int store_read_num_queues(struct xenbus_device *xdev,
		unsigned int *requested_num_queues);
int store_read_data_rings_info(struct xenbus_device *xdev,
		struct xenvif_queue *queue,
		unsigned long *tx_ring_ref, unsigned long *rx_ring_ref,
		unsigned int *tx_evtchn, unsigned int *rx_evtchn);
int store_read_vif_flags(struct xenbus_device *xdev,
		struct xenvif *vif);

void store_destroy(void);//TODO temporary

#endif /* __XEN_NETBACK__STORE_H__ */

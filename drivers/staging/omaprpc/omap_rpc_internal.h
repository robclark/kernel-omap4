/*
 * OMAP Remote Procedure Call Driver
 *
 * Copyright (C) 2011 Texas Instruments, Inc.
 *
 * Erik Rainey <erik.rainey@ti.com>
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#ifndef _OMAP_RPC_INTERNAL_H_
#define _OMAP_RPC_INTERNAL_H_

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/scatterlist.h>
#include <linux/slab.h>
#include <linux/idr.h>
#include <linux/fs.h>
#include <linux/poll.h>
#include <linux/cdev.h>
#include <linux/jiffies.h>
#include <linux/mutex.h>
#include <linux/wait.h>
#include <linux/skbuff.h>
#include <linux/sched.h>
#include <linux/completion.h>

#if defined(CONFIG_RPMSG) || defined(CONFIG_RPMSG_MODULE)
#include <linux/rpmsg.h>
#else
#error "OMAP RPC requireds RPMSG"
#endif

#if defined(CONFIG_RPC_OMAP) || defined(CONFIG_RPC_OMAP_MODULE)
#include <linux/omap_rpc.h>
#endif

#if defined(CONFIG_TI_TILER) || defined(CONFIG_TI_TILER_MODULE)
#include <mach/tiler.h>
#endif

#if defined(CONFIG_DRM_OMAP) || defined(CONFIG_DRM_OMAP_MODULE)
#include "../omapdrm/omap_dmm_tiler.h"
#endif

#if defined(CONFIG_DMA_SHARED_BUFFER) || defined(CONFIG_DMA_SHARED_BUFFER_MODULE)
#include <linux/dma-buf.h>
#endif

#if defined(CONFIG_ION_OMAP) || defined(CONFIG_ION_OMAP_MODULE)
#include <linux/omap_ion.h>
extern struct ion_device *omap_ion_device;
#if defined(CONFIG_PVR_SGX) || defined(CONFIG_PVR_SGX_MODULE)
#include "../../gpu/pvr/ion.h"
#endif
#endif

#if defined(CONFIG_TI_TILER) || defined(CONFIG_TI_TILER_MODULE)
#define OMAPRPC_USE_TILER
#else
#undef  OMAPRPC_USE_TILER
#endif

#if defined(CONFIG_DMA_SHARED_BUFFER) || defined(CONFIG_DMA_SHARED_BUFFER_MODULE)
#define OMAPRPC_USE_DMABUF
#undef  OMAPRPC_USE_RPROC_LOOKUP // genernic linux does not support yet.
#else
#undef  OMAPRPC_USE_DMABUF
#endif

#if defined(CONFIG_ION_OMAP) || defined(CONFIG_ION_OMAP_MODULE)
#define OMAPRPC_USE_ION
#define OMAPRPC_USE_RPROC_LOOKUP // android support this.
#if defined(CONFIG_PVR_SGX) || defined(CONFIG_PVR_SGX_MODULE)
#define OMAPRPC_USE_PVR
#else
#undef  OMAPRPC_USE_PVR
#endif
#else
#undef  OMAPRPC_USE_ION
#endif

// Testing and debugging defines
#define OMAPRPC_DEBUGGING
#undef OMAPRPC_USE_HASH
#undef OMAPRPC_PERF_MEASUREMENT

#if defined(OMAPRPC_DEBUGGING)
#define OMAPRPC_INFO(dev, fmt, ...)    dev_info(dev, fmt, ## __VA_ARGS__)
#define OMAPRPC_ERR(dev, fmt, ...)     dev_err(dev, fmt, ## __VA_ARGS__)
#else
#define OMAPRPC_INFO(dev, fmt, ...)
#define OMAPRPC_ERR(dev, fmt, ...)     dev_err(dev, fmt, ## __VA_ARGS__)
#endif

#ifdef CONFIG_PHYS_ADDR_T_64BIT
typedef u64 virt_addr_t;
#else
typedef u32 virt_addr_t;
#endif

typedef enum _omaprpc_service_state_e {
    OMAPRPC_SERVICE_STATE_DOWN,
    OMAPRPC_SERVICE_STATE_UP,
} omaprpc_service_state_e;

struct omaprpc_service_t {
    struct list_head list;
    struct cdev cdev;
    struct device *dev;
    struct rpmsg_channel *rpdev;
    int minor;
    struct list_head instance_list;
    struct mutex lock;
    struct completion comp;
    omaprpc_service_state_e state;
#if defined(OMAPRPC_USE_ION)
    struct ion_client *ion_client;
#endif
};

struct omaprpc_call_function_list_t {
    struct list_head list;
    struct omaprpc_call_function_t *function;
    u16 msgId;
};

struct omaprpc_instance_t {
    struct list_head list;
    struct omaprpc_service_t *rpcserv;
    struct sk_buff_head queue;
    struct mutex lock;
    wait_queue_head_t readq;
    struct completion reply_arrived;
    struct rpmsg_endpoint *ept;
    int transisioning;
    u32 dst;
    int state;
    u32 core;
#if defined(OMAPRPC_USE_ION)
    struct ion_client *ion_client;
#elif defined(OMAPRPC_USE_DMABUF)
    struct list_head dma_list;
#endif
    u16 msgId;
    struct list_head fxn_list;
};

#if defined(OMAPRPC_USE_DMABUF)
typedef struct _dma_info_t {
    struct list_head list;
    int fd;
    struct dma_buf *dbuf;
    struct dma_buf_attachment *attach;
    struct sg_table *sgt;
} dma_info_t;
#endif

#include "omap_rpc_htable.h"


/*!
 * A Wrapper function to translate local physical addresses to the remote core
 * memory maps. Initialially we can only use an internal static table until
 * rproc support querying.
 */
phys_addr_t rpmsg_local_to_remote_pa(uint32_t core, phys_addr_t pa);

/*!
 * This function translates all the pointers within the function call structure and the translation structures.
 */
int omaprpc_xlate_buffers(struct omaprpc_instance_t *rpc, struct omaprpc_call_function_t *function, int direction);

/*!
 *
 */
phys_addr_t omaprpc_buffer_lookup(struct omaprpc_instance_t *rpc, uint32_t core, virt_addr_t uva, virt_addr_t buva, void *reserved);


#endif


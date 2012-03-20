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
#include <linux/rpmsg.h>
#include <linux/rpmsg_rpc.h>
#include <linux/completion.h>

#if defined(CONFIG_TI_TILER)
#include <mach/tiler.h>
#endif

#if defined(CONFIG_DRM_OMAP)
#include <omap_dmm_tiler.h>
#endif

#if defined(CONFIG_ION_OMAP)
#include <linux/omap_ion.h>
extern struct ion_device *omap_ion_device;
#endif

#undef OMAPRPC_DEBUGGING
#undef OMAPRPC_USE_HASH
#undef OMAPRPC_PERF_MEASUREMENT

#ifdef CONFIG_PHYS_ADDR_T_64BIT
typedef u64 virt_addr_t;
#else
typedef u32 virt_addr_t;
#endif

typedef struct addr_htable_t {
    uint32_t collisions;
    uint32_t count;
    uint32_t pow;
    struct list_head *lines;
} addr_htable_t;

typedef struct addr_record {
    struct list_head node;      /**< Node For List Maintanence */
    long handle;                /**< Handle to either ION or SGX memory */
    virt_addr_t uva;            /**< User Virtual Address */
    virt_addr_t buva;           /**< Base User Virtual Address */
    phys_addr_t lpa;            /**< Local Physical Address */
    phys_addr_t rpa;            /**< Remote Physical Address */
} addr_record_t;

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
#if defined(CONFIG_ION_OMAP)
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
#if defined(CONFIG_ION_OMAP)
    struct ion_client *ion_client;
#endif
    u16 msgId;
    struct list_head fxn_list;
};

static struct class *omaprpc_class;
static dev_t omaprpc_dev;

/* store all remote rpc connection services (usually one per remoteproc) */
static struct idr omaprpc_services = IDR_INIT(omaprpc_services); // DEFINE_IDR(omaprpc_services);
static spinlock_t omaprpc_services_lock = __SPIN_LOCK_UNLOCKED(omaprpc_services_lock); //DEFINE_SPINLOCK(omaprpc_services_lock);
static struct list_head omaprpc_services_list = LIST_HEAD_INIT(omaprpc_services_list); //LIST_HEAD(omaprpc_services_list);

#if defined(OMAPRPC_USE_HASH)
static addr_htable_t omaprpc_aht = {0, 0, 8, NULL}; /* 2^8 sized hash table */
static struct mutex aht_lock;
#endif

#if defined(CONFIG_ION_OMAP)
#ifdef CONFIG_PVR_SGX
#include "../gpu/pvr/ion.h"
#endif
#endif

typedef struct _remote_mmu_region_t {
    phys_addr_t tiler_start;
    phys_addr_t tiler_end;
    phys_addr_t ion_1d_start;
    phys_addr_t ion_1d_end;
    phys_addr_t ion_1d_va;
} remote_mmu_region_t;

static remote_mmu_region_t regions[OMAPRPC_CORE_REMOTE_MAX] = {
    {0x60000000, 0x80000000, 0xBA300000, 0xBFD00000, 0x88000000}, // Tesla
    {0x60000000, 0x80000000, 0xBA300000, 0xBFD00000, 0x88000000}, // iMX
    {0x60000000, 0x80000000, 0xBA300000, 0xBFD00000, 0x88000000}, // MCU0 (SYS)
    {0x60000000, 0x80000000, 0xBA300000, 0xBFD00000, 0x88000000}, // MCU1 (APP)
    {0x60000000, 0x80000000, 0xBA300000, 0xBFD00000, 0x88000000}, // EVE
};
static u32 numCores = sizeof(regions)/sizeof(regions[0]);

#if defined(OMAPRPC_PERF_MEASUREMENT)
static struct timeval start_time;
static struct timeval end_time;
static long usec_elapsed;
#endif

static void list_delete(struct list_head *old, struct list_head *head)
{
    // remove the old from the list and update the head pointers
    if (head->next == old) {
        head->next = old->next;
    }
    if (head->prev == old) {
        head->prev = old->prev;
    }
    list_del(old);
}

static phys_addr_t rpmsg_local_to_remote_pa(uint32_t core, phys_addr_t pa)
{
    if (core < numCores)
    {
        if (regions[core].tiler_start <= pa && pa < regions[core].tiler_end)
            return pa;
        else if (regions[core].ion_1d_start <= pa && pa < regions[core].ion_1d_end)
            return (pa - regions[core].ion_1d_start) + regions[core].ion_1d_va;
    }
    return 0;
}

#if defined(OMAPRPC_USE_HASH)
static uint32_t htable_hash(addr_htable_t *aht, long key)
{
    uint32_t mask = (1 << aht->pow) - 1;
    key += (key << 10);
    key ^= (key >> 6);
    key += (key << 3);
    key ^= (key >> 11);
    key += (key << 15);
    return (uint32_t)key&mask;      /* creates a power of two range mask */
}

static int htable_get(addr_htable_t *aht, addr_record_t *ar)
{
    if (aht && aht->lines)
    {
        /* find the index */
        uint32_t index = htable_hash(aht, ar->handle);
        if (!list_empty(&aht->lines[index]))
        {
            struct list_head *pos = NULL;
            list_for_each(pos, &aht->lines[index])
            {
                addr_record_t *art = (addr_record_t *)pos;
                if (art->handle == ar->handle)
                {
                    memcpy(ar, art, sizeof(addr_record_t));
                    return 1;
                }
            }
        }
    }
    return 0;
}

static int htable_set(addr_htable_t *aht, addr_record_t *ar)
{
    if (aht && aht->lines)
    {
        /* find the index */
        uint32_t index = htable_hash(aht, ar->handle);

        /* is it already there? */
        if (list_empty(&aht->lines[index]))
        {
            /* add it to the list */
            list_add((struct list_head *)ar, &aht->lines[index]);
            return 1;
        }
        else
        {
            struct list_head *pos = NULL;
            int found = 0;
            /* search for an existing version */
            list_for_each(pos, &aht->lines[index])
            {
                addr_record_t *art = (addr_record_t *)pos;
                /* if keys match */
                if (art->handle == ar->handle)
                {
                    if (art->uva == ar->uva &&
                        art->lpa == ar->lpa &&
                        art->rpa == ar->rpa)
                    {
                        /* already set, ignore. */
                        return 1;
                    }
                    else
                    {
                        printk("OMAPRPC HTABLE: WARNING! HANDLE COLLISION!\n");
                        aht->collisions++;
                        return 0;
                    }
                }
            }

            if (found == 0)
            {
                list_add((struct list_head *)ar, &aht->lines[index]);
                return 1;
            }
        }
    }
    return 0;
}
#endif

static phys_addr_t omaprpc_buffer_lookup(struct omaprpc_instance_t *rpc, uint32_t core, virt_addr_t uva, virt_addr_t buva, void *reserved)
{
    phys_addr_t lpa = 0, rpa = 0;
    long uoff = uva - buva;           /* User VA - Base User VA = User Offset */
#if defined(OMAPRPC_USE_HASH)
    addr_record_t *ar = NULL;
#endif

#if defined(OMAPRPC_DEBUGGING)
    dev_info(rpc->rpcserv->dev, "CORE=%u BUVA=%p UVA=%p Uoff=%ld [0x%016lx] Hdl=%p\n", core, (void *)buva, (void *)uva, uoff, (ulong)uoff, reserved);
#endif

    if (uoff < 0)
    {
        dev_err(rpc->rpcserv->dev, "Offsets calculation for BUVA=%p from UVA=%p is a negative number. Bad parameters!\n", (void *)buva, (void *)uva);
        rpa = 0; // == NULL
        goto to_end;
    }

#if defined(OMAPRPC_USE_HASH)
    /* Allocate a node for the hash table */
    ar = kmalloc(sizeof(addr_record_t), GFP_KERNEL);
    if (ar) {
        int ret = 0;
        memset(ar, 0, sizeof(addr_record_t));
        ar->handle = (long)reserved;
        ar->uva    = uva;
        ar->buva   = buva;
        mutex_lock(&aht_lock);
        /* lookup up the Handle/UVA in the table to see if we know it */
        ret = htable_get(&omaprpc_aht, ar);
        mutex_unlock(&aht_lock);
        if (ret == 1)
        {
#if defined(OMAPRPC_DEBUGGING)
            dev_info(rpc->rpcserv->dev, "Found old translation in htable!\n");
#endif
            lpa = ar->lpa;
            rpa = ar->rpa;
            kfree(ar);
            goto to_end;
        }
    }
#endif


#if defined(CONFIG_ION_OMAP)
    if (reserved)
    {
        struct ion_handle *handle;
        ion_phys_addr_t paddr;
        size_t unused;
        int fd;

        /* is it an ion handle? */
        handle = (struct ion_handle *)reserved;
        if (!ion_phys(rpc->ion_client, handle, &paddr, &unused)) {
            lpa = (phys_addr_t)paddr;
#if defined(OMAPRPC_DEBUGGING)
            dev_info(rpc->rpcserv->dev, "Handle %p is an ION Handle to ARM PA %p (Uoff=%ld)\n", reserved, (void *)lpa, uoff);
#endif
            lpa+=uoff;
            goto to_va;
        }
 #ifdef CONFIG_PVR_SGX
        else
        {
            /* is it an sgx buffer wrapping an ion handle? */
            struct ion_client *pvr_ion_client;
            fd = (int)reserved;
            handle = PVRSRVExportFDToIONHandle(fd, &pvr_ion_client);
            if (handle && !ion_phys(pvr_ion_client, handle, &paddr, &unused)) {
                lpa = (phys_addr_t)paddr;
#if defined(OMAPRPC_DEBUGGING)
                dev_info(rpc->rpcserv->dev, "FD %d is an SGX Handle to ARM PA %p (Uoff=%ld)\n", (int)reserved, (void *)lpa, uoff);
#endif
                lpa+=uoff;
                goto to_va;
            }
        }
#endif
    }
#endif

    /* Ask the TILER to convert from virtual to physical */
    lpa = (phys_addr_t)tiler_virt2phys(uva);
to_va:
    /* convert the local physical address to remote physical address */
    rpa = rpmsg_local_to_remote_pa(core, lpa);
#if defined(OMAPRPC_USE_HASH)
    if (ar)
    {
        ar->handle = (long)reserved;
        ar->uva = uva;
        ar->lpa = lpa;
        ar->rpa = rpa;
        mutex_lock(&aht_lock);
        htable_set(&omaprpc_aht, ar);
        mutex_unlock(&aht_lock);
    }
#endif
to_end:
#if defined(OMAPRPC_DEBUGGING)
    dev_info(rpc->rpcserv->dev, "ARM VA %p == ARM PA %p => REMOTE[%u] PA %p (RESV %p)\n", (void *)uva, (void *)lpa, core, (void *)rpa, reserved);
#endif
    return rpa;
}

static int omaprpc_xlate_buffers(struct omaprpc_instance_t *rpc, struct omaprpc_call_function_t *function, int direction)
{
    int idx = 0, start = 0, inc = 1, limit = 0, ret = 0;
    uint32_t ptr_idx = 0, offset = 0, size = 0;
    uint8_t *base_ptrs[OMAPRPC_MAX_PARAMETERS]; // @NOTE not all the parameters are pointers so this may be sparse

    if (function->num_translations == 0)
        return 0;

    limit = function->num_translations;
    memset(base_ptrs, 0, sizeof(base_ptrs));
#if defined(OMAPRPC_DEBUGGING)
    dev_info(rpc->rpcserv->dev, "Operating on %d pointers\n", function->num_translations);
#endif
    /* we may have a failure during translation, in which case we need to unwind
       the whole operation from here */
restart:
    for (idx = start; idx != limit; idx+=inc)
    {
        /* conveinence variables */
        ptr_idx = function->translations[idx].index;
        offset  = function->translations[idx].offset;

        /* if the pointer index for this translation is invalid */
        if (ptr_idx >= OMAPRPC_MAX_PARAMETERS)
        {
            dev_err(rpc->rpcserv->dev, "Invalid parameter pointer index %u\n", ptr_idx);
            goto unwind;
        }
        else if (function->params[ptr_idx].type != OMAPRPC_PARAM_TYPE_PTR)
        {
            dev_err(rpc->rpcserv->dev, "Parameter index %u is not a pointer (type %u)\n", ptr_idx, function->params[ptr_idx].type);
            goto unwind;
        }

        size = function->params[ptr_idx].size;

        if (offset >= (size - sizeof(virt_addr_t)))
        {
            dev_err(rpc->rpcserv->dev, "Offset is larger than data area! (offset=%u size=%u)\n", offset, size);
            goto unwind;
        }

        if (function->params[ptr_idx].data == 0)
        {
            dev_err(rpc->rpcserv->dev, "Supplied user pointer is NULL!\n");
            goto unwind;
        }

        /* if the KVA pointer has not been mapped */
        if (base_ptrs[ptr_idx] == NULL)
        {
            uint32_t offset = 0;
            uint8_t *mkva = NULL;
#if defined(CONFIG_ION_OMAP)
            /* map the UVA (ION) pointer to KVA space */
            base_ptrs[ptr_idx] = (uint8_t *)ion_map_kernel(rpc->ion_client, (struct ion_handle *)function->params[ptr_idx].reserved);
#else
#error      "OMAPRPC Driver must have a base pointer translation mechanism"
#endif
            /* calc any offset if present */
            offset = function->params[ptr_idx].data - function->params[ptr_idx].base;
            /* set the modified kernel va equal to the base kernel va plus the offset */
            mkva = &base_ptrs[ptr_idx][offset];
#if defined(OMAPRPC_DEBUGGING)
            dev_info(rpc->rpcserv->dev, "Mapped UVA:%p to KVA:%p+OFF:%08x SIZE:%08x (MKVA:%p to END:%p)\n", (void *)function->params[ptr_idx].data, (void *)base_ptrs[ptr_idx], offset, size, (void *)mkva, (void *)&mkva[size]);
#endif
            /* modify the base pointer now*/
            base_ptrs[ptr_idx] = mkva;
        }

#if defined(OMAPRPC_DEBUGGING)
        dev_info(rpc->rpcserv->dev, "");
#endif

        /* if the KVA pointer is not NULL */
        if (base_ptrs[ptr_idx] != NULL)
        {
            if (direction == OMAPRPC_UVA_TO_RPA)
            {
                /* get the kernel virtual pointer to the pointer to translate */
                virt_addr_t kva = (virt_addr_t)&((base_ptrs[ptr_idx])[offset]);
                virt_addr_t uva = 0;
                virt_addr_t buva = (virt_addr_t)function->translations[idx].base;
                phys_addr_t rpa = 0;
                void       *reserved = (void *)function->translations[idx].reserved;

                /* make sure we won't cause an unalign mem access */
                if ((kva & 0x3) > 0) {
                    dev_err(rpc->rpcserv->dev, "ERROR: KVA %p is unaligned!\n",(void *)kva);
                    return -EADDRNOTAVAIL;
                }
                /* load the user's VA */
                uva = *(virt_addr_t *)kva;
#if defined(OMAPRPC_DEBUGGING)
                dev_info(rpc->rpcserv->dev, "Replacing UVA %p at KVA %p PTRIDX:%u OFFSET:%u IDX:%d\n", (void *)uva, (void *)kva, ptr_idx, offset, idx);
#endif
                /* calc the new RPA (remote physical address) */
                rpa = omaprpc_buffer_lookup(rpc, rpc->core, uva, buva, reserved);
                /* save the old value */
                function->translations[idx].reserved = uva;
                /* replace with new RPA */
                *(phys_addr_t *)kva = rpa;
#if defined(OMAPRPC_DEBUGGING)
                dev_info(rpc->rpcserv->dev, "Replaced UVA %p with RPA %p at KVA %p\n", (void *)uva, (void *)rpa, (void *)kva);
#endif
                if (rpa == 0) {
                    // need to unwind all operations..
                    direction = OMAPRPC_RPA_TO_UVA;
                    start = idx-1;
                    inc = -1;
                    limit = -1;
                    ret = -ENODATA;
                    goto restart;
                }
            }
            else if (direction == OMAPRPC_RPA_TO_UVA)
            {
                /* address of the pointer in memory */
                virt_addr_t kva = (virt_addr_t)&((base_ptrs[ptr_idx])[offset]);
                virt_addr_t uva = 0;
                phys_addr_t rpa = 0;
                /* make sure we won't cause an unalign mem access */
                if ((kva & 0x3) > 0)
                    return -EADDRNOTAVAIL;
                /* get what was there for debugging */
                rpa = *(phys_addr_t *)kva;
                /* conveinence value of uva */
                uva = (virt_addr_t)function->translations[idx].reserved;
                /* replace the translated value with the remember version */
                *(virt_addr_t *)kva = uva;
#if defined(OMAPRPC_DEBUGGING)
                dev_info(rpc->rpcserv->dev, "Replaced RPA %p with UVA %p at KVA %p\n", (void *)rpa, (void *)uva, (void *)kva);
#endif
                if (uva == 0) {
                    // need to unwind all operations..
                    direction = OMAPRPC_RPA_TO_UVA;
                    start = idx-1;
                    inc = -1;
                    limit = -1;
                    ret = -ENODATA;
                    goto restart;
                }
            }
        }
        else
        {
            dev_err(rpc->rpcserv->dev, "Failed to map UVA to KVA to do translation!\n");
            /* we can arrive here from multiple points, but the action is the
               same from everywhere */
unwind:
            if (direction == OMAPRPC_UVA_TO_RPA)
            {
                /* we've encountered an error which needs to unwind all the operations */
                dev_err(rpc->rpcserv->dev, "Unwinding UVA to RPA translations!\n");
                direction = OMAPRPC_RPA_TO_UVA;
                start = idx-1;
                inc = -1;
                limit = -1;
                ret = -ENOBUFS;
                goto restart;
            }
            else if (direction == OMAPRPC_RPA_TO_UVA)
            {
                /* there was a problem restoring the pointer, there's nothing to
                   do but to continue processing */
                continue;
            }
        }
    }
    /* unmap all the pointers that were mapped */
    for (idx = 0; idx < OMAPRPC_MAX_PARAMETERS; idx++)
    {
        if (base_ptrs[idx]) {
#if defined(CONFIG_ION_OMAP)
            ion_unmap_kernel(rpc->ion_client, (struct ion_handle *)function->params[idx].reserved);
#endif
            base_ptrs[idx] = NULL;
        }
    }
    return ret;
}

static void omaprpc_fxn_del(struct omaprpc_instance_t *rpc)
{
    /* Free any outstanding function calls */
    if (!list_empty(&rpc->fxn_list))
    {
        struct list_head *pos = NULL;
        struct omaprpc_call_function_list_t *last = NULL;
        struct omaprpc_call_function_list_t *node = NULL;

        mutex_lock(&rpc->lock);
        list_for_each(pos, &rpc->fxn_list)
        {
            node = (struct omaprpc_call_function_list_t *)pos;
            kfree(node->function);
            list_delete(pos, &rpc->fxn_list);
            if (last)
                kfree(last);
            last = node;
        }
        if (last != NULL)
            kfree(last);
        mutex_lock(&rpc->lock);
    }
}

static struct omaprpc_call_function_t *omaprpc_fxn_get(struct omaprpc_instance_t *rpc, u16 msgId)
{
    struct omaprpc_call_function_t *function = NULL;
    struct list_head *pos = NULL;
    struct omaprpc_call_function_list_t *node = NULL;
    mutex_lock(&rpc->lock);
    list_for_each(pos, &rpc->fxn_list)
    {
        node = (struct omaprpc_call_function_list_t *)pos;
#if defined(OMAPRPC_DEBUGGING)
        dev_info(rpc->rpcserv->dev, "Looking for msg %u, found msg %u\n", msgId, node->msgId);
#endif
        if (node->msgId == msgId) {
            function = node->function;
            list_delete(pos, &rpc->fxn_list);
            kfree(node);
            break;
        }
    }
    mutex_unlock(&rpc->lock);
    return function;
}

static int omaprpc_fxn_add(struct omaprpc_instance_t *rpc, struct omaprpc_call_function_t *function, u16 msgId)
{
    struct omaprpc_call_function_list_t *fxn = NULL;
    fxn = (struct omaprpc_call_function_list_t *)kmalloc(sizeof(struct omaprpc_call_function_list_t), GFP_KERNEL);
    if (fxn)
    {
        fxn->function = function;
        fxn->msgId = msgId;

        mutex_lock(&rpc->lock);
        list_add(&fxn->list, &rpc->fxn_list);
        mutex_unlock(&rpc->lock);
#if defined(OMAPRPC_DEBUGGING)
        dev_info(rpc->rpcserv->dev, "Added msg id %u to list", msgId);
#endif
    }
    else
    {
        dev_err(rpc->rpcserv->dev, "Failed to add function %p to list with id %d\n", function, msgId);
        return -ENOMEM;
    }
    return 0;
}

/* This is the callback from the remote core to this side */
static void omaprpc_cb(struct rpmsg_channel *rpdev, void *data, int len, void *priv, u32 src)
{
    struct omaprpc_msg_header_t *hdr = data;
    struct omaprpc_instance_t *rpc = priv;
    struct omaprpc_instance_handle_t *hdl;
    struct sk_buff *skb;
    char *buf = (char *)data;
    char *skbdata;
    u32 expected = 0;

#if defined(OMAPRPC_DEBUGGING)
    dev_info(rpc->rpcserv->dev, "OMAPRPC: incoming msg src %d len %d msg_type %d msg_len %d\n", src, len, hdr->msg_type, hdr->msg_len);
#endif
#if 0
    print_hex_dump(KERN_DEBUG, "OMAPRPC: RX: ", DUMP_PREFIX_NONE, 16, 1, data, len, true);
#endif
    expected = sizeof(struct omaprpc_msg_header_t);
    switch (hdr->msg_type)
    {
        case OMAPRPC_MSG_INSTANCE_CREATED:
        case OMAPRPC_MSG_INSTANCE_DESTROYED:
            expected += sizeof(struct omaprpc_instance_handle_t);
            break;
        case OMAPRPC_MSG_INSTANCE_INFO:
            expected += sizeof(struct omaprpc_instance_info_t);
            break;
    }

    if (len < expected) {
        dev_err(rpc->rpcserv->dev, "OMAPRPC: truncated message detected! Was %u bytes long, expected %u\n", len, expected);
        rpc->state = OMAPRPC_STATE_FAULT;
        return;
    }

    switch (hdr->msg_type) {
        case OMAPRPC_MSG_INSTANCE_CREATED:
            hdl = OMAPRPC_PAYLOAD(buf, omaprpc_instance_handle_t);
            if (hdl->status)
            {
                dev_err(rpc->rpcserv->dev, "OMAPRPC: Failed to connect to remote core! Status=%d\n", hdl->status);
                rpc->state = OMAPRPC_STATE_FAULT;
            }
            else
            {
                dev_info(rpc->rpcserv->dev, "OMAPRPC: Created addr %d status %d \n", hdl->endpoint_address, hdl->status);
                rpc->dst = hdl->endpoint_address; // only save the address if it connected.
                rpc->state = OMAPRPC_STATE_CONNECTED;
                rpc->core = OMAPRPC_CORE_MCU1; // default core
            }
            rpc->transisioning = 0;
            /* unblock the connect function */
            complete(&rpc->reply_arrived);
            break;
        case OMAPRPC_MSG_INSTANCE_DESTROYED:
            hdl = OMAPRPC_PAYLOAD(buf, omaprpc_instance_handle_t);
            if (hdr->msg_len < sizeof(*hdl))
            {
                dev_err(rpc->rpcserv->dev, "OMAPRPC: disconnect message was incorrect size!\n");
                rpc->state = OMAPRPC_STATE_FAULT;
                break;
            }
            dev_info(rpc->rpcserv->dev, "OMAPRPC: endpoint %d disconnected!\n", hdl->endpoint_address);
            rpc->state = OMAPRPC_STATE_DISCONNECTED;
            rpc->dst = 0;
            rpc->transisioning = 0;
            /* unblock the disconnect ioctl call */
            complete(&rpc->reply_arrived);
            break;
        case OMAPRPC_MSG_INSTANCE_INFO:
            break;
        case OMAPRPC_MSG_CALL_FUNCTION:  // @TODO REMOVE HACK from OMX/RCM frankenstein
        case OMAPRPC_MSG_FUNCTION_RETURN:

#if defined(OMAPRPC_PERF_MEASUREMENT)
            do_gettimeofday(&end_time);
            usec_elapsed = (end_time.tv_sec - start_time.tv_sec) * 1000000 + end_time.tv_usec - start_time.tv_usec;
            dev_info(rpc->rpcserv->dev, "write to callback took %lu usec\n", usec_elapsed);
#endif

            skb = alloc_skb(hdr->msg_len, GFP_KERNEL);
            if (!skb) {
                dev_err(rpc->rpcserv->dev, "OMAPRPC: alloc_skb err: %u\n", hdr->msg_len);
                break;
            }
            skbdata = skb_put(skb, hdr->msg_len);
            memcpy(skbdata, hdr->msg_data, hdr->msg_len);

            mutex_lock(&rpc->lock);
#if defined(OMAPRPC_PERF_MEASUREMENT)
            /* capture the time delay between callback and read */
            do_gettimeofday(&start_time);
#endif
            skb_queue_tail(&rpc->queue, skb);
            mutex_unlock(&rpc->lock);
            /* wake up any blocking processes, waiting for new data */
            wake_up_interruptible(&rpc->readq);
            break;
        default:
            dev_warn(rpc->rpcserv->dev, "OMAPRPC: unexpected msg type: %d\n", hdr->msg_type);
            break;
    }
}

static int omaprpc_connect(struct omaprpc_instance_t *rpc, struct omaprpc_create_instance_t *connect)
{
    struct omaprpc_service_t *rpcserv = rpc->rpcserv;
    char kbuf[512];
    struct omaprpc_msg_header_t *hdr = (struct omaprpc_msg_header_t *)&kbuf[0];
    int ret = 0;
    u32 len = 0;

    /* Return "is connected" if connected */
    if (rpc->state == OMAPRPC_STATE_CONNECTED) {
        dev_dbg(rpcserv->dev, "OMAPRPC: endpoint already connected\n");
        return -EISCONN;
    }

    /* Initialize the Connection Request Message */
    hdr->msg_type = OMAPRPC_MSG_CREATE_INSTANCE;
    hdr->msg_len = sizeof(struct omaprpc_create_instance_t);
    memcpy(hdr->msg_data, connect, hdr->msg_len);
    len = sizeof(struct omaprpc_msg_header_t) + hdr->msg_len;

    /* Initialize a completion object */
    init_completion(&rpc->reply_arrived);

    /* indicate that we are waiting for a completion */
    rpc->transisioning = 1;

    /* send a conn req to the remote RPC connection service. use
     * the new local address that was just allocated by ->open */
    ret = rpmsg_send_offchannel(rpcserv->rpdev, rpc->ept->addr, rpcserv->rpdev->dst, (char *)kbuf, len);
    if (ret > 0) {
        dev_err(rpcserv->dev, "OMAPRPC: rpmsg_send failed: %d\n", ret);
        return ret;
    }

    /* wait until a connection reply arrives or 5 seconds elapse */
    ret = wait_for_completion_interruptible_timeout(&rpc->reply_arrived, msecs_to_jiffies(5000));
    if (rpc->state == OMAPRPC_STATE_CONNECTED)
        return 0;

    if (rpc->state == OMAPRPC_STATE_FAULT)
        return -ENXIO;

    if (ret > 0) {
        dev_err(rpcserv->dev, "OMAPRPC: premature wakeup: %d\n", ret);
        return -EIO;
    }

    return -ETIMEDOUT;
}

static long omaprpc_ioctl(struct file *filp, unsigned int cmd, unsigned long arg)
{
    struct omaprpc_instance_t *rpc = filp->private_data;
    struct omaprpc_service_t *rpcserv = rpc->rpcserv;
    struct omaprpc_create_instance_t connect;
    int ret = 0;

    dev_dbg(rpcserv->dev, "OMAPRPC: %s: cmd %d, arg 0x%lx\n", __func__, cmd, arg);

    /* if the magic was not present, tell the caller that we are not a typewritter[sic]! */
    if (_IOC_TYPE(cmd) != OMAPRPC_IOC_MAGIC)
        return -ENOTTY;

    /* if the number of the command is larger than what we support, also tell the caller that we are not a typewriter[sic]! */
    if (_IOC_NR(cmd) > OMAPRPC_IOC_MAXNR)
        return -ENOTTY;

    switch (cmd) {
        case OMAPRPC_IOC_CREATE:
            /* copy the connection buffer from the user */
            ret = copy_from_user(&connect, (char __user *) arg, sizeof(connect));
            if (ret)
            {
                dev_err(rpcserv->dev, "OMAPRPC: %s: %d: copy_from_user fail: %d\n", __func__, _IOC_NR(cmd), ret);
                ret = -EFAULT;
            }
            else
            {
                /* make sure user input is null terminated */
                connect.name[sizeof(connect.name) - 1] = '\0';

                /* connect to the remote core */
                ret = omaprpc_connect(rpc, &connect);
            }
            break;
        case OMAPRPC_IOC_DESTROY:
            break;
#if defined(CONFIG_ION_OMAP)
        case OMAPRPC_IOC_IONREGISTER:
        {
            struct ion_fd_data data;
            if (copy_from_user(&data, (char __user *) arg, sizeof(data))) {
                ret = -EFAULT;
                dev_err(rpcserv->dev,
                    "OMAPRPC: %s: %d: copy_from_user fail: %d\n", __func__,
                    _IOC_NR(cmd), ret);
            }
            data.handle = ion_import_fd(rpc->ion_client, data.fd);
            if (IS_ERR(data.handle))
                data.handle = NULL;
            if (copy_to_user((char __user *)arg, &data, sizeof(data))) {
                ret = -EFAULT;
                dev_err(rpcserv->dev,
                    "OMAPRPC: %s: %d: copy_to_user fail: %d\n", __func__,
                    _IOC_NR(cmd), ret);
            }
            break;
        }
        case OMAPRPC_IOC_IONUNREGISTER:
        {
            struct ion_fd_data data;
            if (copy_from_user(&data, (char __user *) arg, sizeof(data))) {
                ret = -EFAULT;
                dev_err(rpcserv->dev,
                    "OMAPRPC: %s: %d: copy_from_user fail: %d\n", __func__,
                    _IOC_NR(cmd), ret);
            }
            ion_free(rpc->ion_client, data.handle);
            if (copy_to_user((char __user *)arg, &data, sizeof(data))) {
                ret = -EFAULT;
                dev_err(rpcserv->dev,
                    "OMAPRPC: %s: %d: copy_to_user fail: %d\n", __func__,
                    _IOC_NR(cmd), ret);
            }
            break;
        }
#endif
        default:
            dev_warn(rpcserv->dev, "OMAPRPC: unhandled ioctl cmd: %d\n", cmd);
            break;
    }

    return ret;
}

static int omaprpc_open(struct inode *inode, struct file *filp)
{
    struct omaprpc_service_t *rpcserv;
    struct omaprpc_instance_t *rpc;

    /* get the service pointer out of the inode */
    rpcserv = container_of(inode->i_cdev, struct omaprpc_service_t, cdev);

    /* Return EBUSY if we are down and if non-blocking or waiting for something */
    if (rpcserv->state == OMAPRPC_SERVICE_STATE_DOWN)
        if (filp->f_flags & O_NONBLOCK || wait_for_completion_interruptible(&rpcserv->comp))
            return -EBUSY;

    /* Create a new instance */
    rpc = kzalloc(sizeof(*rpc), GFP_KERNEL);
    if (!rpc)
        return -ENOMEM;

    /* Initialize the instance mutex */
    mutex_init(&rpc->lock);

    /* Initialize the queue head for the socket buffers */
    skb_queue_head_init(&rpc->queue);

    /* Initialize the reading queue */
    init_waitqueue_head(&rpc->readq);

    /* Save the service pointer within the instance for later reference */
    rpc->rpcserv = rpcserv;
    rpc->state = OMAPRPC_STATE_DISCONNECTED;
    rpc->transisioning = 0;

    /* Initialize the current msg id */
    rpc->msgId = 0;

    /* Initialize the remember function call list */
    INIT_LIST_HEAD(&rpc->fxn_list);

    /* assign a new, unique, local address and associate the instance with it */
    rpc->ept = rpmsg_create_ept(rpcserv->rpdev, omaprpc_cb, rpc, RPMSG_ADDR_ANY);
    if (!rpc->ept) {
        dev_err(rpcserv->dev, "OMAPRPC: create ept failed\n");
        kfree(rpc);
        return -ENOMEM;
    }
#if defined(CONFIG_ION_OMAP)
    /* get a handle to the ion client for RPC buffers */
    rpc->ion_client = ion_client_create(omap_ion_device,
                        (1<< ION_HEAP_TYPE_CARVEOUT) |
                        (1 << OMAP_ION_HEAP_TYPE_TILER),
                        "rpmsg-rpc");
#endif

    /* remember rpc in filp's private data */
    filp->private_data = rpc;

    /* Add the instance to the service's list */
    mutex_lock(&rpcserv->lock);
    list_add(&rpc->list, &rpcserv->instance_list);
    mutex_unlock(&rpcserv->lock);

    dev_info(rpcserv->dev, "OMAPRPC: local addr assigned: 0x%x\n", rpc->ept->addr);

    return 0;
}

static int omaprpc_release(struct inode *inode, struct file *filp)
{
    char kbuf[512];
    u32 len = 0;
    int ret = 0;
    if (inode && filp)
    {
        struct omaprpc_instance_t *rpc = filp->private_data;
        if (rpc)
        {
            /* a conveinence pointer to service */
            struct omaprpc_service_t *rpcserv = rpc->rpcserv;

#if defined(OMAPRPC_DEBUGGING)
            dev_info(rpcserv->dev, "Releasing Instance %p, in state %d\n", rpc, rpc->state);
#endif

            /* if we are in a normal state */
            if (rpc->state != OMAPRPC_STATE_FAULT)
            {
                /* if we have connected already */
                if (rpc->state == OMAPRPC_STATE_CONNECTED) {
                    struct omaprpc_msg_header_t *hdr = (struct omaprpc_msg_header_t *)&kbuf[0];
                    struct omaprpc_instance_handle_t *handle = OMAPRPC_PAYLOAD(kbuf, omaprpc_instance_handle_t);

                    /* Format a disconnect message */
                    hdr->msg_type = OMAPRPC_MSG_DESTROY_INSTANCE;
                    hdr->msg_len = sizeof(u32);
                    handle->endpoint_address = rpc->dst; // end point address
                    handle->status = 0;
                    len = sizeof(struct omaprpc_msg_header_t) + hdr->msg_len;

                    dev_info(rpcserv->dev, "OMAPRPC: Disconnecting from RPC service at %d\n", rpc->dst);

                    /* send the msg to the remote RPC connection service */
                    ret = rpmsg_send_offchannel(rpcserv->rpdev, rpc->ept->addr,
                                    rpcserv->rpdev->dst, (char *)kbuf, len);
                    if (ret) {
                        dev_err(rpcserv->dev, "OMAPRPC: rpmsg_send failed: %d\n", ret);
                        return ret;
                    }

                    // @TODO Should I wait for a message to come back?
                    wait_for_completion(&rpc->reply_arrived);

                }

                /* Destroy the local endpoint */
                if (rpc->ept) {
                    rpmsg_destroy_ept(rpc->ept);
                    rpc->ept = NULL;
                }
            }

#if defined(CONFIG_ION_OMAP)
            if (rpc->ion_client) {
                /* Destroy our local client to ion */
                ion_client_destroy(rpc->ion_client);
                rpc->ion_client = NULL;
            }
#endif
            /* Remove the function list */
            omaprpc_fxn_del(rpc);

            /* Remove the instance from the service's list */
            mutex_lock(&rpcserv->lock);
            list_delete(&rpc->list, &rpcserv->instance_list);
            mutex_unlock(&rpcserv->lock);

            dev_info(rpcserv->dev, "OMAPRPC: Instance %p has been deleted!\n", rpc);

            if (list_empty(&rpcserv->instance_list)) {
                dev_info(rpcserv->dev, "OMAPRPC: All instances have been removed!\n");
            }

            /* Delete the instance memory */
            filp->private_data = NULL;
            memset(rpc, 0xFE, sizeof(struct omaprpc_instance_t));
            kfree(rpc);
            rpc = NULL;
        }
    }
    return 0;
}

static ssize_t omaprpc_read(struct file *filp, char __user *buf, size_t len, loff_t *offp)
{
    struct omaprpc_instance_t *rpc = filp->private_data;
    struct omaprpc_packet_t *packet = NULL;
    struct omaprpc_parameter_t *parameters = NULL;
    struct omaprpc_call_function_t *function = NULL;
    struct omaprpc_function_return_t returned;
    struct sk_buff *skb = NULL;
    int ret = 0;
    int use = sizeof(returned);

    /* too big */
    if (len > use) {
        ret = -EOVERFLOW;
        goto failure;
    }

    /* too small */
    if (len < use) {
        ret = -EINVAL;
        goto failure;
    }

    /* locked */
    if (mutex_lock_interruptible(&rpc->lock)) {
        ret = -ERESTARTSYS;
        goto failure;
    }

    /* error state */
    if (rpc->state == OMAPRPC_STATE_FAULT) {
        mutex_unlock(&rpc->lock);
        ret = -ENXIO;
        goto failure;
    }

    /* not connected to the remote side... */
    if (rpc->state != OMAPRPC_STATE_CONNECTED) {
        mutex_unlock(&rpc->lock);
        ret = -ENOTCONN;
        goto failure;
    }

    /* nothing to read ? */
    if (skb_queue_empty(&rpc->queue)) {
        mutex_unlock(&rpc->lock);
        /* non-blocking requested ? return now */
        if (filp->f_flags & O_NONBLOCK) {
            ret = -EAGAIN;
            goto failure;
        }
        /* otherwise block, and wait for data */
        if (wait_event_interruptible(rpc->readq, (!skb_queue_empty(&rpc->queue) || rpc->state == OMAPRPC_STATE_FAULT))) {
            ret = -ERESTARTSYS;
            goto failure;
        }
        /* re-grab the lock */
        if (mutex_lock_interruptible(&rpc->lock)) {
            ret = -ERESTARTSYS;
            goto failure;
        }
    }

    /* a fault happened while we waited. */
    if (rpc->state == OMAPRPC_STATE_FAULT) {
        mutex_unlock(&rpc->lock);
        ret = -ENXIO;
        goto failure;
    }

    /* pull the buffer out of the queue */
    skb = skb_dequeue(&rpc->queue);
    if (!skb) {
        mutex_unlock(&rpc->lock);
        dev_err(rpc->rpcserv->dev, "OMAPRPC: skb was NULL when dequeued, possible race condition!\n");
        ret = -EIO;
        goto failure;
    }

#if defined(OMAPRPC_PERF_MEASUREMENT)
    do_gettimeofday(&end_time);
    usec_elapsed = (end_time.tv_sec - start_time.tv_sec) * 1000000 + end_time.tv_usec - start_time.tv_usec;
    dev_info(rpc->rpcserv->dev, "callback to read took %lu usec\n", usec_elapsed);
#endif

    /* unlock the instances */
    mutex_unlock(&rpc->lock);

    packet = (struct omaprpc_packet_t *)skb->data;
    parameters = (struct omaprpc_parameter_t *)packet->data;

    /* pull the function memory from the list */
    function = omaprpc_fxn_get(rpc, packet->msg_id);
    if (function)
    {
        if (function->num_translations > 0)
        {
            /* Untranslate the PA pointers back to the ARM ION handles */
            ret = omaprpc_xlate_buffers(rpc, function, OMAPRPC_RPA_TO_UVA);
            if (ret < 0) {
                goto failure;
            }
        }
    }
    returned.func_index = OMAPRPC_FXN_MASK(packet->fxn_idx);
    returned.status     = packet->result;

    /* copy the kernel buffer to the user side */
    if (copy_to_user(buf, &returned, use)) {
        dev_err(rpc->rpcserv->dev, "OMAPRPC: %s: copy_to_user fail\n", __func__);
        ret = -EFAULT;
    }
    else
        ret = use;
failure:
    if (function)
        kfree(function);
    if (skb)
        kfree_skb(skb);

    return ret;
 }

static ssize_t omaprpc_write(struct file *filp,
                             const char __user *ubuf,
                             size_t len,
                             loff_t *offp)
{
    struct omaprpc_instance_t *rpc = filp->private_data;
    struct omaprpc_service_t *rpcserv = rpc->rpcserv;
    struct omaprpc_msg_header_t *hdr = NULL;
    struct omaprpc_call_function_t *function = NULL;
    struct omaprpc_packet_t *packet = NULL;
    struct omaprpc_parameter_t *parameters = NULL;
    char kbuf[512];
    int use = 0, ret = 0, param = 0;

    // incorrect parameter
    if (len < sizeof(struct omaprpc_call_function_t)) {
        ret = -ENOTSUPP;
        goto failure;
    }

    // over OMAPRPC_MAX_TRANSLATIONS! too many!
    if (len > (sizeof(struct omaprpc_call_function_t) + OMAPRPC_MAX_TRANSLATIONS*sizeof(struct omaprpc_param_translation_t))) {
        ret = -ENOTSUPP;
        goto failure;
    }

    /* check the state of the driver */
    if (rpc->state != OMAPRPC_STATE_CONNECTED) {
        ret = -ENOTCONN;
        goto failure;
    }

#if defined(OMAPRPC_DEBUGGING)
    dev_info(rpcserv->dev, "OMAPRPC: Allocating local function call copy for %u bytes\n", len);
#endif

    function = (struct omaprpc_call_function_t *)kzalloc(len, GFP_KERNEL);
    if (function == NULL) {
        /* could not allocate enough memory to cover the transaction */
        ret = -ENOMEM;
        goto failure;
    }

    /* copy the user packet to out memory */
    if (copy_from_user(function, ubuf, len)) {
        ret = -EMSGSIZE;
        goto failure;
    }

    /* increment the message ID and wrap if needed */
    rpc->msgId = (rpc->msgId + 1) & 0xFFFF;

    memset(kbuf, 0, sizeof(kbuf));
    hdr = (struct omaprpc_msg_header_t *)kbuf;
    hdr->msg_type = OMAPRPC_MSG_CALL_FUNCTION;
    hdr->msg_flags = 0;
    hdr->msg_len = sizeof(struct omaprpc_packet_t);
    packet = OMAPRPC_PAYLOAD(kbuf, omaprpc_packet_t);
    packet->desc = OMAPRPC_DESC_EXEC_SYNC;
    packet->msg_id = rpc->msgId;
    packet->pool_id = OMAPRPC_POOLID_DEFAULT;
    packet->job_id = OMAPRPC_JOBID_DISCRETE;
    packet->fxn_idx = OMAPRPC_SET_FXN_IDX(function->func_index);
    packet->result = 0;
    packet->data_size = sizeof(struct omaprpc_parameter_t) * function->num_params;
    parameters = (struct omaprpc_parameter_t *)packet->data;
    for (param = 0; param < function->num_params; param++)
    {
        parameters[param].size = function->params[param].size;
        if (function->params[param].type == OMAPRPC_PARAM_TYPE_PTR)
        {
            /* internally the buffer translations takes care of the offsets */
            void *reserved = (void *)function->params[param].reserved;

            parameters[param].data = omaprpc_buffer_lookup(rpc,
                                                           rpc->core,
                                                           function->params[param].data,
                                                           function->params[param].base,
                                                           reserved);
        }
        else if (function->params[param].type == OMAPRPC_PARAM_TYPE_ATOMIC)
            parameters[param].data = function->params[param].data;
        else
        {
            ret = -ENOTSUPP;
            goto failure;
        }
    }

    /* Compute the size of the RPMSG packet */
    use = sizeof(*hdr) + hdr->msg_len + packet->data_size;

    /* failed to provide the translation data */
    if (function->num_translations > 0 && len < (sizeof(struct omaprpc_call_function_t)+(function->num_translations*sizeof(struct omaprpc_param_translation_t))))
    {
        ret = -ENXIO;
        goto failure;
    }

    /* If there are pointers to translate for the user, do so now */
    if (function->num_translations > 0)
    {
        /* alter our copy of function and the user's parameters so that we can send to remote cores */
        ret = omaprpc_xlate_buffers(rpc, function, OMAPRPC_UVA_TO_RPA);
        if (ret < 0)
        {
            dev_err(rpcserv->dev, "OMAPRPC: ERROR: Failed to translate all pointers for remote core!\n");
            goto failure;
        }
    }
#if 0
    print_hex_dump(KERN_DEBUG, "OMAPRPC: TX: ", DUMP_PREFIX_NONE, 16, 1, kbuf, use, true);
#endif

    /* save the function data */
    ret = omaprpc_fxn_add(rpc, function, rpc->msgId);
    if (ret < 0) {
        /* unwind */
        omaprpc_xlate_buffers(rpc, function, OMAPRPC_RPA_TO_UVA);
        goto failure;
    }

#if defined(OMAPRPC_PERF_MEASUREMENT)
    /* capture the time delay between write and callback */
    do_gettimeofday(&start_time);
#endif

    /* Send the msg */
    ret = rpmsg_send_offchannel(rpcserv->rpdev, rpc->ept->addr, rpc->dst, kbuf, use);
    if (ret) {
        dev_err(rpcserv->dev, "OMAPRPC: rpmsg_send failed: %d\n", ret);
        /* remove the function data that we just saved*/
        omaprpc_fxn_get(rpc, rpc->msgId);
        /* unwind */
        omaprpc_xlate_buffers(rpc, function, OMAPRPC_RPA_TO_UVA);
        goto failure;
    }
#if defined(OMAPRPC_DEBUGGING)
    dev_info(rpcserv->dev, "OMAPRPC: Send msg to remote endpoint %u\n", rpc->dst);
#endif
failure:
    if (ret >= 0)
        ret = len;
    else
    {
        if (function)
            kfree(function);
    }

    return ret; /* return the length of the data written to us */
}

static unsigned int omaprpc_poll(struct file *filp, struct poll_table_struct *wait)
{
    struct omaprpc_instance_t *rpc = filp->private_data;
    unsigned int mask = 0;

    /* grab the mutex so we can check the queue */
    if (mutex_lock_interruptible(&rpc->lock))
        return -ERESTARTSYS;

    /* Wait for items to enter the queue */
    poll_wait(filp, &rpc->readq, wait);
    if (rpc->state == OMAPRPC_STATE_FAULT) {
        mutex_unlock(&rpc->lock);
        return -ENXIO; /* The remote service died somehow */
    }

    /* if the queue is not empty set the poll bit correctly */
    if (!skb_queue_empty(&rpc->queue))
        mask |= (POLLIN | POLLRDNORM);

    /* @TODO: implement missing rpmsg virtio functionality here */
    if (true)
        mask |= POLLOUT | POLLWRNORM;

    mutex_unlock(&rpc->lock);

    return mask;
}

static const struct file_operations omaprpc_fops = {
    .owner          = THIS_MODULE,
    .open           = omaprpc_open,
    .release        = omaprpc_release,
    .unlocked_ioctl = omaprpc_ioctl,
    .read           = omaprpc_read,
    .write          = omaprpc_write,
    .poll           = omaprpc_poll,
};

static int omaprpc_probe(struct rpmsg_channel *rpdev)
{
    int ret, major, minor;
    struct omaprpc_service_t *rpcserv = NULL, *tmp;

    dev_info(&rpdev->dev, "OMAPRPC: Probing service with src %u dst %u\n", rpdev->src, rpdev->dst);

again: /* SMP systems could race device probes */

    /* allocate the memory for an integer ID */
    if (!idr_pre_get(&omaprpc_services, GFP_KERNEL)) {
        dev_err(&rpdev->dev, "OMAPRPC: idr_pre_get failed\n");
        return -ENOMEM;
    }

    /* dynamically assign a new minor number */
    spin_lock(&omaprpc_services_lock);
    ret = idr_get_new(&omaprpc_services, rpcserv, &minor);
    if (ret == -EAGAIN) {
        spin_unlock(&omaprpc_services_lock);
        dev_err(&rpdev->dev, "OMAPRPC: Race to the new idr memory! (ret=%d)\n", ret);
        goto again;
    } else if (ret != 0) { // probably -ENOSPC
        spin_unlock(&omaprpc_services_lock);
        dev_err(&rpdev->dev, "OMAPRPC: failed to idr_get_new: %d\n", ret);
        return ret;
    }

    /* look for an already created rpc service and use that if the minor number matches */
    list_for_each_entry(tmp, &omaprpc_services_list, list) {
        if (tmp->minor == minor) {
            rpcserv = tmp;
            idr_replace(&omaprpc_services, rpcserv, minor);
            break;
        }
    }
    spin_unlock(&omaprpc_services_lock);

    /* if we replaced a device, skip the creation */
    if (rpcserv)
        goto serv_up;

    /* Create a new character device and add it to the kernel /dev fs */
    rpcserv = kzalloc(sizeof(*rpcserv), GFP_KERNEL);
    if (!rpcserv) {
        dev_err(&rpdev->dev, "OMAPRPC: kzalloc failed\n");
        ret = -ENOMEM;
        goto rem_idr;
    }

    /* Replace the pointer for the minor number, it shouldn't have existed
    (or was associated with NULL previously) so this is really an assignment */
    spin_lock(&omaprpc_services_lock);
    idr_replace(&omaprpc_services, rpcserv, minor);
    spin_unlock(&omaprpc_services_lock);

    /* Initialize the instance list in the service */
    INIT_LIST_HEAD(&rpcserv->instance_list);

    /* Initialize the Mutex */
    mutex_init(&rpcserv->lock);

    /* Initialize the Completion lock */
    init_completion(&rpcserv->comp);

    /* Add this service to the list of services */
    list_add(&rpcserv->list, &omaprpc_services_list);

    /* get the assigned major number from the dev_t */
    major = MAJOR(omaprpc_dev);

    /* Create the character device */
    cdev_init(&rpcserv->cdev, &omaprpc_fops);
    rpcserv->cdev.owner = THIS_MODULE;

    /* Add the character device to the kernel */
    ret = cdev_add(&rpcserv->cdev, MKDEV(major, minor), 1);
    if (ret) {
        dev_err(&rpdev->dev, "OMAPRPC: cdev_add failed: %d\n", ret);
        goto free_rpc;
    }

    /* Create the /dev sysfs entry */
    rpcserv->dev = device_create(omaprpc_class, &rpdev->dev,
            MKDEV(major, minor), NULL,
            rpdev->id.name);
    if (IS_ERR(rpcserv->dev)) {
        ret = PTR_ERR(rpcserv->dev);
        dev_err(&rpdev->dev, "OMAPRPC: device_create failed: %d\n", ret);
        goto clean_cdev;
    }
serv_up:
    rpcserv->rpdev = rpdev;
    rpcserv->minor = minor;
    rpcserv->state = OMAPRPC_SERVICE_STATE_UP;

    /* Associate the service with the sysfs entry, this will be allow container_of to get the service poitner */
    dev_set_drvdata(&rpdev->dev, rpcserv);

    /* Signal that the driver setup is complete */
    complete_all(&rpcserv->comp);

    dev_info(rpcserv->dev, "OMAPRPC: new RPC connection srv channel: %u -> %u!\n",
                        rpdev->src, rpdev->dst);
    return 0;

clean_cdev:
    /* Failed to create sysfs entry, delete character device */
    cdev_del(&rpcserv->cdev);
free_rpc:
    /* Delete the allocated memory for the service */
    kfree(rpcserv);
rem_idr:
    /* Remove the minor number from our integer ID pool */
    spin_lock(&omaprpc_services_lock);
    idr_remove(&omaprpc_services, minor);
    spin_unlock(&omaprpc_services_lock);
    /* Return the set error */
    return ret;
}

static void __devexit omaprpc_remove(struct rpmsg_channel *rpdev)
{
    struct omaprpc_service_t *rpcserv = dev_get_drvdata(&rpdev->dev);
    int major = MAJOR(omaprpc_dev);
    struct omaprpc_instance_t *rpc = NULL;

    if (rpcserv)
    {
        dev_info(rpcserv->dev, "OMAPRPC: removing rpmsg omaprpc driver %u.%u\n", major,rpcserv->minor);

        spin_lock(&omaprpc_services_lock);
        idr_remove(&omaprpc_services, rpcserv->minor);
        spin_unlock(&omaprpc_services_lock);

        mutex_lock(&rpcserv->lock);

        /* If there are no instances in the list, just teardown */
        if (list_empty(&rpcserv->instance_list)) {
            device_destroy(omaprpc_class, MKDEV(major, rpcserv->minor));
            cdev_del(&rpcserv->cdev);
            list_del(&rpcserv->list);
            mutex_unlock(&rpcserv->lock);
            dev_info(&rpdev->dev, "OMAPRPC: no instances, removed driver!\n");
            kfree(rpcserv);
            return;
        }

        /*
         * If there are rpc instances that means that this is a recovery operation.
         * Don't clean the rpcserv. Each instance may be in a weird state.
        */
        init_completion(&rpcserv->comp);
        rpcserv->state = OMAPRPC_SERVICE_STATE_DOWN;
        list_for_each_entry(rpc, &rpcserv->instance_list, list) {
            dev_info(rpcserv->dev, "Instance %p in state %d\n", rpc, rpc->state);
            /* set rpc instance to fault state */
            rpc->state = OMAPRPC_STATE_FAULT;
            /* complete any on-going transactions */
            if ((rpc->state == OMAPRPC_STATE_CONNECTED ||
                 rpc->state == OMAPRPC_STATE_DISCONNECTED) &&
                rpc->transisioning)
            {
                /* we were in the middle of connecting or disconnecting */
                complete_all(&rpc->reply_arrived);
            }
            /* wake up anyone waiting on a read */
            wake_up_interruptible(&rpc->readq);
        }
        mutex_unlock(&rpcserv->lock);

        dev_info(&rpdev->dev, "OMAPRPC: removed rpmsg omaprpc driver.\n");
    }
    else
    {
        dev_err(&rpdev->dev, "Service was NULL\n");
    }
}

static void omaprpc_driver_cb(struct rpmsg_channel *rpdev,
                                void *data,
                                int len,
                                void *priv,
                                u32 src)
{
    dev_warn(&rpdev->dev, "OMAPRPC: Driver Callback!\n");
    /* @TODO capture the data in the callback to give back to the user? */
}

static struct rpmsg_device_id omaprpc_id_table[] = {
    { .name = "omaprpc-imx" }, /* app-m3 */
    { .name = "omaprpc-dsp" }, /* dsp */
    { },
};
MODULE_DEVICE_TABLE(platform, omaprpc_id_table);

static struct rpmsg_driver omaprpc_driver = {
    .drv.name   = KBUILD_MODNAME,
    .drv.owner  = THIS_MODULE,
    .id_table   = omaprpc_id_table,
    .probe      = omaprpc_probe,
    .remove     = __devexit_p(omaprpc_remove),
    .callback   = omaprpc_driver_cb,
};

static int __init omaprpc_init(void)
{
    int ret;

    ret = alloc_chrdev_region(&omaprpc_dev, 0, OMAPRPC_CORE_REMOTE_MAX,
                              KBUILD_MODNAME);
    if (ret) {
        pr_err("OMAPRPC: alloc_chrdev_region failed: %d\n", ret);
        return ret;
    }

    omaprpc_class = class_create(THIS_MODULE, KBUILD_MODNAME);
    if (IS_ERR(omaprpc_class)) {
        ret = PTR_ERR(omaprpc_class);
        pr_err("OMAPRPC: class_create failed: %d\n", ret);
        goto unreg_region;
    }

#if defined(OMAPRPC_USE_HASH)
    /* Initialize the memory hash */
    mutex_init(&aht_lock);
    omaprpc_aht.lines = kmalloc((1<<omaprpc_aht.pow)*sizeof(struct list_head), GFP_KERNEL);
    if (omaprpc_aht.lines == NULL)
    {
        pr_err("OMAPRPC: Failed to allocate Address Hash Table!\n");
    }
    else
    {
        uint32_t i;
        for (i = 0; i < (1<<omaprpc_aht.pow); i++)
        {
            INIT_LIST_HEAD(&omaprpc_aht.lines[i]);
        }
    }
#endif

    ret = register_rpmsg_driver(&omaprpc_driver);
    pr_err("OMAPRPC: Registration of RPC RPMSG Service returned %d!\n", ret);
    return ret;
unreg_region:
    unregister_chrdev_region(omaprpc_dev, OMAPRPC_CORE_REMOTE_MAX);
    return ret;
}
module_init(omaprpc_init);

static void __exit omaprpc_fini(void)
{
    struct omaprpc_service_t *rpcserv, *tmp;
    int major = MAJOR(omaprpc_dev);

    unregister_rpmsg_driver(&omaprpc_driver);
    list_for_each_entry_safe(rpcserv, tmp, &omaprpc_services_list, list) {
        device_destroy(omaprpc_class, MKDEV(major, rpcserv->minor));
        cdev_del(&rpcserv->cdev);
        list_del(&rpcserv->list);
        kfree(rpcserv);
    }
#if defined(OMAPRPC_USE_HASH)
    /* destroy the hash memory */
    kfree(omaprpc_aht.lines);
    omaprpc_aht.lines = NULL;
    mutex_destroy(&aht_lock);
#endif
    class_destroy(omaprpc_class);
    unregister_chrdev_region(omaprpc_dev, OMAPRPC_CORE_REMOTE_MAX);
}
module_exit(omaprpc_fini);

MODULE_AUTHOR("Erik Rainey <erik.rainey@ti.com>");
MODULE_DESCRIPTION("OMAP Remote Procedure Call Driver");
//MODULE_ALIAS("platform:" MODULE_NAME);
MODULE_LICENSE("GPL v2");


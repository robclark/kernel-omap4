/*
 * OMAP Remote Procedure Call Driver.
 *
 * Copyright(c) 2011 Texas Instruments. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * * Redistributions of source code must retain the above copyright
 *   notice, this list of conditions and the following disclaimer.
 * * Redistributions in binary form must reproduce the above copyright
 *   notice, this list of conditions and the following disclaimer in
 *   the documentation and/or other materials provided with the
 *   distribution.
 * * Neither the name Texas Instruments nor the names of its
 *   contributors may be used to endorse or promote products derived
 *   from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef RPMSG_RPC_H
#define RPMSG_RPC_H

#include <linux/ioctl.h>

#if defined(CONFIG_ION_OMAP)
#include <linux/ion.h>
#endif

#define OMAPRPC_IOC_MAGIC	        'O'

#define OMAPRPC_IOC_CREATE         _IOW(OMAPRPC_IOC_MAGIC, 1, char *)
#define OMAPRPC_IOC_DESTROY         _IO(OMAPRPC_IOC_MAGIC, 2)
#define OMAPRPC_IOC_IONREGISTER   _IOWR(OMAPRPC_IOC_MAGIC, 3, struct ion_fd_data)
#define OMAPRPC_IOC_IONUNREGISTER _IOWR(OMAPRPC_IOC_MAGIC, 4, struct ion_fd_data)

#define OMAPRPC_IOC_MAXNR          (4)

struct omaprpc_create_instance_t {
    char name[48];
};

enum omaprpc_info_type_e {
    OMAPRPC_INFO_NUMFUNCS,      /**< The number of functions in the service instance */
    OMAPRPC_INFO_FUNC_NAME,     /**< The symbol name of the function */
    OMAPRPC_INFO_NUM_CALLS,     /**< The number of times a function has been called */
    OMAPRPC_INFO_FUNC_PERF,     /**< The performance information releated to the function */

    /** @hidden used to define the maximum info type */
    OMAPRPC_INFO_MAX
};

struct omaprpc_query_instance_t {
    uint32_t info_type;         /**< @see omaprpc_info_type_e */
    uint32_t func_index;
};

struct omaprpc_func_perf_t {
    uint32_t clocks_per_sec;
    uint32_t clock_cycles;
};

/** These core are specific to OMAP processors */
typedef enum omaprpc_core_e {
    OMAPRPC_CORE_DSP = 0,       /**< DSP Co-processor */
    OMAPRPC_CORE_VICP,          /**< Video/Imaging Co-processor */
    OMAPRPC_CORE_MCU0,          /**< Cortex M3/M4 [0] */
    OMAPRPC_CORE_MCU1,          /**< Cortex M3/M4 [1] */
    OMAPRPC_CORE_EVE,           /**< Imaging Accelerator */

    OMAPRPC_CORE_REMOTE_MAX
} OMAPRPC_Core_e;

struct omaprpc_instance_info_t {
    uint32_t info_type;
    uint32_t func_index;
    union info {
        uint32_t num_funcs;
        uint32_t num_calls;
        uint32_t core_index;                /**< @see omaprpc_core_e */
        char func_name[64];
        struct omaprpc_func_perf_t perf;
    } info;
};

typedef enum omaprpc_cache_ops_e {
    OMAPRPC_CACHEOP_NONE = 0,
    OMAPRPC_CACHEOP_FLUSH,
    OMAPRPC_CACHEOP_INVALIDATE,

    OMAPRPC_CACHEOP_MAX,
} OMAPRPC_CacheOps_e;

struct omaprpc_param_translation_t {
    uint32_t  index;                /**< The parameter index which indicates which is the base pointer */
    ptrdiff_t offset;               /**< The offset from the base address to the pointer to translate */
    void *    base;                 /**< The base user virtual address of the pointer to translate (used to calc translated pointer offset). */
    void *    reserved;             /**< Handle to shared memory. */
    uint32_t  cacheOps;             /**< The enumeration of desired cache operations for efficiency */
};

enum omaprpc_param_e {
    OMAPRPC_PARAM_TYPE_UNKNOWN = 0,
    OMAPRPC_PARAM_TYPE_ATOMIC,      /**< An atomic data type, 1 byte to architecture limit sized bytes */
    OMAPRPC_PARAM_TYPE_PTR,         /**< A pointer to shared memory. The reserved field must contain the handle to the memory */
    OMAPRPC_PARAM_TYPE_STRUCT,      /**< A structure type. Will be architecure width aligned in memory. */
};

struct omaprpc_param_t {
    uint32_t type;                  /**< @see omaprpc_param_e */
    size_t   size;                  /**< The size of the data */
    void *   data;                  /**< Either the pointer to the data or the data itself, @see .type */
    void *   base;                  /**< If a pointer is in data, this is the base pointer (if data has an offset from base). */
    void *   reserved;              /**< Shared Memory Handle (used only with pointers) */
};

#define OMAPRPC_MAX_PARAMETERS (10)

struct omaprpc_call_function_t {
    uint32_t func_index;                    /**< The function to call */
    uint32_t num_params;                    /**< The number of parameters in the array. */
    struct omaprpc_param_t params[OMAPRPC_MAX_PARAMETERS];  /**< The array of parameters */
    uint32_t num_translations;              /**< The number of translations needed in the offsets array */
    struct omaprpc_param_translation_t translations[0]; /**< An indeterminate lenght array of offsets within payload_data to pointers which need translation */
};

#define OMAPRPC_MAX_TRANSLATIONS    (1024)

struct omaprpc_function_return_t {
    uint32_t func_index;
    uint32_t status;
};

#ifdef __KERNEL__

/** The applicable types of messages that the HOST may send the SERVICE.
 * (@see omx_msg_types must duplicate these for now since they went and shoved
 * it so far down RCM that it's impossible to use it without this)
 */
enum omaprpc_msg_type_e {

    OMAPRPC_MSG_CREATE_INSTANCE = 0,    /**< Ask the ServiceMgr to create a new instance of the service. No secondary data is needed. */
    OMAPRPC_MSG_INSTANCE_CREATED = 1,   /**< The return message from OMAPRPC_CREATE_INSTANCE, contains the new endpoint address in the omaprpc_instance_handle_t */
    OMAPRPC_MSG_QUERY_INSTANCE = 2,     /**< Ask the Service Instance to send information about the Service */
    OMAPRPC_MSG_INSTANCE_INFO = 3,      /**< The return message from OMAPRPC_QUERY_INSTANCE, which contains the information about the instance */
    OMAPRPC_MSG_DESTROY_INSTANCE = 4,   /**< Ask the Service Mgr to destroy an instance */
    OMAPRPC_MSG_CALL_FUNCTION = 5,      /**< Ask the Service Instance to call a particular function */
    OMAPRPC_MSG_INSTANCE_DESTROYED = 6, /**< The return message from OMAPRPC_DESTROY_INSTANCE. contains the old endpoint address in the omaprpc_instance_handle_t */
    OMAPRPC_MSG_ERROR = 7,              /**< Returned from either the ServiceMgr or Service Instance when an error occurs */
    OMAPRPC_MSG_FUNCTION_RETURN = 8,    /**< The return values from a function call */

    /** @hidden used to define the max msg enum, not an actual message */
    OMAPRPC_MSG_MAX
};

enum omaprpc_state {
    OMAPRPC_STATE_DISCONNECTED,   /** No errors, just not initialized */
    OMAPRPC_STATE_CONNECTED,      /** No errors, initialized remote DVP KGM */
    OMAPRPC_STATE_FAULT,          /** Some error has been detected. Disconnected. */

    OMAPRPC_STATE_MAX
};

/** The generic OMAPRPC message header (actually a copy of omx_msg_hdr which is a copy of an RCM header) */
struct omaprpc_msg_header_t {
    uint32_t msg_type;      /**< @see omaprpc_msg_type_e */
    uint32_t msg_flags;     /**< Unused */
    uint32_t msg_len;       /**< The length of the message data in bytes */
    uint8_t  msg_data[0];
} __packed;

struct omaprpc_instance_handle_t {
    uint32_t endpoint_address;
    uint32_t status;
} __packed;

struct omaprpc_error_t {
    uint32_t endpoint_address;
    uint32_t status;
} __packed;

struct omaprpc_parameter_t {
     size_t size;
     void * data;
 }__packed;

#define OMAPRPC_NUM_PARAMETERS(size)      (size/sizeof(struct omaprpc_parameter_t))

#define OMAPRPC_PAYLOAD(ptr, type)    (struct type *)&ptr[sizeof(struct omaprpc_msg_header_t)]

enum _omaprpc_translation_direction_e {
    OMAPRPC_UVA_TO_RPA,
    OMAPRPC_RPA_TO_UVA,
};

#endif /* __KERNEL__ */

#define OMAPRPC_DESC_EXEC_SYNC  (0x0100)
#define OMAPRPC_DESC_EXEC_ASYNC (0x0200)
#define OMAPRPC_DESC_SYM_ADD    (0x0300)
#define OMAPRPC_DESC_SYM_IDX    (0x0400)
#define OMAPRPC_DESC_CMD        (0x0500)
#define OMAPRPC_DESC_TYPE_MASK  (0x0F00)
#define OMAPRPC_JOBID_DISCRETE  (0)
#define OMAPRPC_POOLID_DEFAULT  (0x8000)

#define OMAPRPC_SET_FXN_IDX(idx) (idx | 0x80000000)
#define OMAPRPC_FXN_MASK(idx)    (idx & 0x7FFFFFFF)

/** This is actually a frankensteined structure of RCM */
struct omaprpc_packet_t {
    uint16_t desc;          /**< @see RcmClient_Packet.desc */
    uint16_t msg_id;        /**< @see RcmClient_Packet.msgId */
    uint16_t pool_id;       /**< @see RcmClient_Message.poolId */
    uint16_t job_id;        /**< @see RcmClient_Message.jobId */
    uint32_t fxn_idx;       /**< @see RcmClient_Message.fxnIdx */
     int32_t result;        /**< @see RcmClient_Message.result */
    uint32_t data_size;     /**< @see RcmClient_Message.data_size */
    uint8_t  data[0];       /**< @see RcmClient_Message.data pointer */
} __packed;

#endif /* RPMSG_RPC_H */

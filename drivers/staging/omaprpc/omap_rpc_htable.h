
#ifndef _OMAP_RPC_HTABLE_H_
#define _OMAP_RPC_HTABLE_H_

#include "omap_rpc_internal.h"

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

#endif


#include "omap_rpc_internal.h"

#if defined(OMAPRPC_USE_RPROC_LOOKUP)

phys_addr_t rpmsg_local_to_remote_pa(struct omaprpc_instance_t *rpc, phys_addr_t pa)
{
    int ret;
    struct rproc *rproc;
    u64 da;
    phys_addr_t rpa;

    if (mutex_lock_interruptible(&rpc->rpcserv->lock))
        return 0;

    rproc = rpmsg_get_rproc_handle(rpc->rpcserv->rpdev);
    ret = rproc_pa_to_da(rproc, pa, &da);
    if (ret) {
        pr_err("error from rproc_pa_to_da %d\n", ret);
        da = 0;
    }

    /*Revisit if remote address size increases */
    rpa = (phys_addr_t)da;

    mutex_unlock(&rpc->rpcserv->lock);
    return rpa;

}

#else

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

phys_addr_t rpmsg_local_to_remote_pa(uint32_t core, phys_addr_t pa)
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

#endif



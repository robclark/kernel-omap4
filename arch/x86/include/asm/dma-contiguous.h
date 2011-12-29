#ifndef ASMX86_DMA_CONTIGUOUS_H
#define ASMX86_DMA_CONTIGUOUS_H

#ifdef __KERNEL__

#include <linux/device.h>
#include <linux/dma-contiguous.h>
#include <asm-generic/dma-contiguous.h>

static inline void dma_contiguous_early_fixup(phys_addr_t base, unsigned long size) { }

#endif
#endif

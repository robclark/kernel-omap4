#ifndef __TILER_PACK_H__
#define __TILER_PACK_H__

u32 tiler_best2pack(u16 o, u16 w, u16 e, u16 b, u16 *n, u16 *area);

u16 nv12_together(u16 o, u16 w, u16 a, u16 n, u16 *area, u8 *packing);

u16 nv12_separate(u16 o, u16 w, u16 a, int n, u16 *area);


#endif

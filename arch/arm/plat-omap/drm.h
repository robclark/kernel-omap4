/*
 * File: arch/arm/plat-omap/drm.c
 *
 * DRM/KMS device registration for TI OMAP platforms
 *
 * Copyright (C) 2011 Texas Instruments
 * Author: Rob Clark <rob.clark@linaro.org>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published by
 * the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef __PLAT_OMAP_DRM_H__
#define __PLAT_OMAP_DRM_H__

#if 0
#include <drm/omap_drm.h>
#include <drm/omap_priv.h>
#endif

#if defined(CONFIG_DRM_OMAP) || defined(CONFIG_DRM_OMAP_MODULE)

#if 0
void omapdrm_set_platform_data(struct omap_drm_platform_data *data);
#endif
void omapdrm_reserve_vram(void);

#else

#if 0
static inline void omapdrm_set_platform_data(struct omap_drm_platform_data *data)
{
}
#endif

static inline void omapdrm_reserve_vram(void)
{
}

#endif

#endif /* __PLAT_OMAP_DRM_H__ */

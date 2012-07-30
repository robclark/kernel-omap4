/*
 * drivers/staging/omapdrm/omap_irq.c
 *
 * Copyright (C) 2012 Texas Instruments
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

#include "omap_drv.h"

/* TODO move to priv.. */
static u32 error_mask;
static u32 current_mask;

/**
 * enable_vblank - enable vblank interrupt events
 * @dev: DRM device
 * @crtc: which irq to enable
 *
 * Enable vblank interrupts for @crtc.  If the device doesn't have
 * a hardware vblank counter, this routine should be a no-op, since
 * interrupts will have to stay on to keep the count accurate.
 *
 * RETURNS
 * Zero on success, appropriate errno if the given @crtc's vblank
 * interrupt cannot be enabled.
 */
int omap_irq_enable_vblank(struct drm_device *dev, int crtc)
{
	DBG("dev=%p, crtc=%d", dev, crtc);
	return 0;
}

/**
 * disable_vblank - disable vblank interrupt events
 * @dev: DRM device
 * @crtc: which irq to enable
 *
 * Disable vblank interrupts for @crtc.  If the device doesn't have
 * a hardware vblank counter, this routine should be a no-op, since
 * interrupts will have to stay on to keep the count accurate.
 */
void omap_irq_disable_vblank(struct drm_device *dev, int crtc)
{
	DBG("dev=%p, crtc=%d", dev, crtc);
}

/* call w/ pm runtime held */
void omap_irq_enable(uint32_t mask)
{
	current_mask |= mask;
	dispc_set_irqs(current_mask);
}

/* call w/ pm runtime held */
void omap_irq_disable(uint32_t mask)
{
	current_mask &= ~mask;
	dispc_set_irqs(current_mask);
}

irqreturn_t omap_irq_handler(DRM_IRQ_ARGS)
{
	struct drm_device *dev = (struct drm_device *) arg;
	struct omap_drm_private *priv = dev->dev_private;
	u32 irqstatus;

	irqstatus = dispc_read_irqs();
	dispc_clear_irqs(irqstatus);
	dispc_read_irqs();            /* flush posted write */

	if (irqstatus & error_mask) {
		DBG("errors: %08x", irqstatus & error_mask);
		/* TODO */
	}

	DBG("irqs: %08x", irqstatus & ~error_mask);

#define VBLANK (DISPC_IRQ_VSYNC|DISPC_IRQ_VSYNC2|DISPC_IRQ_EVSYNC_ODD|DISPC_IRQ_EVSYNC_EVEN)
	if (irqstatus & VBLANK) {
		unsigned int i;
		for (i = 0; i < priv->num_encoders; i++)
			omap_encoder_vblank(priv->encoders[i], irqstatus);

		/* TODO we could probably dispatch to CRTC's and handle
		 * page-flip events w/out the callback between omap_plane
		 * and omap_crtc.. that would be a bit cleaner.
		 */
	}

	return IRQ_HANDLED;
}

void omap_irq_preinstall(struct drm_device *dev)
{
	DBG("dev=%p", dev);

	dispc_runtime_get();
	error_mask = dispc_error_irqs();

	/* for now ignore DISPC_IRQ_SYNC_LOST_DIGIT.. really I think
	 * we just need to ignore it while enabling tv-out
	 */
	error_mask &= ~DISPC_IRQ_SYNC_LOST_DIGIT;

	dispc_clear_irqs(0xffffffff);
	dispc_runtime_put();
}

int omap_irq_postinstall(struct drm_device *dev)
{
	DBG("dev=%p", dev);
	dispc_runtime_get();
	omap_irq_enable(error_mask);
	dispc_runtime_put();
	return 0;
}

void omap_irq_uninstall(struct drm_device *dev)
{
	DBG("dev=%p", dev);
}

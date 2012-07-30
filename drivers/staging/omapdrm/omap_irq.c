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

static DEFINE_SPINLOCK(list_lock);

static void omap_irq_error_handler(struct omap_drm_irq *irq,
		uint32_t irqstatus)
{
	DRM_ERROR("errors: %08x\n", irqstatus);
}

/* call with list_lock and dispc runtime held */
static void omap_irq_update(struct drm_device *dev)
{
	struct omap_drm_private *priv = dev->dev_private;
	struct omap_drm_irq *irq;
	uint32_t irqmask = priv->vblank_mask;

	BUG_ON(!spin_is_locked(&list_lock));

	list_for_each_entry(irq, &priv->irq_list, node)
		irqmask |= irq->irqmask;

	DBG("irqmask=%08x", irqmask);

	dispc_set_irqs(irqmask);
	dispc_read_irqs();            /* flush posted write */
}

void omap_irq_register(struct drm_device *dev, struct omap_drm_irq *irq)
{
	struct omap_drm_private *priv = dev->dev_private;
	unsigned long flags;

	dispc_runtime_get();
	spin_lock_irqsave(&list_lock, flags);

	if (!WARN_ON(irq->registered)) {
		irq->registered = true;
		list_add(&irq->node, &priv->irq_list);
		omap_irq_update(dev);
	}

	spin_unlock_irqrestore(&list_lock, flags);
	dispc_runtime_put();
}

void omap_irq_unregister(struct drm_device *dev, struct omap_drm_irq *irq)
{
	unsigned long flags;

	dispc_runtime_get();
	spin_lock_irqsave(&list_lock, flags);

	if (!WARN_ON(!irq->registered)) {
		irq->registered = false;
		list_del(&irq->node);
		omap_irq_update(dev);
	}

	spin_unlock_irqrestore(&list_lock, flags);
	dispc_runtime_put();
}

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
	struct omap_drm_private *priv = dev->dev_private;
	unsigned long flags;

	DBG("dev=%p, crtc=%d", dev, crtc);

	dispc_runtime_get();
	spin_lock_irqsave(&list_lock, flags);
	priv->vblank_mask |= pipe2vbl(crtc);
	omap_irq_update(dev);
	spin_unlock_irqrestore(&list_lock, flags);
	dispc_runtime_put();

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
	struct omap_drm_private *priv = dev->dev_private;
	unsigned long flags;

	DBG("dev=%p, crtc=%d", dev, crtc);

	dispc_runtime_get();
	spin_lock_irqsave(&list_lock, flags);
	priv->vblank_mask &= ~pipe2vbl(crtc);
	omap_irq_update(dev);
	spin_unlock_irqrestore(&list_lock, flags);
	dispc_runtime_put();
}

irqreturn_t omap_irq_handler(DRM_IRQ_ARGS)
{
	struct drm_device *dev = (struct drm_device *) arg;
	struct omap_drm_private *priv = dev->dev_private;
	struct omap_drm_irq *handler, *n;
	unsigned long flags;
	unsigned int id;
	u32 irqstatus;

	irqstatus = dispc_read_irqs();
	dispc_clear_irqs(irqstatus);
	dispc_read_irqs();            /* flush posted write */

	VERB("irqs: %08x", irqstatus);

	spin_lock_irqsave(&list_lock, flags);
	list_for_each_entry_safe(handler, n, &priv->irq_list, node) {
		if (handler->irqmask & irqstatus) {
			spin_unlock_irqrestore(&list_lock, flags);
			handler->irq(handler, handler->irqmask & irqstatus);
			spin_lock_irqsave(&list_lock, flags);
		}
	}
	spin_unlock_irqrestore(&list_lock, flags);

	for (id = 0; id < priv->num_crtcs; id++)
		if (irqstatus & pipe2vbl(id))
			drm_handle_vblank(dev, id);

	return IRQ_HANDLED;
}

void omap_irq_preinstall(struct drm_device *dev)
{
	DBG("dev=%p", dev);
	dispc_runtime_get();
	dispc_clear_irqs(0xffffffff);
	dispc_runtime_put();
}

int omap_irq_postinstall(struct drm_device *dev)
{
	struct omap_drm_private *priv = dev->dev_private;
	struct omap_drm_irq *error_handler = &priv->error_handler;

	DBG("dev=%p", dev);

	INIT_LIST_HEAD(&priv->irq_list);

	error_handler->irq = omap_irq_error_handler;
	error_handler->irqmask = dispc_error_irqs();

	/* for now ignore DISPC_IRQ_SYNC_LOST_DIGIT.. really I think
	 * we just need to ignore it while enabling tv-out
	 */
	error_handler->irqmask &= ~DISPC_IRQ_SYNC_LOST_DIGIT;

	omap_irq_register(dev, error_handler);

	return 0;
}

void omap_irq_uninstall(struct drm_device *dev)
{
	DBG("dev=%p", dev);
	// TODO prolly need to call drm_irq_uninstall() somewhere too
}

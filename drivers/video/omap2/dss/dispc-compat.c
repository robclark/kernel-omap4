/*
 * Copyright (C) 2012 Texas Instruments
 * Author: Tomi Valkeinen <tomi.valkeinen@ti.com>
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

#define DSS_SUBSYS_NAME "APPLY"

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/jiffies.h>
#include <linux/delay.h>
#include <linux/interrupt.h>

#include <video/omapdss.h>

#include "dss.h"
#include "dss_features.h"
#include "dispc-compat.h"

#define DISPC_IRQ_MASK_ERROR            (DISPC_IRQ_GFX_FIFO_UNDERFLOW | \
					 DISPC_IRQ_OCP_ERR | \
					 DISPC_IRQ_VID1_FIFO_UNDERFLOW | \
					 DISPC_IRQ_VID2_FIFO_UNDERFLOW | \
					 DISPC_IRQ_SYNC_LOST | \
					 DISPC_IRQ_SYNC_LOST_DIGIT)

#define DISPC_MAX_NR_ISRS		8

struct omap_dispc_isr_data {
	omap_dispc_isr_t	isr;
	void			*arg;
	u32			mask;
};

struct dispc_irq_stats {
	unsigned long last_reset;
	unsigned irq_count;
	unsigned irqs[32];
};

static struct {
	spinlock_t irq_lock;
	u32 irq_error_mask;
	struct omap_dispc_isr_data registered_isr[DISPC_MAX_NR_ISRS];
	u32 error_irqs;
	struct work_struct error_work;

#ifdef CONFIG_OMAP2_DSS_COLLECT_IRQ_STATS
	spinlock_t irq_stats_lock;
	struct dispc_irq_stats irq_stats;
#endif
} dispc_compat;


#ifdef CONFIG_OMAP2_DSS_COLLECT_IRQ_STATS
void dispc_dump_irqs(struct seq_file *s)
{
	unsigned long flags;
	struct dispc_irq_stats stats;

	spin_lock_irqsave(&dispc_compat.irq_stats_lock, flags);

	stats = dispc_compat.irq_stats;
	memset(&dispc_compat.irq_stats, 0, sizeof(dispc_compat.irq_stats));
	dispc_compat.irq_stats.last_reset = jiffies;

	spin_unlock_irqrestore(&dispc_compat.irq_stats_lock, flags);

	seq_printf(s, "period %u ms\n",
			jiffies_to_msecs(jiffies - stats.last_reset));

	seq_printf(s, "irqs %d\n", stats.irq_count);
#define PIS(x) \
	seq_printf(s, "%-20s %10d\n", #x, stats.irqs[ffs(DISPC_IRQ_##x)-1]);

	PIS(FRAMEDONE);
	PIS(VSYNC);
	PIS(EVSYNC_EVEN);
	PIS(EVSYNC_ODD);
	PIS(ACBIAS_COUNT_STAT);
	PIS(PROG_LINE_NUM);
	PIS(GFX_FIFO_UNDERFLOW);
	PIS(GFX_END_WIN);
	PIS(PAL_GAMMA_MASK);
	PIS(OCP_ERR);
	PIS(VID1_FIFO_UNDERFLOW);
	PIS(VID1_END_WIN);
	PIS(VID2_FIFO_UNDERFLOW);
	PIS(VID2_END_WIN);
	if (dss_feat_get_num_ovls() > 3) {
		PIS(VID3_FIFO_UNDERFLOW);
		PIS(VID3_END_WIN);
	}
	PIS(SYNC_LOST);
	PIS(SYNC_LOST_DIGIT);
	PIS(WAKEUP);
	if (dss_has_feature(FEAT_MGR_LCD2)) {
		PIS(FRAMEDONE2);
		PIS(VSYNC2);
		PIS(ACBIAS_COUNT_STAT2);
		PIS(SYNC_LOST2);
	}
	if (dss_has_feature(FEAT_MGR_LCD3)) {
		PIS(FRAMEDONE3);
		PIS(VSYNC3);
		PIS(ACBIAS_COUNT_STAT3);
		PIS(SYNC_LOST3);
	}
#undef PIS
}
#endif

/* dispc_compat.irq_lock has to be locked by the caller */
static void _omap_dispc_set_irqs(void)
{
	u32 mask;
	u32 old_mask;
	int i;
	struct omap_dispc_isr_data *isr_data;

	mask = dispc_compat.irq_error_mask;

	for (i = 0; i < DISPC_MAX_NR_ISRS; i++) {
		isr_data = &dispc_compat.registered_isr[i];

		if (isr_data->isr == NULL)
			continue;

		mask |= isr_data->mask;
	}

	old_mask = dispc_read_irqenable();
	/* clear the irqstatus for newly enabled irqs */
	dispc_clear_irqstatus((mask ^ old_mask) & mask);

	dispc_write_irqenable(mask);
}

int omap_dispc_register_isr(omap_dispc_isr_t isr, void *arg, u32 mask)
{
	int i;
	int ret;
	unsigned long flags;
	struct omap_dispc_isr_data *isr_data;

	if (isr == NULL)
		return -EINVAL;

	spin_lock_irqsave(&dispc_compat.irq_lock, flags);

	/* check for duplicate entry */
	for (i = 0; i < DISPC_MAX_NR_ISRS; i++) {
		isr_data = &dispc_compat.registered_isr[i];
		if (isr_data->isr == isr && isr_data->arg == arg &&
				isr_data->mask == mask) {
			ret = -EINVAL;
			goto err;
		}
	}

	isr_data = NULL;
	ret = -EBUSY;

	for (i = 0; i < DISPC_MAX_NR_ISRS; i++) {
		isr_data = &dispc_compat.registered_isr[i];

		if (isr_data->isr != NULL)
			continue;

		isr_data->isr = isr;
		isr_data->arg = arg;
		isr_data->mask = mask;
		ret = 0;

		break;
	}

	if (ret)
		goto err;

	_omap_dispc_set_irqs();

	spin_unlock_irqrestore(&dispc_compat.irq_lock, flags);

	return 0;
err:
	spin_unlock_irqrestore(&dispc_compat.irq_lock, flags);

	return ret;
}
EXPORT_SYMBOL(omap_dispc_register_isr);

int omap_dispc_unregister_isr(omap_dispc_isr_t isr, void *arg, u32 mask)
{
	int i;
	unsigned long flags;
	int ret = -EINVAL;
	struct omap_dispc_isr_data *isr_data;

	spin_lock_irqsave(&dispc_compat.irq_lock, flags);

	for (i = 0; i < DISPC_MAX_NR_ISRS; i++) {
		isr_data = &dispc_compat.registered_isr[i];
		if (isr_data->isr != isr || isr_data->arg != arg ||
				isr_data->mask != mask)
			continue;

		/* found the correct isr */

		isr_data->isr = NULL;
		isr_data->arg = NULL;
		isr_data->mask = 0;

		ret = 0;
		break;
	}

	if (ret == 0)
		_omap_dispc_set_irqs();

	spin_unlock_irqrestore(&dispc_compat.irq_lock, flags);

	return ret;
}
EXPORT_SYMBOL(omap_dispc_unregister_isr);

#ifdef DEBUG
static void print_irq_status(u32 status)
{
	if ((status & dispc_compat.irq_error_mask) == 0)
		return;

	printk(KERN_DEBUG "DISPC IRQ: 0x%x: ", status);

#define PIS(x) \
	if (status & DISPC_IRQ_##x) \
		printk(#x " ");
	PIS(GFX_FIFO_UNDERFLOW);
	PIS(OCP_ERR);
	PIS(VID1_FIFO_UNDERFLOW);
	PIS(VID2_FIFO_UNDERFLOW);
	if (dss_feat_get_num_ovls() > 3)
		PIS(VID3_FIFO_UNDERFLOW);
	PIS(SYNC_LOST);
	PIS(SYNC_LOST_DIGIT);
	if (dss_has_feature(FEAT_MGR_LCD2))
		PIS(SYNC_LOST2);
	if (dss_has_feature(FEAT_MGR_LCD3))
		PIS(SYNC_LOST3);
#undef PIS

	printk("\n");
}
#endif

/* Called from dss.c. Note that we don't touch clocks here,
 * but we presume they are on because we got an IRQ. However,
 * an irq handler may turn the clocks off, so we may not have
 * clock later in the function. */
static irqreturn_t omap_dispc_irq_handler(int irq, void *arg)
{
	int i;
	u32 irqstatus, irqenable;
	u32 handledirqs = 0;
	u32 unhandled_errors;
	struct omap_dispc_isr_data *isr_data;
	struct omap_dispc_isr_data registered_isr[DISPC_MAX_NR_ISRS];

	spin_lock(&dispc_compat.irq_lock);

	irqstatus = dispc_read_irqstatus();
	irqenable = dispc_read_irqenable();

	/* IRQ is not for us */
	if (!(irqstatus & irqenable)) {
		spin_unlock(&dispc_compat.irq_lock);
		return IRQ_NONE;
	}

#ifdef CONFIG_OMAP2_DSS_COLLECT_IRQ_STATS
	spin_lock(&dispc_compat.irq_stats_lock);
	dispc_compat.irq_stats.irq_count++;
	dss_collect_irq_stats(irqstatus, dispc_compat.irq_stats.irqs);
	spin_unlock(&dispc_compat.irq_stats_lock);
#endif

#ifdef DEBUG
	if (dss_debug)
		print_irq_status(irqstatus);
#endif
	/* Ack the interrupt. Do it here before clocks are possibly turned
	 * off */
	dispc_clear_irqstatus(irqstatus);
	/* flush posted write */
	dispc_read_irqstatus();

	/* make a copy and unlock, so that isrs can unregister
	 * themselves */
	memcpy(registered_isr, dispc_compat.registered_isr,
			sizeof(registered_isr));

	spin_unlock(&dispc_compat.irq_lock);

	for (i = 0; i < DISPC_MAX_NR_ISRS; i++) {
		isr_data = &registered_isr[i];

		if (!isr_data->isr)
			continue;

		if (isr_data->mask & irqstatus) {
			isr_data->isr(isr_data->arg, irqstatus);
			handledirqs |= isr_data->mask;
		}
	}

	spin_lock(&dispc_compat.irq_lock);

	unhandled_errors = irqstatus & ~handledirqs & dispc_compat.irq_error_mask;

	if (unhandled_errors) {
		dispc_compat.error_irqs |= unhandled_errors;

		dispc_compat.irq_error_mask &= ~unhandled_errors;
		_omap_dispc_set_irqs();

		schedule_work(&dispc_compat.error_work);
	}

	spin_unlock(&dispc_compat.irq_lock);

	return IRQ_HANDLED;
}

static void dispc_error_worker(struct work_struct *work)
{
	int i;
	u32 errors;
	unsigned long flags;
	static const unsigned fifo_underflow_bits[] = {
		DISPC_IRQ_GFX_FIFO_UNDERFLOW,
		DISPC_IRQ_VID1_FIFO_UNDERFLOW,
		DISPC_IRQ_VID2_FIFO_UNDERFLOW,
		DISPC_IRQ_VID3_FIFO_UNDERFLOW,
	};

	spin_lock_irqsave(&dispc_compat.irq_lock, flags);
	errors = dispc_compat.error_irqs;
	dispc_compat.error_irqs = 0;
	spin_unlock_irqrestore(&dispc_compat.irq_lock, flags);

	dispc_runtime_get();

	for (i = 0; i < omap_dss_get_num_overlays(); ++i) {
		struct omap_overlay *ovl;
		unsigned bit;

		ovl = omap_dss_get_overlay(i);
		bit = fifo_underflow_bits[i];

		if (bit & errors) {
			DSSERR("FIFO UNDERFLOW on %s, disabling the overlay\n",
					ovl->name);
			dispc_ovl_enable(ovl->id, false);
			dispc_mgr_go(ovl->manager->id);
			msleep(50);
		}
	}

	for (i = 0; i < omap_dss_get_num_overlay_managers(); ++i) {
		struct omap_overlay_manager *mgr;
		unsigned bit;

		mgr = omap_dss_get_overlay_manager(i);
		bit = dispc_mgr_get_sync_lost_irq(i);

		if (bit & errors) {
			struct omap_dss_device *dssdev = mgr->get_device(mgr);
			bool enable;

			DSSERR("SYNC_LOST on channel %s, restarting the output "
					"with video overlays disabled\n",
					mgr->name);

			enable = dssdev->state == OMAP_DSS_DISPLAY_ACTIVE;
			dssdev->driver->disable(dssdev);

			for (i = 0; i < omap_dss_get_num_overlays(); ++i) {
				struct omap_overlay *ovl;
				ovl = omap_dss_get_overlay(i);

				if (ovl->id != OMAP_DSS_GFX &&
						ovl->manager == mgr)
					dispc_ovl_enable(ovl->id, false);
			}

			dispc_mgr_go(mgr->id);
			msleep(50);

			if (enable)
				dssdev->driver->enable(dssdev);
		}
	}

	if (errors & DISPC_IRQ_OCP_ERR) {
		DSSERR("OCP_ERR\n");
		for (i = 0; i < omap_dss_get_num_overlay_managers(); ++i) {
			struct omap_overlay_manager *mgr;
			struct omap_dss_device *dssdev;

			mgr = omap_dss_get_overlay_manager(i);
			dssdev = mgr->get_device(mgr);

			if (dssdev && dssdev->driver)
				dssdev->driver->disable(dssdev);
		}
	}

	spin_lock_irqsave(&dispc_compat.irq_lock, flags);
	dispc_compat.irq_error_mask |= errors;
	_omap_dispc_set_irqs();
	spin_unlock_irqrestore(&dispc_compat.irq_lock, flags);

	dispc_runtime_put();
}

int dss_dispc_initialize_irq(void)
{
	int r;

#ifdef CONFIG_OMAP2_DSS_COLLECT_IRQ_STATS
	spin_lock_init(&dispc_compat.irq_stats_lock);
	dispc_compat.irq_stats.last_reset = jiffies;
	dss_debugfs_create_file("dispc_irq", dispc_dump_irqs);
#endif

	spin_lock_init(&dispc_compat.irq_lock);

	memset(dispc_compat.registered_isr, 0,
			sizeof(dispc_compat.registered_isr));

	dispc_compat.irq_error_mask = DISPC_IRQ_MASK_ERROR;
	if (dss_has_feature(FEAT_MGR_LCD2))
		dispc_compat.irq_error_mask |= DISPC_IRQ_SYNC_LOST2;
	if (dss_has_feature(FEAT_MGR_LCD3))
		dispc_compat.irq_error_mask |= DISPC_IRQ_SYNC_LOST3;
	if (dss_feat_get_num_ovls() > 3)
		dispc_compat.irq_error_mask |= DISPC_IRQ_VID3_FIFO_UNDERFLOW;

	/*
	 * there's SYNC_LOST_DIGIT waiting after enabling the DSS,
	 * so clear it
	 */
	dispc_clear_irqstatus(dispc_read_irqstatus());

	INIT_WORK(&dispc_compat.error_work, dispc_error_worker);

	_omap_dispc_set_irqs();

	r = dispc_request_irq(omap_dispc_irq_handler, &dispc_compat);
	if (r) {
		printk("REQ IRQ failed\n");
		return r;
	}

	return 0;
}

void dss_dispc_uninitialize_irq(void)
{
	dispc_free_irq(&dispc_compat);
}

static void dispc_disable_output_isr(void *data, u32 mask)
{
	struct completion *compl = data;
	complete(compl);
}

void dispc_mgr_enable_lcd_out(enum omap_channel channel)
{
	dispc_mgr_enable_output(channel, true);
}

void dispc_mgr_disable_lcd_out(enum omap_channel channel)
{
	DECLARE_COMPLETION_ONSTACK(frame_done_completion);
	int r;
	u32 irq;

	if (dispc_mgr_output_enabled(channel) == false)
		return;

	/* When we disable LCD output, we need to wait until frame is done.
	 * Otherwise the DSS is still working, and turning off the clocks
	 * prevents DSS from going to OFF mode */

	irq = dispc_mgr_get_framedone_irq(channel);

	r = omap_dispc_register_isr(dispc_disable_output_isr,
			&frame_done_completion, irq);
	if (r)
		DSSERR("failed to register FRAMEDONE isr\n");

	dispc_mgr_enable_output(channel, false);

	/* if we couldn't register for framedone, just sleep and exit */
	if (r) {
		msleep(200);
		return;
	}

	if (!wait_for_completion_timeout(&frame_done_completion,
				msecs_to_jiffies(100)))
		DSSERR("timeout waiting for FRAME DONE\n");

	r = omap_dispc_unregister_isr(dispc_disable_output_isr,
			&frame_done_completion, irq);
	if (r)
		DSSERR("failed to unregister FRAMEDONE isr\n");
}

static void dispc_digit_out_enable_isr(void *data, u32 mask)
{
	struct completion *compl = data;

	if (mask & (DISPC_IRQ_EVSYNC_EVEN | DISPC_IRQ_EVSYNC_ODD))
		complete(compl);
}

void dispc_mgr_enable_digit_out(void)
{
	DECLARE_COMPLETION_ONSTACK(frame_done_completion);
	int r, i;
	u32 irq_mask;
	int num_irqs;

	if (dispc_mgr_output_enabled(OMAP_DSS_CHANNEL_DIGIT) == true)
		return;

	irq_mask = DISPC_IRQ_EVSYNC_EVEN | DISPC_IRQ_EVSYNC_ODD |
		DISPC_IRQ_SYNC_LOST_DIGIT;
	num_irqs = 2;

	r = omap_dispc_register_isr(dispc_digit_out_enable_isr,
			&frame_done_completion, irq_mask);
	if (r) {
		DSSERR("failed to register %x isr\n", irq_mask);
		return;
	}

	dispc_mgr_enable_output(OMAP_DSS_CHANNEL_DIGIT, true);

	for (i = 0; i < num_irqs; ++i) {
		if (!wait_for_completion_timeout(&frame_done_completion,
					msecs_to_jiffies(100)))
			DSSERR("timeout waiting for digit out to start\n");
	}

	r = omap_dispc_unregister_isr(dispc_digit_out_enable_isr,
			&frame_done_completion, irq_mask);
	if (r)
		DSSERR("failed to unregister %x isr\n", irq_mask);
}

void dispc_mgr_disable_digit_out(void)
{
	DECLARE_COMPLETION_ONSTACK(frame_done_completion);
	enum dss_hdmi_venc_clk_source_select src;
	int r, i;
	u32 irq_mask;
	int num_irqs;

	if (dispc_mgr_output_enabled(OMAP_DSS_CHANNEL_DIGIT) == false)
		return;

	src = dss_get_hdmi_venc_clk_source();

	/* When we disable digit output, we need to wait until fields are done.
	 * Otherwise the DSS is still working, and turning off the clocks
	 * prevents DSS from going to OFF mode. And when enabling, we need to
	 * wait for the extra sync losts */

	if (src == DSS_HDMI_M_PCLK) {
		irq_mask = DISPC_IRQ_FRAMEDONETV;
		num_irqs = 1;
	} else {
		irq_mask = DISPC_IRQ_EVSYNC_EVEN | DISPC_IRQ_EVSYNC_ODD;
		/* XXX I understand from TRM that we should only wait for the
		 * current field to complete. But it seems we have to wait for
		 * both fields */
		num_irqs = 2;
	}

	r = omap_dispc_register_isr(dispc_disable_output_isr,
			&frame_done_completion, irq_mask);
	if (r)
		DSSERR("failed to register %x isr\n", irq_mask);

	dispc_mgr_enable_output(OMAP_DSS_CHANNEL_DIGIT, false);

	for (i = 0; i < num_irqs; ++i) {
		if (!wait_for_completion_timeout(&frame_done_completion,
					msecs_to_jiffies(100)))
			DSSERR("timeout waiting for digit out to stop\n");
	}

	r = omap_dispc_unregister_isr(dispc_disable_output_isr,
			&frame_done_completion, irq_mask);
	if (r)
		DSSERR("failed to unregister %x isr\n", irq_mask);
}

void dispc_wb_enable(void)
{
	if (dispc_wb_is_enabled())
		return;

	dispc_ovl_enable(OMAP_DSS_WB, true);
}

void dispc_wb_disable(void)
{
	DECLARE_COMPLETION_ONSTACK(frame_done_completion);
	int r;
	u32 irq;

	if (dispc_wb_is_enabled() == false)
		return;

	irq = DISPC_IRQ_FRAMEDONEWB;

	r = omap_dispc_register_isr(dispc_disable_output_isr,
			&frame_done_completion, irq);
	if (r)
		DSSERR("failed to register FRAMEDONEWB isr\n");

	dispc_ovl_enable(OMAP_DSS_WB, false);

	if (!wait_for_completion_timeout(&frame_done_completion,
				msecs_to_jiffies(100)))
		DSSERR("timeout waiting for FRAMEDONEWB\n");

	r = omap_dispc_unregister_isr(dispc_disable_output_isr,
			&frame_done_completion, irq);
	if (r)
		DSSERR("failed to unregister FRAMEDONEWB isr\n");
}

int omap_dispc_wait_for_irq_interruptible_timeout(u32 irqmask,
		unsigned long timeout)
{
	void dispc_irq_wait_handler(void *data, u32 mask)
	{
		complete((struct completion *)data);
	}

	int r;
	DECLARE_COMPLETION_ONSTACK(completion);

	r = omap_dispc_register_isr(dispc_irq_wait_handler, &completion,
			irqmask);

	if (r)
		return r;

	timeout = wait_for_completion_interruptible_timeout(&completion,
			timeout);

	omap_dispc_unregister_isr(dispc_irq_wait_handler, &completion, irqmask);

	if (timeout == 0)
		return -ETIMEDOUT;

	if (timeout == -ERESTARTSYS)
		return -ERESTARTSYS;

	return 0;
}

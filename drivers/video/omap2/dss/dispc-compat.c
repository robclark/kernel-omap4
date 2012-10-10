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

#include <video/omapdss.h>

#include "dss.h"
#include "dss_features.h"
#include "dispc-compat.h"

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

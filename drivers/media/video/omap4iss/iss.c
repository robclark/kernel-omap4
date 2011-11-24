/*
 * V4L2 Driver for OMAP4 ISS
 *
 * Copyright (C) 2011, Texas Instruments
 *
 * Author: Sergio Aguirre <saaguirre@ti.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/dma-mapping.h>
#include <linux/i2c.h>
#include <linux/interrupt.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/sched.h>
#include <linux/vmalloc.h>

#include <media/v4l2-common.h>
#include <media/v4l2-device.h>

#include "iss.h"
#include "iss_regs.h"

static void iss_save_ctx(struct iss_device *iss);

static void iss_restore_ctx(struct iss_device *iss);

/* Structure for saving/restoring ISS module registers */
static struct iss_reg iss_reg_list[] = {
	{OMAP4_ISS_MEM_TOP, ISS_HL_SYSCONFIG, 0},
	{OMAP4_ISS_MEM_TOP, ISS_CTRL, 0},
	{OMAP4_ISS_MEM_TOP, ISS_CLKCTRL, 0},
	{0, ISS_TOK_TERM, 0}
};

/*
 * omap4iss_flush - Post pending L3 bus writes by doing a register readback
 * @iss: OMAP4 ISS device
 *
 * In order to force posting of pending writes, we need to write and
 * readback the same register, in this case the revision register.
 *
 * See this link for reference:
 *   http://www.mail-archive.com/linux-omap@vger.kernel.org/msg08149.html
 */
void omap4iss_flush(struct iss_device *iss)
{
	writel(0, iss->regs[OMAP4_ISS_MEM_TOP] + ISS_HL_REVISION);
	readl(iss->regs[OMAP4_ISS_MEM_TOP] + ISS_HL_REVISION);
}

/*
 * iss_enable_interrupts - Enable ISS interrupts.
 * @iss: OMAP4 ISS device
 */
static void iss_enable_interrupts(struct iss_device *iss)
{
	static const u32 irq = ISS_HL_IRQ_CSIA;

	/* Enable HL interrupts */
	writel(irq, iss->regs[OMAP4_ISS_MEM_TOP] + ISS_HL_IRQSTATUS_5);
	writel(irq, iss->regs[OMAP4_ISS_MEM_TOP] + ISS_HL_IRQENABLE_5_SET);
}

/*
 * iss_disable_interrupts - Disable ISS interrupts.
 * @iss: OMAP4 ISS device
 */
static void iss_disable_interrupts(struct iss_device *iss)
{
	writel(-1, iss->regs[OMAP4_ISS_MEM_TOP] + ISS_HL_IRQENABLE_5_CLR);
}

static inline void iss_isr_dbg(struct iss_device *iss, u32 irqstatus)
{
	static const char *name[] = {
		"ISP_IRQ0",
		"ISP_IRQ1",
		"ISP_IRQ2",
		"ISP_IRQ3",
		"CSIA_IRQ",
		"CSIB_IRQ",
		"CCP2_IRQ0",
		"CCP2_IRQ1",
		"CCP2_IRQ2",
		"CCP2_IRQ3",
		"CBUFF_IRQ",
		"BTE_IRQ",
		"SIMCOP_IRQ0",
		"SIMCOP_IRQ1",
		"SIMCOP_IRQ2",
		"SIMCOP_IRQ3",
		"CCP2_IRQ8",
		"HS_VS_IRQ",
		"res18",
		"res19",
		"res20",
		"res21",
		"res22",
		"res23",
		"res24",
		"res25",
		"res26",
		"res27",
		"res28",
		"res29",
		"res30",
		"res31",
	};
	int i;

	dev_dbg(iss->dev, "ISS IRQ: ");

	for (i = 0; i < ARRAY_SIZE(name); i++) {
		if ((1 << i) & irqstatus)
			printk(KERN_CONT "%s ", name[i]);
	}
	printk(KERN_CONT "\n");
}

/*
 * iss_isr - Interrupt Service Routine for ISS module.
 * @irq: Not used currently.
 * @_iss: Pointer to the OMAP4 ISS device
 *
 * Handles the corresponding callback if plugged in.
 *
 * Returns IRQ_HANDLED when IRQ was correctly handled, or IRQ_NONE when the
 * IRQ wasn't handled.
 */
static irqreturn_t iss_isr(int irq, void *_iss)
{
	struct iss_device *iss = _iss;
	u32 irqstatus;

	irqstatus = readl(iss->regs[OMAP4_ISS_MEM_TOP] + ISS_HL_IRQSTATUS_5);
	writel(irqstatus, iss->regs[OMAP4_ISS_MEM_TOP] + ISS_HL_IRQSTATUS_5);

	if (irqstatus & ISS_HL_IRQ_CSIA)
		omap4iss_csi2_isr(&iss->csi2a);

	omap4iss_flush(iss);

#if defined(DEBUG) && defined(ISS_ISR_DEBUG)
	iss_isr_dbg(iss, irqstatus);
#endif

	return IRQ_HANDLED;
}

/* -----------------------------------------------------------------------------
 * Pipeline power management
 *
 * Entities must be powered up when part of a pipeline that contains at least
 * one open video device node.
 *
 * To achieve this use the entity use_count field to track the number of users.
 * For entities corresponding to video device nodes the use_count field stores
 * the users count of the node. For entities corresponding to subdevs the
 * use_count field stores the total number of users of all video device nodes
 * in the pipeline.
 *
 * The omap4iss_pipeline_pm_use() function must be called in the open() and
 * close() handlers of video device nodes. It increments or decrements the use
 * count of all subdev entities in the pipeline.
 *
 * To react to link management on powered pipelines, the link setup notification
 * callback updates the use count of all entities in the source and sink sides
 * of the link.
 */

/*
 * iss_pipeline_pm_use_count - Count the number of users of a pipeline
 * @entity: The entity
 *
 * Return the total number of users of all video device nodes in the pipeline.
 */
static int iss_pipeline_pm_use_count(struct media_entity *entity)
{
	struct media_entity_graph graph;
	int use = 0;

	media_entity_graph_walk_start(&graph, entity);

	while ((entity = media_entity_graph_walk_next(&graph))) {
		if (media_entity_type(entity) == MEDIA_ENT_T_DEVNODE)
			use += entity->use_count;
	}

	return use;
}

/*
 * iss_pipeline_pm_power_one - Apply power change to an entity
 * @entity: The entity
 * @change: Use count change
 *
 * Change the entity use count by @change. If the entity is a subdev update its
 * power state by calling the core::s_power operation when the use count goes
 * from 0 to != 0 or from != 0 to 0.
 *
 * Return 0 on success or a negative error code on failure.
 */
static int iss_pipeline_pm_power_one(struct media_entity *entity, int change)
{
	struct v4l2_subdev *subdev;

	subdev = media_entity_type(entity) == MEDIA_ENT_T_V4L2_SUBDEV
	       ? media_entity_to_v4l2_subdev(entity) : NULL;

	if (entity->use_count == 0 && change > 0 && subdev != NULL) {
		int ret;

		ret = v4l2_subdev_call(subdev, core, s_power, 1);
		if (ret < 0 && ret != -ENOIOCTLCMD)
			return ret;
	}

	entity->use_count += change;
	WARN_ON(entity->use_count < 0);

	if (entity->use_count == 0 && change < 0 && subdev != NULL)
		v4l2_subdev_call(subdev, core, s_power, 0);

	return 0;
}

/*
 * iss_pipeline_pm_power - Apply power change to all entities in a pipeline
 * @entity: The entity
 * @change: Use count change
 *
 * Walk the pipeline to update the use count and the power state of all non-node
 * entities.
 *
 * Return 0 on success or a negative error code on failure.
 */
static int iss_pipeline_pm_power(struct media_entity *entity, int change)
{
	struct media_entity_graph graph;
	struct media_entity *first = entity;
	int ret = 0;

	if (!change)
		return 0;

	media_entity_graph_walk_start(&graph, entity);

	while (!ret && (entity = media_entity_graph_walk_next(&graph)))
		if (media_entity_type(entity) != MEDIA_ENT_T_DEVNODE)
			ret = iss_pipeline_pm_power_one(entity, change);

	if (!ret)
		return 0;

	media_entity_graph_walk_start(&graph, first);

	while ((first = media_entity_graph_walk_next(&graph))
	       && first != entity)
		if (media_entity_type(first) != MEDIA_ENT_T_DEVNODE)
			iss_pipeline_pm_power_one(first, -change);

	return ret;
}

/*
 * omap4iss_pipeline_pm_use - Update the use count of an entity
 * @entity: The entity
 * @use: Use (1) or stop using (0) the entity
 *
 * Update the use count of all entities in the pipeline and power entities on or
 * off accordingly.
 *
 * Return 0 on success or a negative error code on failure. Powering entities
 * off is assumed to never fail. No failure can occur when the use parameter is
 * set to 0.
 */
int omap4iss_pipeline_pm_use(struct media_entity *entity, int use)
{
	int change = use ? 1 : -1;
	int ret;

	mutex_lock(&entity->parent->graph_mutex);

	/* Apply use count to node. */
	entity->use_count += change;
	WARN_ON(entity->use_count < 0);

	/* Apply power change to connected non-nodes. */
	ret = iss_pipeline_pm_power(entity, change);
	if (ret < 0)
		entity->use_count -= change;

	mutex_unlock(&entity->parent->graph_mutex);

	return ret;
}

/*
 * iss_pipeline_link_notify - Link management notification callback
 * @source: Pad at the start of the link
 * @sink: Pad at the end of the link
 * @flags: New link flags that will be applied
 *
 * React to link management on powered pipelines by updating the use count of
 * all entities in the source and sink sides of the link. Entities are powered
 * on or off accordingly.
 *
 * Return 0 on success or a negative error code on failure. Powering entities
 * off is assumed to never fail. This function will not fail for disconnection
 * events.
 */
static int iss_pipeline_link_notify(struct media_pad *source,
				    struct media_pad *sink, u32 flags)
{
	int source_use = iss_pipeline_pm_use_count(source->entity);
	int sink_use = iss_pipeline_pm_use_count(sink->entity);
	int ret;

	if (!(flags & MEDIA_LNK_FL_ENABLED)) {
		/* Powering off entities is assumed to never fail. */
		iss_pipeline_pm_power(source->entity, -sink_use);
		iss_pipeline_pm_power(sink->entity, -source_use);
		return 0;
	}

	ret = iss_pipeline_pm_power(source->entity, sink_use);
	if (ret < 0)
		return ret;

	ret = iss_pipeline_pm_power(sink->entity, source_use);
	if (ret < 0)
		iss_pipeline_pm_power(source->entity, -sink_use);

	return ret;
}

/* -----------------------------------------------------------------------------
 * Pipeline stream management
 */

/*
 * iss_pipeline_enable - Enable streaming on a pipeline
 * @pipe: ISS pipeline
 * @mode: Stream mode (single shot or continuous)
 *
 * Walk the entities chain starting at the pipeline output video node and start
 * all modules in the chain in the given mode.
 *
 * Return 0 if successful, or the return value of the failed video::s_stream
 * operation otherwise.
 */
static int iss_pipeline_enable(struct iss_pipeline *pipe,
			       enum iss_pipeline_stream_state mode)
{
	struct media_entity *entity;
	struct media_pad *pad;
	struct v4l2_subdev *subdev;
	unsigned long flags;
	int ret = 0;

	spin_lock_irqsave(&pipe->lock, flags);
	pipe->state &= ~(ISS_PIPELINE_IDLE_INPUT | ISS_PIPELINE_IDLE_OUTPUT);
	spin_unlock_irqrestore(&pipe->lock, flags);

	pipe->do_propagation = false;

	entity = &pipe->output->video.entity;
	while (1) {
		pad = &entity->pads[0];
		if (!(pad->flags & MEDIA_PAD_FL_SINK))
			break;

		pad = media_entity_remote_source(pad);
		if (pad == NULL ||
		    media_entity_type(pad->entity) != MEDIA_ENT_T_V4L2_SUBDEV)
			break;

		entity = pad->entity;
		subdev = media_entity_to_v4l2_subdev(entity);

		ret = v4l2_subdev_call(subdev, video, s_stream, mode);
		if (ret < 0 && ret != -ENOIOCTLCMD)
			break;
	}

	return ret;
}

/*
 * iss_pipeline_disable - Disable streaming on a pipeline
 * @pipe: ISS pipeline
 *
 * Walk the entities chain starting at the pipeline output video node and stop
 * all modules in the chain. Wait synchronously for the modules to be stopped if
 * necessary.
 */
static int iss_pipeline_disable(struct iss_pipeline *pipe)
{
	struct media_entity *entity;
	struct media_pad *pad;
	struct v4l2_subdev *subdev;
	int failure = 0;

	entity = &pipe->output->video.entity;
	while (1) {
		pad = &entity->pads[0];
		if (!(pad->flags & MEDIA_PAD_FL_SINK))
			break;

		pad = media_entity_remote_source(pad);
		if (pad == NULL ||
		    media_entity_type(pad->entity) != MEDIA_ENT_T_V4L2_SUBDEV)
			break;

		entity = pad->entity;
		subdev = media_entity_to_v4l2_subdev(entity);

		v4l2_subdev_call(subdev, video, s_stream, 0);
	}

	return failure;
}

/*
 * omap4iss_pipeline_set_stream - Enable/disable streaming on a pipeline
 * @pipe: ISS pipeline
 * @state: Stream state (stopped, single shot or continuous)
 *
 * Set the pipeline to the given stream state. Pipelines can be started in
 * single-shot or continuous mode.
 *
 * Return 0 if successful, or the return value of the failed video::s_stream
 * operation otherwise. The pipeline state is not updated when the operation
 * fails, except when stopping the pipeline.
 */
int omap4iss_pipeline_set_stream(struct iss_pipeline *pipe,
				 enum iss_pipeline_stream_state state)
{
	int ret;

	if (state == ISS_PIPELINE_STREAM_STOPPED)
		ret = iss_pipeline_disable(pipe);
	else
		ret = iss_pipeline_enable(pipe, state);

	if (ret == 0 || state == ISS_PIPELINE_STREAM_STOPPED)
		pipe->stream_state = state;

	return ret;
}

/*
 * iss_pipeline_is_last - Verify if entity has an enabled link to the output
 *			  video node
 * @me: ISS module's media entity
 *
 * Returns 1 if the entity has an enabled link to the output video node or 0
 * otherwise. It's true only while pipeline can have no more than one output
 * node.
 */
static int iss_pipeline_is_last(struct media_entity *me)
{
	struct iss_pipeline *pipe;
	struct media_pad *pad;

	if (!me->pipe)
		return 0;
	pipe = to_iss_pipeline(me);
	if (pipe->stream_state == ISS_PIPELINE_STREAM_STOPPED)
		return 0;
	pad = media_entity_remote_source(&pipe->output->pad);
	return pad->entity == me;
}

static int iss_reset(struct iss_device *iss)
{
	unsigned long timeout = 0;

	writel(readl(iss->regs[OMAP4_ISS_MEM_TOP] + ISS_HL_SYSCONFIG) |
		ISS_HL_SYSCONFIG_SOFTRESET,
		iss->regs[OMAP4_ISS_MEM_TOP] + ISS_HL_SYSCONFIG);

	while (readl(iss->regs[OMAP4_ISS_MEM_TOP] + ISS_HL_SYSCONFIG) &
			ISS_HL_SYSCONFIG_SOFTRESET) {
		if (timeout++ > 10000) {
			dev_alert(iss->dev, "cannot reset ISS\n");
			return -ETIMEDOUT;
		}
		udelay(1);
	}

	return 0;
}

/*
 * iss_save_context - Saves the values of the ISS module registers.
 * @iss: OMAP4 ISS device
 * @reg_list: Structure containing pairs of register address and value to
 *            modify on OMAP.
 */
static void
iss_save_context(struct iss_device *iss, struct iss_reg *reg_list)
{
	struct iss_reg *next = reg_list;

	for (; next->reg != ISS_TOK_TERM; next++)
		next->val = readl(iss->regs[next->mmio_range] + next->reg);
}

/*
 * iss_restore_context - Restores the values of the ISS module registers.
 * @iss: OMAP4 ISS device
 * @reg_list: Structure containing pairs of register address and value to
 *            modify on OMAP.
 */
static void
iss_restore_context(struct iss_device *iss, struct iss_reg *reg_list)
{
	struct iss_reg *next = reg_list;

	for (; next->reg != ISS_TOK_TERM; next++)
		writel(next->val, iss->regs[next->mmio_range] + next->reg);
}

/*
 * iss_save_ctx - Saves ISS context.
 * @iss: OMAP4 ISS device
 *
 * Routine for saving the context of each module in the ISS.
 */
static void iss_save_ctx(struct iss_device *iss)
{
	iss_save_context(iss, iss_reg_list);
}

/*
 * iss_restore_ctx - Restores ISS context.
 * @iss: OMAP4 ISS device
 *
 * Routine for restoring the context of each module in the ISS.
 */
static void iss_restore_ctx(struct iss_device *iss)
{
	iss_restore_context(iss, iss_reg_list);
}

/*
 * iss_module_sync_idle - Helper to sync module with its idle state
 * @me: ISS submodule's media entity
 * @wait: ISS submodule's wait queue for streamoff/interrupt synchronization
 * @stopping: flag which tells module wants to stop
 *
 * This function checks if ISS submodule needs to wait for next interrupt. If
 * yes, makes the caller to sleep while waiting for such event.
 */
int omap4iss_module_sync_idle(struct media_entity *me, wait_queue_head_t *wait,
			      atomic_t *stopping)
{
	struct iss_pipeline *pipe = to_iss_pipeline(me);

	if (pipe->stream_state == ISS_PIPELINE_STREAM_STOPPED ||
	    (pipe->stream_state == ISS_PIPELINE_STREAM_SINGLESHOT &&
	     !iss_pipeline_ready(pipe)))
		return 0;

	/*
	 * atomic_set() doesn't include memory barrier on ARM platform for SMP
	 * scenario. We'll call it here to avoid race conditions.
	 */
	atomic_set(stopping, 1);
	smp_mb();

	/*
	 * If module is the last one, it's writing to memory. In this case,
	 * it's necessary to check if the module is already paused due to
	 * DMA queue underrun or if it has to wait for next interrupt to be
	 * idle.
	 * If it isn't the last one, the function won't sleep but *stopping
	 * will still be set to warn next submodule caller's interrupt the
	 * module wants to be idle.
	 */
	if (iss_pipeline_is_last(me)) {
		struct iss_video *video = pipe->output;
		unsigned long flags;

		spin_lock_irqsave(&video->qlock, flags);
		if (video->dmaqueue_flags & ISS_VIDEO_DMAQUEUE_UNDERRUN) {
			spin_unlock_irqrestore(&video->qlock, flags);
			atomic_set(stopping, 0);
			smp_mb();
			return 0;
		}
		spin_unlock_irqrestore(&video->qlock, flags);
		if (!wait_event_timeout(*wait, !atomic_read(stopping),
					msecs_to_jiffies(1000))) {
			atomic_set(stopping, 0);
			smp_mb();
			return -ETIMEDOUT;
		}
	}

	return 0;
}

/*
 * omap4iss_module_sync_is_stopped - Helper to verify if module was stopping
 * @wait: ISS submodule's wait queue for streamoff/interrupt synchronization
 * @stopping: flag which tells module wants to stop
 *
 * This function checks if ISS submodule was stopping. In case of yes, it
 * notices the caller by setting stopping to 0 and waking up the wait queue.
 * Returns 1 if it was stopping or 0 otherwise.
 */
int omap4iss_module_sync_is_stopping(wait_queue_head_t *wait,
				     atomic_t *stopping)
{
	if (atomic_cmpxchg(stopping, 1, 0)) {
		wake_up(wait);
		return 1;
	}

	return 0;
}

/* --------------------------------------------------------------------------
 * Clock management
 */

#define ISS_CLKCTRL_MASK	(ISS_CLKCTRL_CSI2_A)

static int __iss_subclk_update(struct iss_device *iss)
{
	u32 clk = 0;
	int ret = 0, timeout = 1000;

	if (iss->subclk_resources & OMAP4_ISS_SUBCLK_CSI2_A)
		clk |= ISS_CLKCTRL_CSI2_A;

	writel((readl(iss->regs[OMAP4_ISS_MEM_TOP] + ISS_CLKCTRL) &
		~ISS_CLKCTRL_MASK) | clk,
		iss->regs[OMAP4_ISS_MEM_TOP] + ISS_CLKCTRL);

	/* Wait for HW assertion */
	while (timeout-- > 0) {
		udelay(1);
		if ((readl(iss->regs[OMAP4_ISS_MEM_TOP] + ISS_CLKSTAT) &
		     ISS_CLKCTRL_MASK) == clk)
			break;
	}

	if (!timeout)
		ret = -EBUSY;

	return ret;
}

int omap4iss_subclk_enable(struct iss_device *iss,
			    enum iss_subclk_resource res)
{
	iss->subclk_resources |= res;

	return __iss_subclk_update(iss);
}

int omap4iss_subclk_disable(struct iss_device *iss,
			     enum iss_subclk_resource res)
{
	iss->subclk_resources &= ~res;

	return __iss_subclk_update(iss);
}

/*
 * iss_enable_clocks - Enable ISS clocks
 * @iss: OMAP4 ISS device
 *
 * Return 0 if successful, or clk_enable return value if any of tthem fails.
 */
static int iss_enable_clocks(struct iss_device *iss)
{
	int r;

	r = clk_enable(iss->iss_fck);
	if (r) {
		dev_err(iss->dev, "clk_enable iss_fck failed\n");
		goto out_clk_enable_fck;
	}

	r = clk_enable(iss->iss_ctrlclk);
	if (r) {
		dev_err(iss->dev, "clk_enable iss_ctrlclk failed\n");
		goto out_clk_enable_ctrlclk;
	}
	return 0;

out_clk_enable_ctrlclk:
	clk_disable(iss->iss_fck);
out_clk_enable_fck:
	return r;
}

/*
 * iss_disable_clocks - Disable ISS clocks
 * @iss: OMAP4 ISS device
 */
static void iss_disable_clocks(struct iss_device *iss)
{
	clk_disable(iss->iss_ctrlclk);
	clk_disable(iss->iss_fck);
}

static void iss_put_clocks(struct iss_device *iss)
{
	if (iss->iss_fck) {
		clk_put(iss->iss_fck);
		iss->iss_fck = NULL;
	}

	if (iss->iss_ctrlclk) {
		clk_put(iss->iss_ctrlclk);
		iss->iss_ctrlclk = NULL;
	}
}

static int iss_get_clocks(struct iss_device *iss)
{
	iss->iss_fck = clk_get(iss->dev, "iss_fck");
	if (IS_ERR(iss->iss_fck)) {
		dev_err(iss->dev, "Unable to get iss_fck clock info\n");
		iss_put_clocks(iss);
		return PTR_ERR(iss->iss_fck);
	}

	iss->iss_ctrlclk = clk_get(iss->dev, "iss_ctrlclk");
	if (IS_ERR(iss->iss_ctrlclk)) {
		dev_err(iss->dev, "Unable to get iss_ctrlclk clock info\n");
		iss_put_clocks(iss);
		return PTR_ERR(iss->iss_fck);
	}

	return 0;
}

/*
 * omap4iss_get - Acquire the ISS resource.
 *
 * Initializes the clocks for the first acquire.
 *
 * Increment the reference count on the ISS. If the first reference is taken,
 * enable clocks and power-up all submodules.
 *
 * Return a pointer to the ISS device structure, or NULL if an error occurred.
 */
struct iss_device *omap4iss_get(struct iss_device *iss)
{
	struct iss_device *__iss = iss;

	if (iss == NULL)
		return NULL;

	mutex_lock(&iss->iss_mutex);
	if (iss->ref_count > 0)
		goto out;

	if (iss_enable_clocks(iss) < 0) {
		__iss = NULL;
		goto out;
	}

	/* We don't want to restore context before saving it! */
	if (iss->has_context)
		iss_restore_ctx(iss);
	else
		iss->has_context = 1;

	iss_enable_interrupts(iss);

out:
	if (__iss != NULL)
		iss->ref_count++;
	mutex_unlock(&iss->iss_mutex);

	return __iss;
}

/*
 * omap4iss_put - Release the ISS
 *
 * Decrement the reference count on the ISS. If the last reference is released,
 * power-down all submodules, disable clocks and free temporary buffers.
 */
void omap4iss_put(struct iss_device *iss)
{
	if (iss == NULL)
		return;

	mutex_lock(&iss->iss_mutex);
	BUG_ON(iss->ref_count == 0);
	if (--iss->ref_count == 0) {
		iss_disable_interrupts(iss);
		iss_save_ctx(iss);
		iss_disable_clocks(iss);
	}
	mutex_unlock(&iss->iss_mutex);
}

static int iss_map_mem_resource(struct platform_device *pdev,
				struct iss_device *iss,
				enum iss_mem_resources res)
{
	struct resource *mem;

	/* request the mem region for the camera registers */

	mem = platform_get_resource(pdev, IORESOURCE_MEM, res);
	if (!mem) {
		dev_err(iss->dev, "no mem resource?\n");
		return -ENODEV;
	}

	if (!request_mem_region(mem->start, resource_size(mem), pdev->name)) {
		dev_err(iss->dev,
			"cannot reserve camera register I/O region\n");
		return -ENODEV;
	}
	iss->res[res] = mem;

	/* map the region */
	iss->regs[res] = ioremap_nocache(mem->start, resource_size(mem));
	if (!iss->regs[res]) {
		dev_err(iss->dev, "cannot map camera register I/O region\n");
		return -ENODEV;
	}

	return 0;
}

static void iss_unregister_entities(struct iss_device *iss)
{
	omap4iss_csi2_unregister_entities(&iss->csi2a);

	v4l2_device_unregister(&iss->v4l2_dev);
	media_device_unregister(&iss->media_dev);
}

/*
 * iss_register_subdev_group - Register a group of subdevices
 * @iss: OMAP4 ISS device
 * @board_info: I2C subdevs board information array
 *
 * Register all I2C subdevices in the board_info array. The array must be
 * terminated by a NULL entry, and the first entry must be the sensor.
 *
 * Return a pointer to the sensor media entity if it has been successfully
 * registered, or NULL otherwise.
 */
static struct v4l2_subdev *
iss_register_subdev_group(struct iss_device *iss,
		     struct iss_subdev_i2c_board_info *board_info)
{
	struct v4l2_subdev *sensor = NULL;
	unsigned int first;

	if (board_info->board_info == NULL)
		return NULL;

	for (first = 1; board_info->board_info; ++board_info, first = 0) {
		struct v4l2_subdev *subdev;
		struct i2c_adapter *adapter;

		adapter = i2c_get_adapter(board_info->i2c_adapter_id);
		if (adapter == NULL) {
			printk(KERN_ERR "%s: Unable to get I2C adapter %d for "
				"device %s\n", __func__,
				board_info->i2c_adapter_id,
				board_info->board_info->type);
			continue;
		}

		subdev = v4l2_i2c_new_subdev_board(&iss->v4l2_dev, adapter,
				board_info->board_info, NULL);
		if (subdev == NULL) {
			printk(KERN_ERR "%s: Unable to register subdev %s\n",
				__func__, board_info->board_info->type);
			continue;
		}

		if (first)
			sensor = subdev;
	}

	return sensor;
}

static int iss_register_entities(struct iss_device *iss)
{
	struct iss_platform_data *pdata = iss->pdata;
	struct iss_v4l2_subdevs_group *subdevs;
	int ret;

	iss->media_dev.dev = iss->dev;
	strlcpy(iss->media_dev.model, "TI OMAP4 ISS",
		sizeof(iss->media_dev.model));
	iss->media_dev.link_notify = iss_pipeline_link_notify;
	ret = media_device_register(&iss->media_dev);
	if (ret < 0) {
		printk(KERN_ERR "%s: Media device registration failed (%d)\n",
			__func__, ret);
		return ret;
	}

	iss->v4l2_dev.mdev = &iss->media_dev;
	ret = v4l2_device_register(iss->dev, &iss->v4l2_dev);
	if (ret < 0) {
		printk(KERN_ERR "%s: V4L2 device registration failed (%d)\n",
			__func__, ret);
		goto done;
	}

	/* Register internal entities */
	ret = omap4iss_csi2_register_entities(&iss->csi2a, &iss->v4l2_dev);
	if (ret < 0)
		goto done;

	/* Register external entities */
	for (subdevs = pdata->subdevs; subdevs && subdevs->subdevs; ++subdevs) {
		struct v4l2_subdev *sensor;
		struct media_entity *input;
		unsigned int flags;
		unsigned int pad;

		sensor = iss_register_subdev_group(iss, subdevs->subdevs);
		if (sensor == NULL)
			continue;

		sensor->host_priv = subdevs;

		/* Connect the sensor to the correct interface module.
		 * CSI2a receiver through CSIPHY1.
		 */
		switch (subdevs->interface) {
		case ISS_INTERFACE_CSI2A_PHY1:
			input = &iss->csi2a.subdev.entity;
			pad = CSI2_PAD_SINK;
			flags = MEDIA_LNK_FL_IMMUTABLE
			      | MEDIA_LNK_FL_ENABLED;
			break;

		default:
			printk(KERN_ERR "%s: invalid interface type %u\n",
			       __func__, subdevs->interface);
			ret = -EINVAL;
			goto done;
		}

		ret = media_entity_create_link(&sensor->entity, 0, input, pad,
					       flags);
		if (ret < 0)
			goto done;
	}

	ret = v4l2_device_register_subdev_nodes(&iss->v4l2_dev);

done:
	if (ret < 0)
		iss_unregister_entities(iss);

	return ret;
}

static void iss_cleanup_modules(struct iss_device *iss)
{
	omap4iss_csi2_cleanup(iss);
}

static int iss_initialize_modules(struct iss_device *iss)
{
	int ret;

	ret = omap4iss_csiphy_init(iss);
	if (ret < 0) {
		dev_err(iss->dev, "CSI PHY initialization failed\n");
		goto error_csiphy;
	}

	ret = omap4iss_csi2_init(iss);
	if (ret < 0) {
		dev_err(iss->dev, "CSI2 initialization failed\n");
		goto error_csi2;
	}

	return 0;

error_csi2:
error_csiphy:
	return ret;
}

static int iss_probe(struct platform_device *pdev)
{
	struct iss_platform_data *pdata = pdev->dev.platform_data;
	struct iss_device *iss;
	int i, ret;

	if (pdata == NULL)
		return -EINVAL;

	iss = kzalloc(sizeof(*iss), GFP_KERNEL);
	if (!iss) {
		dev_err(&pdev->dev, "Could not allocate memory\n");
		return -ENOMEM;
	}

	mutex_init(&iss->iss_mutex);

	iss->dev = &pdev->dev;
	iss->pdata = pdata;
	iss->ref_count = 0;

	iss->raw_dmamask = DMA_BIT_MASK(32);
	iss->dev->dma_mask = &iss->raw_dmamask;
	iss->dev->coherent_dma_mask = DMA_BIT_MASK(32);

	platform_set_drvdata(pdev, iss);

	/* Clocks */
	ret = iss_map_mem_resource(pdev, iss, OMAP4_ISS_MEM_TOP);
	if (ret < 0)
		goto error;

	ret = iss_get_clocks(iss);
	if (ret < 0)
		goto error;

	if (omap4iss_get(iss) == NULL)
		goto error;

	ret = iss_reset(iss);
	if (ret < 0)
		goto error_iss;

	iss->revision = readl(iss->regs[OMAP4_ISS_MEM_TOP] + ISS_HL_REVISION);
	dev_info(iss->dev, "Revision %08x found\n", iss->revision);

	for (i = 1; i < OMAP4_ISS_MEM_LAST; i++) {
		ret = iss_map_mem_resource(pdev, iss, i);
		if (ret)
			goto error_iss;
	}

	/* Interrupt */
	iss->irq_num = platform_get_irq(pdev, 0);
	if (iss->irq_num <= 0) {
		dev_err(iss->dev, "No IRQ resource\n");
		ret = -ENODEV;
		goto error_iss;
	}

	if (request_irq(iss->irq_num, iss_isr, IRQF_SHARED, "OMAP4 ISS", iss)) {
		dev_err(iss->dev, "Unable to request IRQ\n");
		ret = -EINVAL;
		goto error_iss;
	}

	/* Entities */
	ret = iss_initialize_modules(iss);
	if (ret < 0)
		goto error_irq;

	ret = iss_register_entities(iss);
	if (ret < 0)
		goto error_modules;

	omap4iss_put(iss);

	return 0;

error_modules:
	iss_cleanup_modules(iss);
error_irq:
	free_irq(iss->irq_num, iss);
error_iss:
	omap4iss_put(iss);
error:
	iss_put_clocks(iss);

	for (i = 0; i < OMAP4_ISS_MEM_LAST; i++) {
		if (iss->regs[i]) {
			iounmap(iss->regs[i]);
			iss->regs[i] = NULL;
		}

		if (iss->res[i]) {
			release_mem_region(iss->res[i]->start,
					   resource_size(iss->res[i]));
			iss->res[i] = NULL;
		}
	}
	platform_set_drvdata(pdev, NULL);
	kfree(iss);

	return ret;
}

static int iss_remove(struct platform_device *pdev)
{
	struct iss_device *iss = platform_get_drvdata(pdev);
	int i;

	iss_unregister_entities(iss);
	iss_cleanup_modules(iss);

	free_irq(iss->irq_num, iss);
	iss_put_clocks(iss);

	for (i = 0; i < OMAP4_ISS_MEM_LAST; i++) {
		if (iss->regs[i]) {
			iounmap(iss->regs[i]);
			iss->regs[i] = NULL;
		}

		if (iss->res[i]) {
			release_mem_region(iss->res[i]->start,
					   resource_size(iss->res[i]));
			iss->res[i] = NULL;
		}
	}

	kfree(iss);

	return 0;
}

static struct platform_device_id omap4iss_id_table[] = {
	{ "omap4iss", 0 },
	{ },
};
MODULE_DEVICE_TABLE(platform, omap4iss_id_table);

static struct platform_driver iss_driver = {
	.probe		= iss_probe,
	.remove		= iss_remove,
	.id_table	= omap4iss_id_table,
	.driver = {
		.owner	= THIS_MODULE,
		.name	= "omap4iss",
	},
};

static int __init iss_init(void)
{
	return platform_driver_register(&iss_driver);
}

static void __exit iss_exit(void)
{
	platform_driver_unregister(&iss_driver);
}

/*
 * FIXME: Had to make it late_initcall. Strangely while being module_init,
 * The I2C communication was failing in the sensor, because no XCLK was
 * provided.
 */
late_initcall(iss_init);
module_exit(iss_exit);

MODULE_DESCRIPTION("TI OMAP4 ISS driver");
MODULE_AUTHOR("Sergio Aguirre <saaguirre@ti.com>");
MODULE_LICENSE("GPL");

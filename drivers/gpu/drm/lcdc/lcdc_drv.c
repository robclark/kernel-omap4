/*
 * Copyright (C) 2012 Texas Instruments
 * Author: Rob Clark <robdclark@gmail.com>
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

/* LCDC DRM driver, based on da8xx-fb */

#include "lcdc_drv.h"
#include "lcdc_regs.h"
#include "lcdc_tfp410.h"
#include "lcdc_panel.h"
#include "lcdc_slave.h"

#include "drm_fb_helper.h"

static LIST_HEAD(module_list);

void lcdc_module_init(struct lcdc_module *mod, const char *name,
		const struct lcdc_module_ops *funcs)
{
	mod->name = name;
	mod->funcs = funcs;
	INIT_LIST_HEAD(&mod->list);
	list_add(&mod->list, &module_list);
}

void lcdc_module_cleanup(struct lcdc_module *mod)
{
	list_del(&mod->list);
}

static struct of_device_id lcdc_of_match[];

static struct drm_framebuffer *lcdc_fb_create(struct drm_device *dev,
		struct drm_file *file_priv, struct drm_mode_fb_cmd2 *mode_cmd)
{
	// XXX todo, check supported format, pitch, etc..
	return drm_fb_cma_create(dev, file_priv, mode_cmd);
}

static void lcdc_fb_output_poll_changed(struct drm_device *dev)
{
	struct lcdc_drm_private *priv = dev->dev_private;
	if (priv->fbdev)
		drm_fbdev_cma_hotplug_event(priv->fbdev);
}

static const struct drm_mode_config_funcs mode_config_funcs = {
	.fb_create = lcdc_fb_create,
	.output_poll_changed = lcdc_fb_output_poll_changed,
};

static int modeset_init(struct drm_device *dev)
{
	struct lcdc_drm_private *priv = dev->dev_private;
	struct lcdc_module *mod;

	drm_mode_config_init(dev);

	priv->crtc = lcdc_crtc_create(dev);

	list_for_each_entry(mod, &module_list, list) {
		DBG("loading module: %s", mod->name);
		mod->funcs->modeset_init(mod, dev);
	}

	if ((priv->num_encoders = 0) || (priv->num_connectors == 0)) {
		/* oh nos! */
		dev_err(dev->dev, "no encoders/connectors found\n");
		return -ENXIO;
	}

	dev->mode_config.min_width = 0;
	dev->mode_config.min_height = 0;
	dev->mode_config.max_width = lcdc_crtc_max_width(priv->crtc);
	dev->mode_config.max_height = 2048;
	dev->mode_config.funcs = &mode_config_funcs;

	return 0;
}

#ifdef CONFIG_CPU_FREQ
static int cpufreq_transition(struct notifier_block *nb,
				     unsigned long val, void *data)
{
	struct lcdc_drm_private *priv = container_of(nb,
			struct lcdc_drm_private, freq_transition);
	if (val == CPUFREQ_POSTCHANGE) {
		if (priv->lcd_fck_rate != clk_get_rate(priv->clk)) {
			priv->lcd_fck_rate = clk_get_rate(priv->clk);
			lcdc_crtc_update_clk(priv->crtc);
		}
	}

	return 0;
}
#endif

/*
 * DRM operations:
 */

static int lcdc_unload(struct drm_device *dev)
{
	struct lcdc_drm_private *priv = dev->dev_private;
	struct lcdc_module *mod, *cur;

	drm_kms_helper_poll_fini(dev);
	drm_mode_config_cleanup(dev);
	drm_vblank_cleanup(dev);

	pm_runtime_get_sync(dev->dev);
	drm_irq_uninstall(dev);
	pm_runtime_put_sync(dev->dev);

#ifdef CONFIG_CPU_FREQ
	cpufreq_unregister_notifier(&priv->freq_transition,
			CPUFREQ_TRANSITION_NOTIFIER);
#endif

	if (priv->clk)
		clk_put(priv->clk);

	if (priv->mmio)
		iounmap(priv->mmio);

	flush_workqueue(priv->wq);
	destroy_workqueue(priv->wq);

	dev->dev_private = NULL;

	pm_runtime_disable(dev->dev);

	list_for_each_entry_safe(mod, cur, &module_list, list) {
		DBG("destroying module: %s", mod->name);
		mod->funcs->destroy(mod);
	}

	kfree(priv);

	return 0;
}

static int lcdc_load(struct drm_device *dev, unsigned long flags)
{
	struct platform_device *pdev = dev->platformdev;
	struct lcdc_drm_private *priv;
	struct resource *res;
	int ret;

	priv = kzalloc(sizeof(*priv), GFP_KERNEL);
	if (!priv) {
		dev_err(dev->dev, "failed to allocate private data\n");
		return -ENOMEM;
	}

	dev->dev_private = priv;

	priv->wq = alloc_ordered_workqueue("lcdc", 0);

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res) {
		dev_err(dev->dev, "failed to get memory resource\n");
		ret = -EINVAL;
		goto fail;
	}

	priv->mmio = ioremap_nocache(res->start, resource_size(res));
	if (!priv->mmio) {
		dev_err(dev->dev, "failed to ioremap\n");
		ret = -ENOMEM;
		goto fail;
	}

	priv->clk = clk_get(dev->dev, "fck");
	if (IS_ERR(priv->clk)) {
		dev_err(dev->dev, "failed to get functional clock\n");
		ret = -ENODEV;
		goto fail;
	}

	priv->disp_clk = clk_get(dev->dev, "dpll_disp_ck");
	if (IS_ERR(priv->clk)) {
		dev_err(dev->dev, "failed to get display clock\n");
		ret = -ENODEV;
		goto fail;
	}

#ifdef CONFIG_CPU_FREQ
	priv->lcd_fck_rate = clk_get_rate(priv->clk);
	priv->freq_transition.notifier_call = cpufreq_transition;
	ret = cpufreq_register_notifier(&priv->freq_transition,
			CPUFREQ_TRANSITION_NOTIFIER);
	if (ret) {
		dev_err(dev->dev, "failed to register cpufreq notifier\n");
		goto fail;
	}
#endif

	pm_runtime_enable(dev->dev);

	/* Determine LCD IP Version */
	pm_runtime_get_sync(dev->dev);
	switch (lcdc_read(dev, LCDC_PID_REG)) {
	case 0x4C100102:
		priv->rev = 1;
		break;
	case 0x4F200800:
	case 0x4F201000:
		priv->rev = 2;
		break;
	default:
		dev_warn(dev->dev, "Unknown PID Reg value 0x%08x, "
				"defaulting to LCD revision 1\n",
				lcdc_read(dev, LCDC_PID_REG));
		priv->rev = 1;
		break;
	}

	pm_runtime_put_sync(dev->dev);

	ret = modeset_init(dev);
	if (ret < 0) {
		dev_err(dev->dev, "failed to initialize mode setting\n");
		goto fail;
	}

	ret = drm_vblank_init(dev, 1);
	if (ret < 0) {
		dev_err(dev->dev, "failed to initialize vblank\n");
		goto fail;
	}

	pm_runtime_get_sync(dev->dev);
	ret = drm_irq_install(dev);
	pm_runtime_put_sync(dev->dev);
	if (ret < 0) {
		dev_err(dev->dev, "failed to install IRQ handler\n");
		goto fail;
	}

	platform_set_drvdata(pdev, dev);

	priv->fbdev = drm_fbdev_cma_init(dev, 16,
			dev->mode_config.num_crtc,
			dev->mode_config.num_connector);

	drm_kms_helper_poll_init(dev);

	return 0;

fail:
	lcdc_unload(dev);
	return ret;
}

static void lcdc_preclose(struct drm_device *dev, struct drm_file *file)
{
	struct lcdc_drm_private *priv = dev->dev_private;

	lcdc_crtc_cancel_page_flip(priv->crtc, file);
}

static void lcdc_lastclose(struct drm_device *dev)
{
	struct lcdc_drm_private *priv = dev->dev_private;
	drm_fbdev_cma_restore_mode(priv->fbdev);
}

static irqreturn_t lcdc_irq(DRM_IRQ_ARGS)
{
	struct drm_device *dev = arg;
	struct lcdc_drm_private *priv = dev->dev_private;
	return lcdc_crtc_irq(priv->crtc);
}

static void lcdc_irq_preinstall(struct drm_device *dev)
{
	lcdc_clear_irqstatus(dev, 0xffffffff);
}

static int lcdc_irq_postinstall(struct drm_device *dev)
{
	struct lcdc_drm_private *priv = dev->dev_private;

	/* enable FIFO underflow irq: */
	if (priv->rev == 1) {
		lcdc_set(dev, LCDC_RASTER_CTRL_REG, LCDC_V1_UNDERFLOW_INT_ENA);
	} else {
		lcdc_set(dev, LCDC_INT_ENABLE_SET_REG, LCDC_V2_UNDERFLOW_INT_ENA);
	}

	return 0;
}

static void lcdc_irq_uninstall(struct drm_device *dev)
{
	struct lcdc_drm_private *priv = dev->dev_private;

	/* disable irqs that we might have enabled: */
	if (priv->rev == 1) {
		lcdc_clear(dev, LCDC_RASTER_CTRL_REG,
				LCDC_V1_UNDERFLOW_INT_ENA | LCDC_V1_PL_INT_ENA);
		lcdc_clear(dev, LCDC_DMA_CTRL_REG, LCDC_V1_END_OF_FRAME_INT_ENA);
	} else {
		lcdc_clear(dev, LCDC_INT_ENABLE_SET_REG,
			LCDC_V2_UNDERFLOW_INT_ENA | LCDC_V2_PL_INT_ENA |
			LCDC_V2_END_OF_FRAME0_INT_ENA | LCDC_V2_END_OF_FRAME1_INT_ENA |
			LCDC_FRAME_DONE);
	}

}

static void enable_vblank(struct drm_device *dev, bool enable)
{
	struct lcdc_drm_private *priv = dev->dev_private;
	u32 reg, mask;

	if (priv->rev == 1) {
		reg = LCDC_DMA_CTRL_REG;
		mask = LCDC_V1_END_OF_FRAME_INT_ENA;
	} else {
		reg = LCDC_INT_ENABLE_SET_REG;
		mask = LCDC_V2_END_OF_FRAME0_INT_ENA |
			LCDC_V2_END_OF_FRAME1_INT_ENA | LCDC_FRAME_DONE;
	}

	if (enable)
		lcdc_set(dev, reg, mask);
	else
		lcdc_clear(dev, reg, mask);
}

static int lcdc_enable_vblank(struct drm_device *dev, int crtc)
{
	enable_vblank(dev, true);
	return 0;
}

static void lcdc_disable_vblank(struct drm_device *dev, int crtc)
{
	enable_vblank(dev, false);
}

#if defined(CONFIG_DEBUG_FS) || defined(CONFIG_PM_SLEEP)
static const struct {
	const char *name;
	uint8_t  rev;
	uint8_t  save;
	uint32_t reg;
} registers[] = 	{
#define REG(rev, save, reg) { #reg, rev, save, reg }
		/* exists in revision 1: */
		REG(1, false, LCDC_PID_REG),
		REG(1, true,  LCDC_CTRL_REG),
		REG(1, false, LCDC_STAT_REG),
		REG(1, true,  LCDC_RASTER_CTRL_REG),
		REG(1, true,  LCDC_RASTER_TIMING_0_REG),
		REG(1, true,  LCDC_RASTER_TIMING_1_REG),
		REG(1, true,  LCDC_RASTER_TIMING_2_REG),
		REG(1, true,  LCDC_DMA_CTRL_REG),
		REG(1, true,  LCDC_DMA_FB_BASE_ADDR_0_REG),
		REG(1, true,  LCDC_DMA_FB_CEILING_ADDR_0_REG),
		REG(1, true,  LCDC_DMA_FB_BASE_ADDR_1_REG),
		REG(1, true,  LCDC_DMA_FB_CEILING_ADDR_1_REG),
		/* new in revision 2: */
		REG(2, false, LCDC_RAW_STAT_REG),
		REG(2, false, LCDC_MASKED_STAT_REG),
		REG(2, false, LCDC_INT_ENABLE_SET_REG),
		REG(2, false, LCDC_INT_ENABLE_CLR_REG),
		REG(2, false, LCDC_END_OF_INT_IND_REG),
		REG(2, true,  LCDC_CLK_ENABLE_REG),
		REG(2, true,  LCDC_INT_ENABLE_SET_REG),
#undef REG
};
#endif

#ifdef CONFIG_DEBUG_FS
static int lcdc_regs_show(struct seq_file *m, void *arg)
{
	struct drm_info_node *node = (struct drm_info_node *) m->private;
	struct drm_device *dev = node->minor->dev;
	struct lcdc_drm_private *priv = dev->dev_private;
	unsigned i;

	pm_runtime_get_sync(dev->dev);

	seq_printf(m, "revision: %d\n", priv->rev);

	for (i = 0; i < ARRAY_SIZE(registers); i++)
		if (priv->rev >= registers[i].rev)
			seq_printf(m, "%s:\t %08x\n", registers[i].name,
					lcdc_read(dev, registers[i].reg));

	pm_runtime_put_sync(dev->dev);

	return 0;
}

static int lcdc_mm_show(struct seq_file *m, void *arg)
{
	struct drm_info_node *node = (struct drm_info_node *) m->private;
	struct drm_device *dev = node->minor->dev;
	return drm_mm_dump_table(m, dev->mm_private);
}

static struct drm_info_list lcdc_debugfs_list[] = {
		{ "regs", lcdc_regs_show, 0 },
		{ "mm",   lcdc_mm_show,   0 },
		{ "fb",   drm_fb_cma_debugfs_show, 0 },
};

static int lcdc_debugfs_init(struct drm_minor *minor)
{
	struct drm_device *dev = minor->dev;
	struct lcdc_module *mod;
	int ret;

	ret = drm_debugfs_create_files(lcdc_debugfs_list,
			ARRAY_SIZE(lcdc_debugfs_list),
			minor->debugfs_root, minor);

	list_for_each_entry(mod, &module_list, list)
		if (mod->funcs->debugfs_init)
			mod->funcs->debugfs_init(mod, minor);

	if (ret) {
		dev_err(dev->dev, "could not install lcdc_debugfs_list\n");
		return ret;
	}

	return ret;
}

static void lcdc_debugfs_cleanup(struct drm_minor *minor)
{
	struct lcdc_module *mod;
	drm_debugfs_remove_files(lcdc_debugfs_list,
			ARRAY_SIZE(lcdc_debugfs_list), minor);

	list_for_each_entry(mod, &module_list, list)
		if (mod->funcs->debugfs_cleanup)
			mod->funcs->debugfs_cleanup(mod, minor);
}
#endif

static const struct file_operations fops = {
	.owner              = THIS_MODULE,
	.open               = drm_open,
	.release            = drm_release,
	.unlocked_ioctl     = drm_ioctl,
#ifdef CONFIG_COMPAT
	.compat_ioctl       = drm_compat_ioctl,
#endif
	.poll               = drm_poll,
	.read               = drm_read,
	.fasync             = drm_fasync,
	.llseek             = no_llseek,
	.mmap               = drm_gem_cma_mmap,
};

static struct drm_driver lcdc_driver = {
	.driver_features    = DRIVER_HAVE_IRQ | DRIVER_GEM | DRIVER_MODESET,
	.load               = lcdc_load,
	.unload             = lcdc_unload,
	.preclose           = lcdc_preclose,
	.lastclose          = lcdc_lastclose,
	.irq_handler        = lcdc_irq,
	.irq_preinstall     = lcdc_irq_preinstall,
	.irq_postinstall    = lcdc_irq_postinstall,
	.irq_uninstall      = lcdc_irq_uninstall,
	.get_vblank_counter = drm_vblank_count,
	.enable_vblank      = lcdc_enable_vblank,
	.disable_vblank     = lcdc_disable_vblank,
	.gem_free_object    = drm_gem_cma_free_object,
	.gem_vm_ops         = &drm_gem_cma_vm_ops,
	.dumb_create        = drm_gem_cma_dumb_create,
	.dumb_map_offset    = drm_gem_cma_dumb_map_offset,
	.dumb_destroy       = drm_gem_cma_dumb_destroy,
#ifdef CONFIG_DEBUG_FS
	.debugfs_init       = lcdc_debugfs_init,
	.debugfs_cleanup    = lcdc_debugfs_cleanup,
#endif
	.fops               = &fops,
	.name               = "lcdc",
	.desc               = "TI LCD Controller DRM",
	.date               = "20121205",
	.major              = 1,
	.minor              = 0,
};

/*
 * Power management:
 */

#if CONFIG_PM_SLEEP
static int lcdc_pm_suspend(struct device *dev)
{
	struct drm_device *ddev = dev_get_drvdata(dev);
	struct lcdc_drm_private *priv = ddev->dev_private;
	unsigned i, n = 0;

	drm_kms_helper_poll_disable(ddev);

	/* Save register state: */
	for (i = 0; i < ARRAY_SIZE(registers); i++)
		if (registers[i].save && (priv->rev >= registers[i].rev))
			priv->saved_register[n++] = lcdc_read(ddev, registers[i].reg);

	return 0;
}

static int lcdc_pm_resume(struct device *dev)
{
	struct drm_device *ddev = dev_get_drvdata(dev);
	struct lcdc_drm_private *priv = ddev->dev_private;
	unsigned i, n = 0;

	/* Restore register state: */
	for (i = 0; i < ARRAY_SIZE(registers); i++)
		if (registers[i].save && (priv->rev >= registers[i].rev))
			lcdc_write(ddev, registers[i].reg, priv->saved_register[n++]);

	drm_kms_helper_poll_enable(ddev);

	return 0;
}
#endif

static const struct dev_pm_ops lcdc_pm_ops = {
	SET_SYSTEM_SLEEP_PM_OPS(lcdc_pm_suspend, lcdc_pm_resume)
};

/*
 * Platform driver:
 */

static int __devinit lcdc_pdev_probe(struct platform_device *pdev)
{
	/* bail out early if no DT data: */
	if (!of_match_device(lcdc_of_match, &pdev->dev)) {
		dev_err(&pdev->dev, "device-tree data is missing\n");
		return -ENXIO;
	}

	/* XXX hack until I can figure out how to make hwmod clk names
	 * match devicetree..  otherwise device name ends up "4830e000.fb"
	 * and clk_get() fails:
	 */
	pdev->dev.kobj.name = "da8xx_lcdc.0";

	return drm_platform_init(&lcdc_driver, pdev);
}

static int __devexit lcdc_pdev_remove(struct platform_device *pdev)
{
	drm_platform_exit(&lcdc_driver, pdev);

	return 0;
}

static struct of_device_id lcdc_of_match[] = {
		{ .compatible = "ti,am33xx-lcdc", },
		{ },
};
MODULE_DEVICE_TABLE(of, lcdc_of_match);

static struct platform_driver lcdc_platform_driver = {
	.probe      = lcdc_pdev_probe,
	.remove     = __devexit_p(lcdc_pdev_remove),
	.driver     = {
		.owner  = THIS_MODULE,
		.name   = "lcdc",
		.pm     = &lcdc_pm_ops,
		.of_match_table = lcdc_of_match,
	},
};

static int __init lcdc_drm_init(void)
{
	DBG("init");
	lcdc_tfp410_init();
	lcdc_panel_init();
	lcdc_slave_init();
	return platform_driver_register(&lcdc_platform_driver);
}

static void __exit lcdc_drm_fini(void)
{
	DBG("fini");
	lcdc_tfp410_fini();
	lcdc_panel_fini();
	lcdc_slave_fini();
	platform_driver_unregister(&lcdc_platform_driver);
}

late_initcall(lcdc_drm_init);
module_exit(lcdc_drm_fini);

MODULE_AUTHOR("Rob Clark <robdclark@gmail.com");
MODULE_DESCRIPTION("TI LCD Controller DRM Driver");
MODULE_LICENSE("GPL");

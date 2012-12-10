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

static struct of_device_id lcdc_of_match[];

static struct drm_framebuffer *lcdc_fb_create(struct drm_device *dev,
		struct drm_file *file_priv, struct drm_mode_fb_cmd2 *mode_cmd)
{
	// XXX todo, check supported format, pitch, etc..
	return drm_fb_cma_create(dev, file_priv, mode_cmd);
}

static const struct drm_mode_config_funcs mode_config_funcs = {
	.fb_create = lcdc_fb_create,
};

static int modeset_init(struct drm_device *dev)
{
	struct lcdc_drm_private *priv = dev->dev_private;

	drm_mode_config_init(dev);

	priv->crtc = lcdc_crtc_create(dev);
	priv->encoder = lcdc_encoder_create(dev);
	priv->connector = lcdc_connector_create(dev, priv->encoder);

	dev->mode_config.min_width = 0;
	dev->mode_config.min_height = 0;
	dev->mode_config.max_width = 2048;
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

	drm_kms_helper_poll_fini(dev);
	drm_mode_config_cleanup(dev);
	drm_vblank_cleanup(dev);
	drm_irq_uninstall(dev);

#ifdef CONFIG_CPU_FREQ
	cpufreq_unregister_notifier(&priv->freq_transition,
			CPUFREQ_TRANSITION_NOTIFIER);
#endif

	if (priv->clk)
		clk_put(priv->clk);

	if (priv->mmio)
		iounmap(priv->mmio);

	dev->dev_private = NULL;

	pm_runtime_disable(dev->dev);

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

	priv->clk = clk_get(dev->dev, NULL);
	if (IS_ERR(priv->clk)) {
		dev_err(dev->dev, "failed to get clock\n");
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

	spin_lock_init(&priv->irq_lock);

	pm_runtime_enable(dev->dev);

	pm_runtime_get_sync(dev->dev);

	/* Determine LCD IP Version */
	switch (lcdc_read(dev, LCDC_PID_REG)) {
	case 0x4C100102:
		priv->rev = 1;
		break;
	case 0x4F200800:
		priv->rev = 2;
		break;
	default:
		dev_warn(dev->dev, "Unknown PID Reg value 0x08%x, "
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

	ret = drm_irq_install(dev);
	if (ret < 0) {
		dev_err(dev->dev, "failed to install IRQ handler\n");
		goto fail;
	}

	platform_set_drvdata(pdev, dev);

	// TODO version1 can't do >16bpp..
	priv->fbdev = drm_fbdev_cma_init(dev, 32,
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

static irqreturn_t lcdc_irq(DRM_IRQ_ARGS)
{
	struct drm_device *dev = arg;
	struct lcdc_drm_private *priv = dev->dev_private;
	return lcdc_crtc_irq(priv->crtc);
}

static void lcdc_irq_preinstall(struct drm_device *dev)
{
	pm_runtime_get_sync(dev->dev);
	lcdc_clear_irqstatus(dev, 0xffffffff);
	pm_runtime_put_sync(dev->dev);
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
	unsigned long flags;
	u32 reg, mask;

	if (priv->rev == 1) {
		reg = LCDC_DMA_CTRL_REG;
		mask = LCDC_V1_END_OF_FRAME_INT_ENA;
	} else {
		reg = LCDC_INT_ENABLE_SET_REG;
		mask = LCDC_V2_END_OF_FRAME0_INT_ENA |
			LCDC_V2_END_OF_FRAME1_INT_ENA | LCDC_FRAME_DONE;
	}

	spin_lock_irqsave(&priv->irq_lock, flags);

	if (enable)
		lcdc_set(dev, reg, mask);
	else
		lcdc_clear(dev, reg, mask);

	spin_unlock_irqrestore(&priv->irq_lock, flags);
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
	u32 rev;
	u32 reg;
} registers[] = 	{
#define REG(rev, reg) { #reg, rev, reg }
		/* new in revision 2: */
		REG(2, LCDC_CLK_ENABLE_REG),
		REG(2, LCDC_INT_ENABLE_SET_REG),
		/* exists in revision 1: */
		REG(1, LCDC_CTRL_REG),
		REG(1, LCDC_DMA_CTRL_REG),
		REG(1, LCDC_RASTER_TIMING_0_REG),
		REG(1, LCDC_RASTER_TIMING_1_REG),
		REG(1, LCDC_RASTER_TIMING_2_REG),
		REG(1, LCDC_DMA_FRM_BUF_BASE_ADDR_0_REG),
		REG(1, LCDC_DMA_FRM_BUF_CEILING_ADDR_0_REG),
		REG(1, LCDC_DMA_FRM_BUF_BASE_ADDR_1_REG),
		REG(1, LCDC_DMA_FRM_BUF_CEILING_ADDR_1_REG),
		REG(1, LCDC_RASTER_CTRL_REG),
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

	seq_printf(m, "revision: %d\n", priv->rev);

	for (i = 0; i < ARRAY_SIZE(registers); i++)
		if (priv->rev >= registers[i].rev)
			seq_printf(m, "%s:\t %08x\n", registers[i].name,
					lcdc_read(dev, registers[i].reg));

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
	int ret;

	ret = drm_debugfs_create_files(lcdc_debugfs_list,
			ARRAY_SIZE(lcdc_debugfs_list),
			minor->debugfs_root, minor);

	if (ret) {
		dev_err(dev->dev, "could not install lcdc_debugfs_list\n");
		return ret;
	}

	return ret;
}

static void lcdc_debugfs_cleanup(struct drm_minor *minor)
{
	drm_debugfs_remove_files(lcdc_debugfs_list,
			ARRAY_SIZE(lcdc_debugfs_list), minor);
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
	unsigned i;

	drm_kms_helper_poll_disable(ddev);

	/* Save register state: */
	for (i = 0; i < ARRAY_SIZE(registers); i++)
		if (priv->rev >= registers[i].rev)
			priv->saved_register[i] = lcdc_read(ddev, registers[i].reg);

	return 0;
}

static int lcdc_pm_resume(struct device *dev)
{
	struct drm_device *ddev = dev_get_drvdata(dev);
	struct lcdc_drm_private *priv = ddev->dev_private;
	unsigned i;

	/* Restore register state: */
	for (i = 0; i < ARRAY_SIZE(registers); i++)
		if (priv->rev >= registers[i].rev)
			lcdc_write(ddev, registers[i].reg, priv->saved_register[i]);

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

module_platform_driver(lcdc_platform_driver);

MODULE_AUTHOR("Rob Clark <robdclark@gmail.com");
MODULE_DESCRIPTION("TI LCD Controller DRM Driver");
MODULE_LICENSE("GPL");

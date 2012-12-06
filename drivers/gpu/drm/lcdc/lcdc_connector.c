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

#include "lcdc_drv.h"

struct lcdc_connector {
	struct drm_connector base;

	struct drm_encoder *encoder;  /* our connected encoder */
};
#define to_lcdc_connector(x) container_of(x, struct lcdc_connector, base)


static void lcdc_connector_destroy(struct drm_connector *connector)
{
	drm_connector_cleanup(connector);
	// XXX
}

static enum drm_connector_status lcdc_connector_detect(
		struct drm_connector *connector,
		bool force)
{
	// XXX
	return connector_status_unknown;
}

static int lcdc_connector_get_modes(struct drm_connector *connector)
{
	// XXX
	return -1;
}

static int lcdc_connector_mode_valid(struct drm_connector *connector,
		  struct drm_display_mode *mode)
{
	// XXX
	return -1;
}

static struct drm_encoder *lcdc_connector_best_encoder(
		struct drm_connector *connector)
{
	struct lcdc_connector *lcdc_connector = to_lcdc_connector(connector);
	return lcdc_connector->encoder;
}

static const struct drm_connector_funcs lcdc_connector_funcs = {
	.destroy            = lcdc_connector_destroy,
	.dpms               = drm_helper_connector_dpms,
	.detect             = lcdc_connector_detect,
	.fill_modes         = drm_helper_probe_single_connector_modes,
};

static const struct drm_connector_helper_funcs lcdc_connector_helper_funcs = {
	.get_modes          = lcdc_connector_get_modes,
	.mode_valid         = lcdc_connector_mode_valid,
	.best_encoder       = lcdc_connector_best_encoder,
};

struct drm_connector *lcdc_connector_create(struct drm_device *dev,
		struct drm_encoder *encoder)
{
	struct lcdc_connector *lcdc_connector;
	struct drm_connector *connector;
	int ret;

	lcdc_connector = kzalloc(sizeof(*lcdc_connector), GFP_KERNEL);
	if (!lcdc_connector) {
		dev_err(dev->dev, "allocation failed\n");
		return NULL;
	}

	lcdc_connector->encoder = encoder;

	connector = &lcdc_connector->base;

	drm_connector_init(dev, connector, &lcdc_connector_funcs,
			DRM_MODE_CONNECTOR_LVDS);
	drm_connector_helper_add(connector, &lcdc_connector_helper_funcs);

	connector->polled = DRM_CONNECTOR_POLL_CONNECT |
			DRM_CONNECTOR_POLL_DISCONNECT;

	connector->interlace_allowed = 1;
	connector->doublescan_allowed = 0;

	ret = drm_mode_connector_attach_encoder(connector, encoder);
	if (ret)
		goto fail;

	drm_sysfs_connector_add(connector);

	return connector;

fail:
	lcdc_connector_destroy(connector);
	return NULL;
}

/*
 * linux/drivers/gpu/drm/omap/omap_gpu_priv.h
 *
 * Copyright (C) 2011 Texas Instruments
 * Author: Rob Clark <rob@ti.com>
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

#ifndef __OMAP_GPU_PRIV_H__
#define __OMAP_GPU_PRIV_H__

#include <plat/display.h>
#include <linux/module.h>

#define DBG(fmt,...) DRM_DEBUG(fmt"\n", ##__VA_ARGS__)
#define VERB(fmt,...) do { if (0) DBG(fmt, ##__VA_ARGS__); } while (0)

#define MODULE_NAME     "omap_gpu"

struct omap_gpu_private {
	int num_crtcs;
	struct drm_crtc *crtcs[8];
	int num_encoders;
	struct drm_encoder *encoders[8];
	int num_connectors;
	struct drm_connector *connectors[8];

	struct drm_fb_helper *fbdev;
};

struct drm_fb_helper * omap_fbdev_init(struct drm_device *dev);

struct drm_crtc * omap_crtc_init(struct drm_device *dev,
		struct omap_overlay *ovl, int id);
struct omap_overlay * omap_crtc_get_overlay(struct drm_crtc *crtc);

struct drm_encoder * omap_encoder_init(struct drm_device *dev,
		struct omap_overlay_manager *mgr);
struct omap_overlay_manager * omap_encoder_get_manager(
		struct drm_encoder *encoder);
struct drm_encoder * omap_connector_attached_encoder (
		struct drm_connector *connector);
enum drm_connector_status omap_connector_detect(
		struct drm_connector *connector, bool force);

struct drm_connector * omap_connector_init(struct drm_device *dev,
		int connector_type, struct omap_dss_device *dssdev);
void omap_connector_mode_set(struct drm_connector *connector,
		struct drm_display_mode *mode);
void omap_connector_flush(struct drm_connector *connector,
		int x, int y, int w, int h);
void omap_connector_dpms(struct drm_connector *connector, int mode);

struct drm_framebuffer * omap_framebuffer_create(struct drm_device *dev,
		struct drm_file *file, struct drm_mode_fb_cmd *mode_cmd);

#endif /* __OMAP_GPU_PRIV_H__ */

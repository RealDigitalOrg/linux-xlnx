/*
 * Real Digital DRM HDMI encoder driver for Xilinx
 *
 *  Copyright (C) 2019 Xilinx, Inc.
 *
 *  Author: Rick Hoover (rphoover@hotmail.com)
 *
 * Portions of this driver have been leveraged from the xilinx_drm_dp.c,
 * Copyright (C) 2014 Xilinx, Inc.
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <drm/drmP.h>
#include <drm/drm_edid.h>
#include <drm/drm_crtc_helper.h>
#include <drm/drm_encoder_slave.h>

#include <linux/device.h>
#include <linux/i2c.h>
#include <linux/platform_device.h>

#include "xilinx_drm_drv.h"


#define DEF_PIXCLK	150000
#define DEF_MAX_HORZ	1920
#define DEF_MAX_VERT	1080
#define DEF_PREF_HORZ	1280
#define DEF_PREF_VERT	720

/**
 * struct rd_drm_hdmi_config - Configuration of HDMI parameters
 * @max_pclock: Maximum pixel clock rate in KHz
 * @max_horz_res: Maximum horizontal resolution allowed
 * @max_vert_res: Maximum vertical resolution allowed
 * @pref_horz_res: Preferred horizontal resolution
 * @pref_vert_res: Preferred vertical resolution
 */
struct rd_drm_hdmi_config {
	u32 max_pclock;
	u32 max_horz_res;
	u32 max_vert_res;
	u32 pref_horz_res;
	u32 pref_vert_res;
};

/**
 * struct rd_drm_hdmi - Real Digital HDMI encoder core
 * @encoder: pointer to the drm encoder structure
 * @dev: device structure
 * @config: IP core configuration from DTS
 * @i2c_hdmi: I2C bus for HDMI EDID
 */
struct rd_drm_hdmi {
	struct drm_encoder *encoder;
	struct device *dev;
	struct rd_drm_hdmi_config config;
	struct i2c_adapter *i2c_hdmi;
};

static inline struct rd_drm_hdmi *to_dp(struct drm_encoder *encoder)
{
	return to_encoder_slave(encoder)->slave_priv;
}


static void rd_drm_hdmi_dpms(struct drm_encoder *encoder, int dpms)
{
	/* no op */
}

static void rd_drm_hdmi_save(struct drm_encoder *encoder)
{
	/* no op */
}

static void rd_drm_hdmi_restore(struct drm_encoder *encoder)
{
	/* no op */
}

static bool rd_drm_hdmi_mode_fixup(struct drm_encoder *encoder,
				   const struct drm_display_mode *mode,
				   struct drm_display_mode *adjusted_mode)
{
	return true;
}

static int rd_drm_hdmi_mode_valid(struct drm_encoder *encoder,
				  struct drm_display_mode *mode)
{
	struct rd_drm_hdmi *dp = to_dp(encoder);
	u32 max_pclock = dp->config.max_pclock;

	if (mode == NULL)
		return MODE_BAD;

	if (mode->clock > max_pclock)
		return MODE_CLOCK_HIGH;

	if ((mode->hdisplay > dp->config.max_horz_res) ||
	    (mode->vdisplay > dp->config.max_vert_res))
		return MODE_PANEL;

	if (mode->flags & DRM_MODE_FLAG_INTERLACE)
		return MODE_NO_INTERLACE;
	if (mode->flags & (DRM_MODE_FLAG_DBLCLK | DRM_MODE_FLAG_3D_MASK))
		return MODE_BAD;

	return MODE_OK;
}

static void rd_drm_hdmi_mode_set(struct drm_encoder *encoder,
				 struct drm_display_mode *mode,
				 struct drm_display_mode *adjusted_mode)
{
	/* no op */
}

static enum drm_connector_status
rd_drm_hdmi_detect(struct drm_encoder *encoder, struct drm_connector *connector)
{
	struct rd_drm_hdmi *dp = to_dp(encoder);

	if (dp->i2c_hdmi != NULL) {
		if (drm_probe_ddc(dp->i2c_hdmi) != 0)
			return connector_status_connected;
		else
			return connector_status_disconnected;
	}
	else
		return connector_status_unknown;
}

static int rd_drm_hdmi_get_modes(struct drm_encoder *encoder,
				 struct drm_connector *connector)
{
	struct rd_drm_hdmi *dp = to_dp(encoder);
	struct rd_drm_hdmi_config *config = &dp->config;
	struct edid *edid;
	int ret;

	if (dp->i2c_hdmi != NULL) {
		edid = drm_get_edid(connector, dp->i2c_hdmi);
		if (!edid)
			return 0;

		drm_mode_connector_update_edid_property(connector, edid);
		ret = drm_add_edid_modes(connector, edid);

		kfree(edid);
	}
	else {
		ret = drm_add_modes_noedid(connector, config->max_horz_res,
					   config->max_vert_res);
		drm_set_preferred_mode(connector, config->pref_horz_res,
				       config->pref_vert_res);
	}

	return ret;
}

static struct drm_encoder_slave_funcs rd_drm_hdmi_encoder_funcs = {
	.dpms			= rd_drm_hdmi_dpms,
	.save			= rd_drm_hdmi_save,
	.restore		= rd_drm_hdmi_restore,
	.mode_fixup		= rd_drm_hdmi_mode_fixup,
	.mode_valid		= rd_drm_hdmi_mode_valid,
	.mode_set		= rd_drm_hdmi_mode_set,
	.detect			= rd_drm_hdmi_detect,
	.get_modes		= rd_drm_hdmi_get_modes,
};

/**
 * rd_of_read_u32 - Fetch a parameter from the device tree
 * @node: Node in the device tree
 * @param: Parameter to read from node
 * @def: Default value if not found in the device tree
 *
 * Fetch a u32 parameter from the device tree.
 */
static u32 rd_of_read_u32 (struct device_node *node, char *param, u32 def)
{
	int ret;
	u32 val;
	char rd_param[40] = "realdigital,";

	strcat (rd_param, param);
	ret = of_property_read_u32(node, rd_param, &val);
	if (ret < 0) {
		val = def;
		DRM_INFO("No value for '%s', using default: %d\n",
			 rd_param, def);
	}
	return val;
}

/**
 * rd_drm_hdmi_parse_of - Fetch parameters from the device tree
 * @dp: DisplayPort IP core structure
 *
 * Fetch the following parameters from the device tree:
 * 	i2c-edid      - I2C edid interface to HDMI monitor
 *	max-pclock    - Maximum rate of the pixel clock in KHz
 *	max-horz-res  - Maximum horizontal resolution allowed
 *	max-vert-res  - Maximum vertical resolution allowed
 *	pref-horz-res - Preferred horizontal resolution
 *	pref-vert-res - Preferred vertical resolution
 */
static int rd_drm_hdmi_parse_of(struct rd_drm_hdmi *dp)
{
	struct device_node *node = dp->dev->of_node;
	struct device_node *i2c_node;
	struct rd_drm_hdmi_config *config = &dp->config;

	i2c_node = of_parse_phandle(node, "realdigital,i2c-edid", 0);
	if (i2c_node) {
		dp->i2c_hdmi = of_find_i2c_adapter_by_node(i2c_node);
		if (dp->i2c_hdmi == NULL)
			DRM_INFO("HDMI I2C interface not found, default modes will be used\n");
		of_node_put(i2c_node);
	}

	/* Attempt to read in maximum and preferred screen resolution along
           with the maximum pixel clock frequency */
	config->max_pclock = rd_of_read_u32 (node, "max-pclock", DEF_PIXCLK);
	config->max_horz_res = rd_of_read_u32 (node, "max-horz-res",
					       DEF_MAX_HORZ);
	config->max_vert_res = rd_of_read_u32 (node, "max-vert-res",
					       DEF_MAX_VERT);
	config->pref_horz_res = rd_of_read_u32 (node, "pref-horz-res",
					       DEF_PREF_HORZ);
	config->pref_vert_res = rd_of_read_u32 (node, "pref-vert-res",
					       DEF_PREF_VERT);

	return 0;
}

static int rd_drm_hdmi_encoder_init(struct platform_device *pdev,
				    struct drm_device *dev,
				    struct drm_encoder_slave *encoder)
{
	struct rd_drm_hdmi *dp = platform_get_drvdata(pdev);

	encoder->slave_priv = dp;
	encoder->slave_funcs = &rd_drm_hdmi_encoder_funcs;

	dp->encoder = &encoder->base;

	return rd_drm_hdmi_parse_of (dp);
}

static int __maybe_unused rd_drm_hdmi_pm_suspend(struct device *dev)
{
	//struct rd_drm_hdmi *dp = dev_get_drvdata(dev);

	return 0;
}

static int __maybe_unused rd_drm_hdmi_pm_resume(struct device *dev)
{
	struct rd_drm_hdmi *dp = dev_get_drvdata(dev);

	drm_helper_hpd_irq_event(dp->encoder->dev);

	return 0;
}

static const struct dev_pm_ops rd_drm_hdmi_pm_ops = {
	SET_SYSTEM_SLEEP_PM_OPS(rd_drm_hdmi_pm_suspend,
				rd_drm_hdmi_pm_resume)
};

static int rd_drm_hdmi_probe(struct platform_device *pdev)
{
	struct rd_drm_hdmi *dp;

	dp = devm_kzalloc(&pdev->dev, sizeof(*dp), GFP_KERNEL);
	if (dp == NULL)
		return -ENOMEM;

	dp->dev = &pdev->dev;
	platform_set_drvdata(pdev, dp);

	return 0;
}

static int rd_drm_hdmi_remove(struct platform_device *pdev)
{
	return 0;
}

static const struct of_device_id rd_drm_hdmi_of_match[] = {
	{ .compatible = "realdigital,drm-encoder-hdmi", },
	{ /* end of table */ },
};
MODULE_DEVICE_TABLE(of, rd_drm_hdmi_of_match);

static struct drm_platform_encoder_driver rd_drm_hdmi_driver = {
	.platform_driver = {
		.probe			= rd_drm_hdmi_probe,
		.remove			= rd_drm_hdmi_remove,
		.driver			= {
			.owner		= THIS_MODULE,
			.name		= "realdigital-drm-hdmi",
			.of_match_table	= rd_drm_hdmi_of_match,
			.pm		= &rd_drm_hdmi_pm_ops,
		},
	},

	.encoder_init = rd_drm_hdmi_encoder_init,
};

static int __init rd_drm_hdmi_init(void)
{
	return platform_driver_register(&rd_drm_hdmi_driver.platform_driver);
}

static void __exit rd_drm_hdmi_exit(void)
{
	platform_driver_unregister(&rd_drm_hdmi_driver.platform_driver);
}

module_init(rd_drm_hdmi_init);
module_exit(rd_drm_hdmi_exit);

MODULE_AUTHOR("Real Digital, LLC");
MODULE_DESCRIPTION("Real Digital DRM KMS HDMI Encoder Driver");
MODULE_LICENSE("GPL v2");

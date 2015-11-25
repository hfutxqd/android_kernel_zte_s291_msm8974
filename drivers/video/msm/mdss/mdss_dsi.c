/* Copyright (c) 2012-2013, The Linux Foundation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#include <linux/module.h>
#include <linux/interrupt.h>
#include <linux/spinlock.h>
#include <linux/delay.h>
#include <linux/io.h>
#include <linux/of_device.h>
#include <linux/of_gpio.h>
#include <linux/gpio.h>
#include <linux/err.h>
#include <linux/regulator/consumer.h>
/** ZTE_MODIFY chenfei added for LCD driver 2013/05/21 */ 
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <asm/uaccess.h>
/** ZTE_MODIFY end chenfei */ 

#include "mdss.h"
#include "mdss_panel.h"
#include "mdss_dsi.h"
#include "mdss_debug.h"

static unsigned char *mdss_dsi_base;

static int mdss_dsi_regulator_init(struct platform_device *pdev)
{
	struct mdss_dsi_ctrl_pdata *ctrl_pdata = NULL;

	if (!pdev) {
		pr_err("%s: invalid input\n", __func__);
		return -EINVAL;
	}

	ctrl_pdata = platform_get_drvdata(pdev);
	if (!ctrl_pdata) {
		pr_err("%s: invalid driver data\n", __func__);
		return -EINVAL;
	}

	return msm_dss_config_vreg(&pdev->dev,
			ctrl_pdata->power_data.vreg_config,
			ctrl_pdata->power_data.num_vreg, 1);
}

static int mdss_dsi_panel_power_on(struct mdss_panel_data *pdata, int enable)
{
	int ret;
	struct mdss_dsi_ctrl_pdata *ctrl_pdata = NULL;

	if (pdata == NULL) {
		pr_err("%s: Invalid input data\n", __func__);
		ret = -EINVAL;
		goto error;
	}

	ctrl_pdata = container_of(pdata, struct mdss_dsi_ctrl_pdata,
				panel_data);
	pr_debug("%s: enable=%d\n", __func__, enable);

	if (enable) {
		ret = msm_dss_enable_vreg(
			ctrl_pdata->power_data.vreg_config,
			ctrl_pdata->power_data.num_vreg, 1);
		if (ret) {
			pr_err("%s:Failed to enable vregs.rc=%d\n",
				__func__, ret);
			goto error;
		}
		/** ZTE_MODIFY chenfei added lcd driver 2013/05/21 */ 
		#if 1
		if (gpio_is_valid(ctrl_pdata->disp_en_gpio))
		{
			pr_info("%s disp_en_gpio on\n", __func__);
			gpio_direction_output(ctrl_pdata->disp_en_gpio, 1);
			//gpio_set_value((ctrl_pdata->disp_en_gpio), 1);
		}
		msleep(21);
		#endif
		
		//if (pdata->panel_info.panel_power_on == 0)
		//	mdss_dsi_panel_reset(pdata, 1);
		/** ZTE_MODIFY end chenfei */ 

	} else {

		mdss_dsi_panel_reset(pdata, 0);

		/** ZTE_MODIFY chenfei added lcd driver 2013/05/21 */ 
		#if 1
		msleep(5);
		if (gpio_is_valid(ctrl_pdata->disp_en_gpio))
		{
			pr_info("%s disp_en_gpio off\n", __func__);

			gpio_direction_output(ctrl_pdata->disp_en_gpio, 0);
			//gpio_set_value((ctrl_pdata->disp_en_gpio), 0);
		}
		#endif

		msleep(5);
		/** ZTE_MODIFY end chenfei */ 
		ret = msm_dss_enable_vreg(
			ctrl_pdata->power_data.vreg_config,
			ctrl_pdata->power_data.num_vreg, 0);
		if (ret) {
			pr_err("%s: Failed to disable vregs.rc=%d\n",
				__func__, ret);
		}
	}
error:
	return ret;
}

static void mdss_dsi_put_dt_vreg_data(struct device *dev,
	struct dss_module_power *module_power)
{
	if (!module_power) {
		pr_err("%s: invalid input\n", __func__);
		return;
	}

	if (module_power->vreg_config) {
		devm_kfree(dev, module_power->vreg_config);
		module_power->vreg_config = NULL;
	}
	module_power->num_vreg = 0;
}

static int mdss_dsi_get_dt_vreg_data(struct device *dev,
	struct dss_module_power *mp)
{
	int i = 0, rc = 0;
	u32 tmp = 0;
	struct device_node *of_node = NULL, *supply_node = NULL;

	if (!dev || !mp) {
		pr_err("%s: invalid input\n", __func__);
		rc = -EINVAL;
		goto error;
	}

	of_node = dev->of_node;

	mp->num_vreg = 0;
	for_each_child_of_node(of_node, supply_node) {
		if (!strncmp(supply_node->name, "qcom,platform-supply-entry",
						26))
			++mp->num_vreg;
	}
	if (mp->num_vreg == 0) {
		pr_debug("%s: no vreg\n", __func__);
		goto novreg;
	} else {
		pr_debug("%s: vreg found. count=%d\n", __func__, mp->num_vreg);
	}

	mp->vreg_config = devm_kzalloc(dev, sizeof(struct dss_vreg) *
		mp->num_vreg, GFP_KERNEL);
	if (!mp->vreg_config) {
		pr_err("%s: can't alloc vreg mem\n", __func__);
		rc = -ENOMEM;
		goto error;
	}

	for_each_child_of_node(of_node, supply_node) {
		if (!strncmp(supply_node->name, "qcom,platform-supply-entry",
						26)) {
			const char *st = NULL;
			/* vreg-name */
			rc = of_property_read_string(supply_node,
				"qcom,supply-name", &st);
			if (rc) {
				pr_err("%s: error reading name. rc=%d\n",
					__func__, rc);
				goto error;
			}
			snprintf(mp->vreg_config[i].vreg_name,
				ARRAY_SIZE((mp->vreg_config[i].vreg_name)),
				"%s", st);
			/* vreg-min-voltage */
			rc = of_property_read_u32(supply_node,
				"qcom,supply-min-voltage", &tmp);
			if (rc) {
				pr_err("%s: error reading min volt. rc=%d\n",
					__func__, rc);
				goto error;
			}
			mp->vreg_config[i].min_voltage = tmp;

			/* vreg-max-voltage */
			rc = of_property_read_u32(supply_node,
				"qcom,supply-max-voltage", &tmp);
			if (rc) {
				pr_err("%s: error reading max volt. rc=%d\n",
					__func__, rc);
				goto error;
			}
			mp->vreg_config[i].max_voltage = tmp;

			/* enable-load */
			rc = of_property_read_u32(supply_node,
				"qcom,supply-enable-load", &tmp);
			if (rc) {
				pr_err("%s: error reading enable load. rc=%d\n",
					__func__, rc);
				goto error;
			}
			mp->vreg_config[i].enable_load = tmp;

			/* disable-load */
			rc = of_property_read_u32(supply_node,
				"qcom,supply-disable-load", &tmp);
			if (rc) {
				pr_err("%s: error reading disable load. rc=%d\n",
					__func__, rc);
				goto error;
			}
			mp->vreg_config[i].disable_load = tmp;

			/* pre-sleep */
			rc = of_property_read_u32(supply_node,
				"qcom,supply-pre-on-sleep", &tmp);
			if (rc) {
				pr_debug("%s: error reading supply pre sleep value. rc=%d\n",
					__func__, rc);
			}
			mp->vreg_config[i].pre_on_sleep = (!rc ? tmp : 0);

			rc = of_property_read_u32(supply_node,
				"qcom,supply-pre-off-sleep", &tmp);
			if (rc) {
				pr_debug("%s: error reading supply pre sleep value. rc=%d\n",
					__func__, rc);
			}
			mp->vreg_config[i].pre_off_sleep = (!rc ? tmp : 0);

			/* post-sleep */
			rc = of_property_read_u32(supply_node,
				"qcom,supply-post-on-sleep", &tmp);
			if (rc) {
				pr_debug("%s: error reading supply post sleep value. rc=%d\n",
					__func__, rc);
			}
			mp->vreg_config[i].post_on_sleep = (!rc ? tmp : 0);

			rc = of_property_read_u32(supply_node,
				"qcom,supply-post-off-sleep", &tmp);
			if (rc) {
				pr_debug("%s: error reading supply post sleep value. rc=%d\n",
					__func__, rc);
			}
			mp->vreg_config[i].post_off_sleep = (!rc ? tmp : 0);

			pr_debug("%s: %s min=%d, max=%d, enable=%d, disable=%d, preonsleep=%d, postonsleep=%d, preoffsleep=%d, postoffsleep=%d\n",
				__func__,
				mp->vreg_config[i].vreg_name,
				mp->vreg_config[i].min_voltage,
				mp->vreg_config[i].max_voltage,
				mp->vreg_config[i].enable_load,
				mp->vreg_config[i].disable_load,
				mp->vreg_config[i].pre_on_sleep,
				mp->vreg_config[i].post_on_sleep,
				mp->vreg_config[i].pre_off_sleep,
				mp->vreg_config[i].post_off_sleep
				);
			++i;
		}
	}

	return rc;

error:
	if (mp->vreg_config) {
		devm_kfree(dev, mp->vreg_config);
		mp->vreg_config = NULL;
	}
novreg:
	mp->num_vreg = 0;

	return rc;
}

static int mdss_dsi_get_panel_cfg(char *panel_cfg)
{
	int rc;
	struct mdss_panel_cfg *pan_cfg = NULL;

	if (!panel_cfg)
		return MDSS_PANEL_INTF_INVALID;

	pan_cfg = mdss_panel_intf_type(MDSS_PANEL_INTF_DSI);
	if (IS_ERR(pan_cfg)) {
		return PTR_ERR(pan_cfg);
	} else if (!pan_cfg) {
		panel_cfg[0] = 0;
		return 0;
	}

	pr_debug("%s:%d: cfg:[%s]\n", __func__, __LINE__,
		 pan_cfg->arg_cfg);
	rc = strlcpy(panel_cfg, pan_cfg->arg_cfg,
		     sizeof(pan_cfg->arg_cfg));
	return rc;
}

static int mdss_dsi_off(struct mdss_panel_data *pdata)
{
	int ret = 0;
	struct mdss_dsi_ctrl_pdata *ctrl_pdata = NULL;
	struct mdss_panel_info *panel_info = NULL;

	if (pdata == NULL) {
		pr_err("%s: Invalid input data\n", __func__);
		return -EINVAL;
	}

	if (!pdata->panel_info.panel_power_on) {
		pr_warn("%s:%d Panel already off.\n", __func__, __LINE__);
		return -EPERM;
	}

	pdata->panel_info.panel_power_on = 0;

	ctrl_pdata = container_of(pdata, struct mdss_dsi_ctrl_pdata,
				panel_data);

	panel_info = &ctrl_pdata->panel_data.panel_info;
	pr_debug("%s+: ctrl=%p ndx=%d\n", __func__,
				ctrl_pdata, ctrl_pdata->ndx);

	if (pdata->panel_info.type == MIPI_CMD_PANEL)
		mdss_dsi_clk_ctrl(ctrl_pdata, 1);

	/* disable DSI controller */
	mdss_dsi_controller_cfg(0, pdata);

	mdss_dsi_clk_ctrl(ctrl_pdata, 0);

	ret = mdss_dsi_enable_bus_clocks(ctrl_pdata);
	if (ret) {
		pr_err("%s: failed to enable bus clocks. rc=%d\n", __func__,
			ret);
		mdss_dsi_panel_power_on(pdata, 0);
		return ret;
	}

	/* disable DSI phy */
	mdss_dsi_phy_enable(ctrl_pdata, 0);

	mdss_dsi_disable_bus_clocks(ctrl_pdata);
	msleep(20);	/** ZTE_MODIFY chenfei added for LCD driver 2013/05/21 */ 

	ret = mdss_dsi_panel_power_on(pdata, 0);
	if (ret) {
		pr_err("%s: Panel power off failed\n", __func__);
		return ret;
	}

	if (panel_info->dynamic_fps
	    && (panel_info->dfps_update == DFPS_SUSPEND_RESUME_MODE)
	    && (panel_info->new_fps != panel_info->mipi.frame_rate))
		panel_info->mipi.frame_rate = panel_info->new_fps;

	pr_debug("%s-:\n", __func__);

	return ret;
}

int mdss_dsi_on(struct mdss_panel_data *pdata)
{
	int ret = 0;
	u32 clk_rate;
	struct mdss_panel_info *pinfo;
	struct mipi_panel_info *mipi;
	u32 hbp, hfp, vbp, vfp, hspw, vspw, width, height;
	u32 ystride, bpp, data, dst_bpp;
	u32 dummy_xres, dummy_yres;
	struct mdss_dsi_ctrl_pdata *ctrl_pdata = NULL;
	u32 hsync_period, vsync_period;

	if (pdata == NULL) {
		pr_err("%s: Invalid input data\n", __func__);
		return -EINVAL;
	}

	if (pdata->panel_info.panel_power_on) {
		pr_warn("%s:%d Panel already on.\n", __func__, __LINE__);
		return 0;
	}

	ctrl_pdata = container_of(pdata, struct mdss_dsi_ctrl_pdata,
				panel_data);

	/** ZTE_MODIFY chenfei added for LCD driver 2013/05/21 */ 
	pr_info("%s+: ctrl=%p ndx=%d\n",
				__func__, ctrl_pdata, ctrl_pdata->ndx);
	/** ZTE_MODIFY end chenfei */ 
	
	pinfo = &pdata->panel_info;

	ret = mdss_dsi_panel_power_on(pdata, 1);
	if (ret) {
		pr_err("%s: Panel power on failed\n", __func__);
		return ret;
	}

	pdata->panel_info.panel_power_on = 1;

	ret = mdss_dsi_enable_bus_clocks(ctrl_pdata);
	if (ret) {
		pr_err("%s: failed to enable bus clocks. rc=%d\n", __func__,
			ret);
		mdss_dsi_panel_power_on(pdata, 0);
		return ret;
	}

	mdss_dsi_phy_sw_reset((ctrl_pdata->ctrl_base));
	mdss_dsi_phy_init(pdata);
	mdss_dsi_disable_bus_clocks(ctrl_pdata);

	mdss_dsi_clk_ctrl(ctrl_pdata, 1);

	clk_rate = pdata->panel_info.clk_rate;
	clk_rate = min(clk_rate, pdata->panel_info.clk_max);

	dst_bpp = pdata->panel_info.fbc.enabled ?
		(pdata->panel_info.fbc.target_bpp) : (pinfo->bpp);

	hbp = mult_frac(pdata->panel_info.lcdc.h_back_porch, dst_bpp,
			pdata->panel_info.bpp);
	hfp = mult_frac(pdata->panel_info.lcdc.h_front_porch, dst_bpp,
			pdata->panel_info.bpp);
	vbp = mult_frac(pdata->panel_info.lcdc.v_back_porch, dst_bpp,
			pdata->panel_info.bpp);
	vfp = mult_frac(pdata->panel_info.lcdc.v_front_porch, dst_bpp,
			pdata->panel_info.bpp);
	hspw = mult_frac(pdata->panel_info.lcdc.h_pulse_width, dst_bpp,
			pdata->panel_info.bpp);
	vspw = pdata->panel_info.lcdc.v_pulse_width;
	width = mult_frac(pdata->panel_info.xres, dst_bpp,
			pdata->panel_info.bpp);
	height = pdata->panel_info.yres;

	if (pdata->panel_info.type == MIPI_VIDEO_PANEL) {
		dummy_xres = pdata->panel_info.lcdc.xres_pad;
		dummy_yres = pdata->panel_info.lcdc.yres_pad;
	}

	vsync_period = vspw + vbp + height + dummy_yres + vfp;
	hsync_period = hspw + hbp + width + dummy_xres + hfp;

	mipi  = &pdata->panel_info.mipi;
	if (pdata->panel_info.type == MIPI_VIDEO_PANEL) {
		MIPI_OUTP((ctrl_pdata->ctrl_base) + 0x24,
			((hspw + hbp + width + dummy_xres) << 16 |
			(hspw + hbp)));
		MIPI_OUTP((ctrl_pdata->ctrl_base) + 0x28,
			((vspw + vbp + height + dummy_yres) << 16 |
			(vspw + vbp)));
		MIPI_OUTP((ctrl_pdata->ctrl_base) + 0x2C,
				((vsync_period - 1) << 16)
				| (hsync_period - 1));

		MIPI_OUTP((ctrl_pdata->ctrl_base) + 0x30, (hspw << 16));
		MIPI_OUTP((ctrl_pdata->ctrl_base) + 0x34, 0);
		MIPI_OUTP((ctrl_pdata->ctrl_base) + 0x38, (vspw << 16));

	} else {		/* command mode */
		if (mipi->dst_format == DSI_CMD_DST_FORMAT_RGB888)
			bpp = 3;
		else if (mipi->dst_format == DSI_CMD_DST_FORMAT_RGB666)
			bpp = 3;
		else if (mipi->dst_format == DSI_CMD_DST_FORMAT_RGB565)
			bpp = 2;
		else
			bpp = 3;	/* Default format set to RGB888 */

		ystride = width * bpp + 1;

		/* DSI_COMMAND_MODE_MDP_STREAM_CTRL */
		data = (ystride << 16) | (mipi->vc << 8) | DTYPE_DCS_LWRITE;
		MIPI_OUTP((ctrl_pdata->ctrl_base) + 0x60, data);
		MIPI_OUTP((ctrl_pdata->ctrl_base) + 0x58, data);

		/* DSI_COMMAND_MODE_MDP_STREAM_TOTAL */
		data = height << 16 | width;
		MIPI_OUTP((ctrl_pdata->ctrl_base) + 0x64, data);
		MIPI_OUTP((ctrl_pdata->ctrl_base) + 0x5C, data);
	}

	mdss_dsi_sw_reset(pdata);
	mdss_dsi_host_init(mipi, pdata);

	
	mdss_dsi_panel_reset(pdata, 1);	/** ZTE_MODIFY chenfei added for LCD driver 2013/05/21 */ 


	if (mipi->force_clk_lane_hs) {
		u32 tmp;

		tmp = MIPI_INP((ctrl_pdata->ctrl_base) + 0xac);
		tmp |= (1<<28);
		MIPI_OUTP((ctrl_pdata->ctrl_base) + 0xac, tmp);
		wmb();
	}

	if (pdata->panel_info.type == MIPI_CMD_PANEL)
		mdss_dsi_clk_ctrl(ctrl_pdata, 0);

	pr_debug("%s-:\n", __func__);
	return 0;
}

static int mdss_dsi_unblank(struct mdss_panel_data *pdata)
{
	int ret = 0;
	struct mipi_panel_info *mipi;
	struct mdss_dsi_ctrl_pdata *ctrl_pdata = NULL;

	pr_debug("%s+:\n", __func__);

	if (pdata == NULL) {
		pr_err("%s: Invalid input data\n", __func__);
		return -EINVAL;
	}

	ctrl_pdata = container_of(pdata, struct mdss_dsi_ctrl_pdata,
				panel_data);
	mipi  = &pdata->panel_info.mipi;

	if (!(ctrl_pdata->ctrl_state & CTRL_STATE_PANEL_INIT)) {
		ret = ctrl_pdata->on(pdata);
		if (ret) {
			pr_err("%s: unable to initialize the panel\n",
							__func__);
			return ret;
		}
		ctrl_pdata->ctrl_state |= CTRL_STATE_PANEL_INIT;
	}

	if (pdata->panel_info.type == MIPI_CMD_PANEL) {
		if (mipi->vsync_enable && mipi->hw_vsync_mode
			&& gpio_is_valid(ctrl_pdata->disp_te_gpio)) {
				mdss_dsi_set_tear_on(ctrl_pdata);
		}
	}

	pr_debug("%s-:\n", __func__);

	return ret;
}

static int mdss_dsi_blank(struct mdss_panel_data *pdata)
{
	int ret = 0;
	struct mipi_panel_info *mipi;
	struct mdss_dsi_ctrl_pdata *ctrl_pdata = NULL;

	pr_debug("%s+:\n", __func__);

	if (pdata == NULL) {
		pr_err("%s: Invalid input data\n", __func__);
		return -EINVAL;
	}

	ctrl_pdata = container_of(pdata, struct mdss_dsi_ctrl_pdata,
				panel_data);
	mipi = &pdata->panel_info.mipi;

	mdss_dsi_op_mode_config(DSI_CMD_MODE, pdata);

	if (pdata->panel_info.type == MIPI_CMD_PANEL) {
		if (mipi->vsync_enable && mipi->hw_vsync_mode
			&& gpio_is_valid(ctrl_pdata->disp_te_gpio)) {
			mdss_dsi_set_tear_off(ctrl_pdata);
		}
	}

	if (ctrl_pdata->ctrl_state & CTRL_STATE_PANEL_INIT) {
		ret = ctrl_pdata->off(pdata);
		if (ret) {
			pr_err("%s: Panel OFF failed\n", __func__);
			return ret;
		}
		ctrl_pdata->ctrl_state &= ~CTRL_STATE_PANEL_INIT;
	}
	pr_debug("%s-:End\n", __func__);
	return ret;
}

int mdss_dsi_cont_splash_on(struct mdss_panel_data *pdata)
{
	int ret = 0;
	struct mipi_panel_info *mipi;
	struct mdss_dsi_ctrl_pdata *ctrl_pdata = NULL;

	pr_info("%s:%d DSI on for continuous splash.\n", __func__, __LINE__);

	if (pdata == NULL) {
		pr_err("%s: Invalid input data\n", __func__);
		return -EINVAL;
	}

	mipi = &pdata->panel_info.mipi;

	ctrl_pdata = container_of(pdata, struct mdss_dsi_ctrl_pdata,
				panel_data);

	pr_debug("%s+: ctrl=%p ndx=%d\n", __func__,
				ctrl_pdata, ctrl_pdata->ndx);

	WARN((ctrl_pdata->ctrl_state & CTRL_STATE_PANEL_INIT),
		"Incorrect Ctrl state=0x%x\n", ctrl_pdata->ctrl_state);

	mdss_dsi_sw_reset(pdata);
	mdss_dsi_host_init(mipi, pdata);
	mdss_dsi_op_mode_config(mipi->mode, pdata);

	if (ctrl_pdata->on_cmds.link_state == DSI_LP_MODE) {
		ret = mdss_dsi_unblank(pdata);
		if (ret) {
			pr_err("%s: unblank failed\n", __func__);
			return ret;
		}
	}

	pr_debug("%s-:End\n", __func__);
	return ret;
}

static int mdss_dsi_dfps_config(struct mdss_panel_data *pdata, int new_fps)
{
	int rc = 0;
	struct mdss_dsi_ctrl_pdata *ctrl_pdata = NULL;
	u32 dsi_ctrl;

	pr_debug("%s+:\n", __func__);

	if (pdata == NULL) {
		pr_err("%s: Invalid input data\n", __func__);
		return -EINVAL;
	}

	ctrl_pdata = container_of(pdata, struct mdss_dsi_ctrl_pdata,
				panel_data);

	if (!ctrl_pdata->panel_data.panel_info.dynamic_fps) {
		pr_err("%s: Dynamic fps not enabled for this panel\n",
					__func__);
		return -EINVAL;
	}

	if (new_fps !=
		ctrl_pdata->panel_data.panel_info.mipi.frame_rate) {
		rc = mdss_dsi_clk_div_config
			(&ctrl_pdata->panel_data.panel_info, new_fps);
		if (rc) {
			pr_err("%s: unable to initialize the clk dividers\n",
							__func__);
			return rc;
		}
		ctrl_pdata->pclk_rate =
			ctrl_pdata->panel_data.panel_info.mipi.dsi_pclk_rate;
		ctrl_pdata->byte_clk_rate =
			ctrl_pdata->panel_data.panel_info.clk_rate / 8;

		if (pdata->panel_info.dfps_update
				== DFPS_IMMEDIATE_CLK_UPDATE_MODE) {
			dsi_ctrl = MIPI_INP((ctrl_pdata->ctrl_base) +
					    0x0004);
			ctrl_pdata->panel_data.panel_info.mipi.frame_rate =
									new_fps;
			dsi_ctrl &= ~0x2;
			MIPI_OUTP((ctrl_pdata->ctrl_base) + 0x0004,
							dsi_ctrl);
			mdss_dsi_controller_cfg(true, pdata);
			mdss_dsi_clk_ctrl(ctrl_pdata, 0);
			mdss_dsi_clk_ctrl(ctrl_pdata, 1);
			dsi_ctrl |= 0x2;
			MIPI_OUTP((ctrl_pdata->ctrl_base) + 0x0004,
							dsi_ctrl);
		}
	} else {
		pr_debug("%s: Panel is already at this FPS\n", __func__);
	}

	return rc;
}

static int mdss_dsi_ctl_partial_update(struct mdss_panel_data *pdata)
{
	int rc = -EINVAL;
	u32 data;
	struct mdss_dsi_ctrl_pdata *ctrl_pdata = NULL;

	if (pdata == NULL) {
		pr_err("%s: Invalid input data\n", __func__);
		return -EINVAL;
	}

	ctrl_pdata = container_of(pdata, struct mdss_dsi_ctrl_pdata,
				panel_data);

	/* DSI_COMMAND_MODE_MDP_STREAM_CTRL */
	data = (((pdata->panel_info.roi_w * 3) + 1) << 16) |
			(pdata->panel_info.mipi.vc << 8) | DTYPE_DCS_LWRITE;
	MIPI_OUTP((ctrl_pdata->ctrl_base) + 0x60, data);
	MIPI_OUTP((ctrl_pdata->ctrl_base) + 0x58, data);

	/* DSI_COMMAND_MODE_MDP_STREAM_TOTAL */
	data = pdata->panel_info.roi_h << 16 | pdata->panel_info.roi_w;
	MIPI_OUTP((ctrl_pdata->ctrl_base) + 0x64, data);
	MIPI_OUTP((ctrl_pdata->ctrl_base) + 0x5C, data);

	if (ctrl_pdata->partial_update_fnc)
		rc = ctrl_pdata->partial_update_fnc(pdata);

	if (rc) {
		pr_err("%s: unable to initialize the panel\n",
				__func__);
		return rc;
	}

	return rc;
}

/** ZTE_MODIFY niugang10089953 modify continus display **/
void mdss_dsi_panel_bklt_dcs_first(struct mdss_dsi_ctrl_pdata *ctrl, int level);
/** ZTE_MODIFY end **/

static int mdss_dsi_event_handler(struct mdss_panel_data *pdata,
				  int event, void *arg)
{
	int rc = 0;
	struct mdss_dsi_ctrl_pdata *ctrl_pdata = NULL;

	if (pdata == NULL) {
		pr_err("%s: Invalid input data\n", __func__);
		return -EINVAL;
	}
	ctrl_pdata = container_of(pdata, struct mdss_dsi_ctrl_pdata,
				panel_data);
	pr_debug("%s+:event=%d\n", __func__, event);

	switch (event) {
	case MDSS_EVENT_UNBLANK:
		rc = mdss_dsi_on(pdata);
		mdss_dsi_op_mode_config(pdata->panel_info.mipi.mode,
							pdata);
		if (ctrl_pdata->on_cmds.link_state == DSI_LP_MODE)
			rc = mdss_dsi_unblank(pdata);
		break;
	case MDSS_EVENT_PANEL_ON:
		ctrl_pdata->ctrl_state |= CTRL_STATE_MDP_ACTIVE;
		if (ctrl_pdata->on_cmds.link_state == DSI_HS_MODE)
			rc = mdss_dsi_unblank(pdata);
		break;
	case MDSS_EVENT_BLANK:
		if (ctrl_pdata->off_cmds.link_state == DSI_HS_MODE)
			rc = mdss_dsi_blank(pdata);
		break;
	case MDSS_EVENT_PANEL_OFF:
		ctrl_pdata->ctrl_state &= ~CTRL_STATE_MDP_ACTIVE;
		if (ctrl_pdata->off_cmds.link_state == DSI_LP_MODE)
			rc = mdss_dsi_blank(pdata);
		rc = mdss_dsi_off(pdata);
		break;
	case MDSS_EVENT_CONT_SPLASH_FINISH:
		/** ZTE_MODIFY niugang10089953 modify continus display **/
		//mdss_dsi_on(pdata);
		//mdss_dsi_panel_bklt_dcs_first(ctrl_pdata, 102);
		/** ZTE_MODIFY end **/
		ctrl_pdata->ctrl_state &= ~CTRL_STATE_MDP_ACTIVE;
		if (ctrl_pdata->on_cmds.link_state == DSI_LP_MODE) {
			rc = mdss_dsi_cont_splash_on(pdata);
		} else {
			pr_debug("%s:event=%d, Dsi On not called: ctrl_state: %d\n",
				 __func__, event,
				 ctrl_pdata->on_cmds.link_state);
			rc = -EINVAL;
		}
		break;
	case MDSS_EVENT_PANEL_CLK_CTRL:
		mdss_dsi_clk_req(ctrl_pdata, (int)arg);
		break;
	case MDSS_EVENT_DSI_CMDLIST_KOFF:
		ctrl_pdata->recovery = (struct mdss_panel_recovery *)arg;
		mdss_dsi_cmdlist_commit(ctrl_pdata, 1);
		break;
	case MDSS_EVENT_PANEL_UPDATE_FPS:
		if (arg != NULL) {
			rc = mdss_dsi_dfps_config(pdata, (int)arg);
			pr_debug("%s:update fps to = %d\n",
				__func__, (int)arg);
		}
		break;
	case MDSS_EVENT_CONT_SPLASH_BEGIN:
		/** ZTE_MODIFY niugang10089953 modify continus display **/
		mdss_dsi_panel_bklt_dcs_first(ctrl_pdata, 0);
		//mdss_dsi_off(pdata);
		/** ZTE_MODIFY end **/
		if (ctrl_pdata->off_cmds.link_state == DSI_HS_MODE) {
			/* Panel is Enabled in Bootloader */
			rc = mdss_dsi_blank(pdata);
		}
		break;
	case MDSS_EVENT_ENABLE_PARTIAL_UPDATE:
		rc = mdss_dsi_ctl_partial_update(pdata);
		break;
	default:
		pr_debug("%s: unhandled event=%d\n", __func__, event);
		break;
	}
	pr_debug("%s-:event=%d, rc=%d\n", __func__, event, rc);
	return rc;
}

/**
 * mdss_dsi_find_panel_of_node(): find device node of dsi panel
 * @pdev: platform_device of the dsi ctrl node
 * @panel_cfg: string containing intf specific config data
 *
 * Function finds the panel device node using the interface
 * specific configuration data. This configuration data is
 * could be derived from the result of bootloader's GCDB
 * panel detection mechanism. If such config data doesn't
 * exist then this panel returns the default panel configured
 * in the device tree.
 *
 * returns pointer to panel node on success, NULL on error.
 */
static struct device_node *mdss_dsi_find_panel_of_node(
		struct platform_device *pdev, char *panel_cfg)
{
	int l;
	int ctrl_id = -1;
	char *panel_name;
	struct device_node *dsi_pan_node = NULL, *mdss_node = NULL;

	l = strlen(panel_cfg);
	if (!l) {
		/* no panel cfg chg, parse dt */
		pr_debug("%s:%d: no cmd line cfg present\n",
			 __func__, __LINE__);
		dsi_pan_node = of_parse_phandle(
			pdev->dev.of_node,
			"qcom,dsi-pref-prim-pan", 0);
		if (!dsi_pan_node) {
			pr_err("%s:can't find panel phandle\n",
			       __func__);
			return NULL;
		}
	} else {
		if (panel_cfg[0] == '0') {
			pr_debug("%s:%d: DSI ctrl 1\n", __func__, __LINE__);
			ctrl_id = 0;
		} else if (panel_cfg[0] == '1') {
			pr_debug("%s:%d: DSI ctrl 2\n", __func__, __LINE__);
			ctrl_id = 1;
		}
		if ((pdev->id - 1) != ctrl_id) {
			pr_err("%s:%d:pdev_ID=[%d]\n",
			       __func__, __LINE__, pdev->id);
			return NULL;
		}
		/*
		 * skip first two chars '<dsi_ctrl_id>' and
		 * ':' to get to the panel name
		 */
		panel_name = panel_cfg + 2;
		pr_debug("%s:%d:%s:%s\n", __func__, __LINE__,
			 panel_cfg, panel_name);

		mdss_node = of_parse_phandle(pdev->dev.of_node,
					     "qcom,mdss-mdp", 0);

		if (!mdss_node) {
			pr_err("%s: %d: mdss_node null\n",
			       __func__, __LINE__);
			return NULL;
		}
		dsi_pan_node = of_find_node_by_name(mdss_node,
						    panel_name);
		if (!dsi_pan_node) {
			pr_err("%s: invalid pan node\n",
			       __func__);
			return NULL;
		}
	}
	return dsi_pan_node;
}

/** ZTE_MODIFY chenfei added for LCD driver 2013/05/21 */ 

//zhaochenwei start
#if 0
/* interface for exporting device attributes */
struct device_attribute {
	struct attribute	attr;
	ssize_t (*show)(struct device *dev, struct device_attribute *attr,
			char *buf);
	ssize_t (*store)(struct device *dev, struct device_attribute *attr,
			 const char *buf, size_t count);
};
#endif

enum{
	lcd_ce_OFF = 0,
	lcd_ce_LOW,
	lcd_ce_MIDDLE,
	lcd_ce_HIGH,
};

static int current_ce_mode = 0;
struct mdss_dsi_ctrl_pdata *g_ctrl_pdata = NULL;
extern int isyassy;
#define mdd_debug pr_info

static char r63311_ce_low[33] = {0xca, 0x01, 0x80, 0x96, 0x8c, 0x96, 0xa0, 0x96, 0xa0, 
							  0x0c, 0x0c, 0x00, 0x82, 0x0a, 0x4a, 0x37, 0xa0, 0x55, 
							  0xf8, 0x0c, 0x0c, 0x20, 0x10, 0x20, 0x20, 0x00, 0x00, 
							  0x10, 0x10, 0x3f, 0x3f, 0x3f, 0x3f};
static char r63311_ce_middle[33] ={0xca,0x01,0x80,0xaf,0xaa,0xaf,0xb9,0xaf,0xb9,0x0c,0x1e,
								0x00,0x8a,0x0a,0x4a,0x37,0xa0,0x00,0xff,0x0c,0x0c,0x20,
								0x10,0x3f,0x3f,0x00,0x00,0x10,0x10,0x3f,0x3f,0x3f,0x3f};
static char r63311_ce_high[33] = {0xca,0x01,0x80,0xcd,0xc8,0xcd,0xd7,0xcd,0xd7,0x12,0x24,
								0x00,0x96,0x0a,0x4a,0x37,0xa0,0x00,0xff,0x0c,0x0c,0x20,
								0x10,0x3f,0x3f,0x00,0x00,0x10,0x10,0x3f,0x3f,0x3f,0x3f};
								 
static char nt35596_ie_enable_cmd1[2] = {0xFF,0x00};
static char nt35596_ie_enable_cmd2[2] = {0x55,0x83};
//static char nt35596_ie_enable_cmd3[2] = {0x55,0x93};
//static char nt35596_ie_enable_cmd4[2] = {0x55,0xB3};

static char nt35596_ie_cmd01[2] = {0xFF,0x03};
static char nt35596_ie_cmd02[2] = {0xFB,0x01};
static char nt35596_ie_cmd00[2] = {0x00,0x00};

#if 1
//nt35596 ie low cmds
static char nt35596_ie_low_cmd1[2]  = { 0x01,0x04 };
static char nt35596_ie_low_cmd2[2]  = { 0x02,0x08 };
static char nt35596_ie_low_cmd3[2]  = { 0x03,0x0c };
static char nt35596_ie_low_cmd4[2]  = { 0x04,0x10 };
static char nt35596_ie_low_cmd5[2]  = { 0x05,0x14 };
static char nt35596_ie_low_cmd6[2]  = { 0x06,0x18 };
static char nt35596_ie_low_cmd7[2]  = { 0x07,0x20 };
static char nt35596_ie_low_cmd8[2]  = { 0x08,0x28 };
static char nt35596_ie_low_cmd9[2]  = { 0x09,0x30 };
static char nt35596_ie_low_cmd10[2] = { 0x0A,0x38 };
static char nt35596_ie_low_cmd11[2] = { 0x0B,0x44 };
static char nt35596_ie_low_cmd12[2] = { 0x0C,0x54 };
static char nt35596_ie_low_cmd13[2] = { 0x0D,0x64 };
static char nt35596_ie_low_cmd14[2] = { 0x0E,0x78 };
static char nt35596_ie_low_cmd15[2] = { 0x0F,0x90 };
static char nt35596_ie_low_cmd16[2] = { 0x10,0xAC };
static char nt35596_ie_low_cmd17[2] = { 0x11,0xD4 };
static char nt35596_ie_low_cmd18[2] = { 0x12,0xFC };
static char nt35596_ie_low_cmd19[2] = { 0x13,0xFC };
static char nt35596_ie_low_cmd20[2] = { 0x1B,0x00 };
static char nt35596_ie_low_cmd21[2] = { 0x1C,0x04 };
static char nt35596_ie_low_cmd22[2] = { 0x1D,0x08 };
static char nt35596_ie_low_cmd23[2] = { 0x1E,0x0C };
static char nt35596_ie_low_cmd24[2] = { 0x1F,0x10 };
static char nt35596_ie_low_cmd25[2] = { 0x20,0x14 };
static char nt35596_ie_low_cmd26[2] = { 0x21,0x18 };
static char nt35596_ie_low_cmd27[2] = { 0x22,0x20 };
static char nt35596_ie_low_cmd28[2] = { 0x23,0x18 };
static char nt35596_ie_low_cmd29[2] = { 0x24,0x10 };
static char nt35596_ie_low_cmd30[2] = { 0x25,0x08 };
static char nt35596_ie_low_cmd31[2] = { 0x26,0x00 };
static char nt35596_ie_low_cmd32[2] = { 0x27,0x00 };
static char nt35596_ie_low_cmd33[2] = { 0x28,0x00 };
static char nt35596_ie_low_cmd34[2] = { 0x29,0x00 };
static char nt35596_ie_low_cmd35[2] = { 0x2A,0x00 };
static char nt35596_ie_low_cmd36[2] = { 0x2B,0x00 };
static char nt35596_ie_low_cmd37[2] = { 0x2F,0x00 };
static char nt35596_ie_low_cmd38[2] = { 0x30,0x00 };
static char nt35596_ie_low_cmd39[2] = { 0x31,0x00 };
static char nt35596_ie_low_cmd40[2] = { 0x32,0x80 };
static char nt35596_ie_low_cmd41[2] = { 0x33,0x80 };
static char nt35596_ie_low_cmd42[2] = { 0x34,0x82 };
static char nt35596_ie_low_cmd43[2] = { 0x35,0x04 };
static char nt35596_ie_low_cmd44[2] = { 0x36,0x06 };
static char nt35596_ie_low_cmd45[2] = { 0x37,0x08 };
static char nt35596_ie_low_cmd46[2] = { 0x38,0x0A };
static char nt35596_ie_low_cmd47[2] = { 0x39,0x0C };
static char nt35596_ie_low_cmd48[2] = { 0x3A,0x0E };
static char nt35596_ie_low_cmd49[2] = { 0x3B,0x10 };
static char nt35596_ie_low_cmd50[2] = { 0x3F,0x14 };
static char nt35596_ie_low_cmd51[2] = { 0x40,0x18 };
static char nt35596_ie_low_cmd52[2] = { 0x41,0x18 };
static char nt35596_ie_low_cmd53[2] = { 0x42,0x18 };
static char nt35596_ie_low_cmd54[2] = { 0x43,0x18 };
static char nt35596_ie_low_cmd55[2] = { 0x44,0x14 };
static char nt35596_ie_low_cmd56[2] = { 0x45,0x10 };
static char nt35596_ie_low_cmd57[2] = { 0x46,0x0E };
static char nt35596_ie_low_cmd58[2] = { 0x47,0x0C };
static char nt35596_ie_low_cmd59[2] = { 0x48,0x0A };
static char nt35596_ie_low_cmd60[2] = { 0x49,0x08 };
static char nt35596_ie_low_cmd61[2] = { 0x4A,0x06 };
static char nt35596_ie_low_cmd62[2] = { 0x4B,0x04 };
static char nt35596_ie_low_cmd63[2] = { 0x4C,0x82 };
static char nt35596_ie_low_cmd64[2] = { 0x1A,0x00 };
static char nt35596_ie_low_cmd65[2] = { 0x53,0x01 };
static char nt35596_ie_low_cmd66[2] = { 0x54,0x00 };
static char nt35596_ie_low_cmd67[2] = { 0x55,0x00 };
static char nt35596_ie_low_cmd68[2] = { 0x5B,0x00 };
static char nt35596_ie_low_cmd69[2] = { 0x63,0x00 };
static char nt35596_ie_low_cmd70[2] = { 0x14,0x00 };
static char nt35596_ie_low_cmd71[2] = { 0x4D,0x08 };
static char nt35596_ie_low_cmd72[2] = { 0x58,0x00 };
static char nt35596_ie_low_cmd73[2] = { 0x5F,0x2F };
static char nt35596_ie_low_cmd74[2] = { 0x60,0x00 };
//nt35596 ie mid cmds
static char nt35596_ie_mid_cmd1[2]  = { 0x01,0x04 };
static char nt35596_ie_mid_cmd2[2]  = { 0x02,0x08 };
static char nt35596_ie_mid_cmd3[2]  = { 0x03,0x0c };
static char nt35596_ie_mid_cmd4[2]  = { 0x04,0x10 };
static char nt35596_ie_mid_cmd5[2]  = { 0x05,0x14 };
static char nt35596_ie_mid_cmd6[2]  = { 0x06,0x18 };
static char nt35596_ie_mid_cmd7[2]  = { 0x07,0x20 };
static char nt35596_ie_mid_cmd8[2]  = { 0x08,0x28 };
static char nt35596_ie_mid_cmd9[2]  = { 0x09,0x30 };
static char nt35596_ie_mid_cmd10[2] = { 0x0A,0x38 };
static char nt35596_ie_mid_cmd11[2] = { 0x0B,0x44 };
static char nt35596_ie_mid_cmd12[2] = { 0x0C,0x54 };
static char nt35596_ie_mid_cmd13[2] = { 0x0D,0x64 };
static char nt35596_ie_mid_cmd14[2] = { 0x0E,0x78 };
static char nt35596_ie_mid_cmd15[2] = { 0x0F,0x90 };
static char nt35596_ie_mid_cmd16[2] = { 0x10,0xAC };
static char nt35596_ie_mid_cmd17[2] = { 0x11,0xD4 };
static char nt35596_ie_mid_cmd18[2] = { 0x12,0xFC };
static char nt35596_ie_mid_cmd19[2] = { 0x13,0xFC };
static char nt35596_ie_mid_cmd20[2] = { 0x1B,0x00 };
static char nt35596_ie_mid_cmd21[2] = { 0x1C,0x04 };
static char nt35596_ie_mid_cmd22[2] = { 0x1D,0x08 };
static char nt35596_ie_mid_cmd23[2] = { 0x1E,0x0C };
static char nt35596_ie_mid_cmd24[2] = { 0x1F,0x10 };
static char nt35596_ie_mid_cmd25[2] = { 0x20,0x14 };
static char nt35596_ie_mid_cmd26[2] = { 0x21,0x18 };
static char nt35596_ie_mid_cmd27[2] = { 0x22,0x20 };
static char nt35596_ie_mid_cmd28[2] = { 0x23,0x18 };
static char nt35596_ie_mid_cmd29[2] = { 0x24,0x10 };
static char nt35596_ie_mid_cmd30[2] = { 0x25,0x08 };
static char nt35596_ie_mid_cmd31[2] = { 0x26,0x00 };
static char nt35596_ie_mid_cmd32[2] = { 0x27,0x00 };
static char nt35596_ie_mid_cmd33[2] = { 0x28,0x00 };
static char nt35596_ie_mid_cmd34[2] = { 0x29,0x00 };
static char nt35596_ie_mid_cmd35[2] = { 0x2A,0x00 };
static char nt35596_ie_mid_cmd36[2] = { 0x2B,0x00 };
static char nt35596_ie_mid_cmd37[2] = { 0x2F,0x00 };
static char nt35596_ie_mid_cmd38[2] = { 0x30,0x00 };
static char nt35596_ie_mid_cmd39[2] = { 0x31,0x00 };
static char nt35596_ie_mid_cmd40[2] = { 0x32,0x80 };
static char nt35596_ie_mid_cmd41[2] = { 0x33,0x80 };
static char nt35596_ie_mid_cmd42[2] = { 0x34,0x82 };
static char nt35596_ie_mid_cmd43[2] = { 0x35,0x04 };
static char nt35596_ie_mid_cmd44[2] = { 0x36,0x06 };
static char nt35596_ie_mid_cmd45[2] = { 0x37,0x08 };
static char nt35596_ie_mid_cmd46[2] = { 0x38,0x0A };
static char nt35596_ie_mid_cmd47[2] = { 0x39,0x0C };
static char nt35596_ie_mid_cmd48[2] = { 0x3A,0x0E };
static char nt35596_ie_mid_cmd49[2] = { 0x3B,0x10 };
static char nt35596_ie_mid_cmd50[2] = { 0x3F,0x14 };
static char nt35596_ie_mid_cmd51[2] = { 0x40,0x18 };
static char nt35596_ie_mid_cmd52[2] = { 0x41,0x18 };
static char nt35596_ie_mid_cmd53[2] = { 0x42,0x18 };
static char nt35596_ie_mid_cmd54[2] = { 0x43,0x18 };
static char nt35596_ie_mid_cmd55[2] = { 0x44,0x14 };
static char nt35596_ie_mid_cmd56[2] = { 0x45,0x10 };
static char nt35596_ie_mid_cmd57[2] = { 0x46,0x0E };
static char nt35596_ie_mid_cmd58[2] = { 0x47,0x0C };
static char nt35596_ie_mid_cmd59[2] = { 0x48,0x0A };
static char nt35596_ie_mid_cmd60[2] = { 0x49,0x08 };
static char nt35596_ie_mid_cmd61[2] = { 0x4A,0x06 };
static char nt35596_ie_mid_cmd62[2] = { 0x4B,0x04 };
static char nt35596_ie_mid_cmd63[2] = { 0x4C,0x82 };
static char nt35596_ie_mid_cmd64[2] = { 0x1A,0x00 };
static char nt35596_ie_mid_cmd65[2] = { 0x53,0x01 };
static char nt35596_ie_mid_cmd66[2] = { 0x54,0x00 };
static char nt35596_ie_mid_cmd67[2] = { 0x55,0x00 };
static char nt35596_ie_mid_cmd68[2] = { 0x5B,0x00 };
static char nt35596_ie_mid_cmd69[2] = { 0x63,0x00 };
static char nt35596_ie_mid_cmd70[2] = { 0x14,0x00 };
static char nt35596_ie_mid_cmd71[2] = { 0x4D,0x0F };
static char nt35596_ie_mid_cmd72[2] = { 0x58,0x00 };
static char nt35596_ie_mid_cmd73[2] = { 0x5F,0x2F };
static char nt35596_ie_mid_cmd74[2] = { 0x60,0x00 };
//nt35596 ie high cmds
static char nt35596_ie_high_cmd1[2]  = { 0x01,0x04 };
static char nt35596_ie_high_cmd2[2]  = { 0x02,0x08 };
static char nt35596_ie_high_cmd3[2]  = { 0x03,0x0c };
static char nt35596_ie_high_cmd4[2]  = { 0x04,0x10 };
static char nt35596_ie_high_cmd5[2]  = { 0x05,0x14 };
static char nt35596_ie_high_cmd6[2]  = { 0x06,0x18 };
static char nt35596_ie_high_cmd7[2]  = { 0x07,0x20 };
static char nt35596_ie_high_cmd8[2]  = { 0x08,0x28 };
static char nt35596_ie_high_cmd9[2]  = { 0x09,0x30 };
static char nt35596_ie_high_cmd10[2] = { 0x0A,0x38 };
static char nt35596_ie_high_cmd11[2] = { 0x0B,0x44 };
static char nt35596_ie_high_cmd12[2] = { 0x0C,0x54 };
static char nt35596_ie_high_cmd13[2] = { 0x0D,0x64 };
static char nt35596_ie_high_cmd14[2] = { 0x0E,0x78 };
static char nt35596_ie_high_cmd15[2] = { 0x0F,0x90 };
static char nt35596_ie_high_cmd16[2] = { 0x10,0xAC };
static char nt35596_ie_high_cmd17[2] = { 0x11,0xD4 };
static char nt35596_ie_high_cmd18[2] = { 0x12,0xFC };
static char nt35596_ie_high_cmd19[2] = { 0x13,0xFC };
static char nt35596_ie_high_cmd20[2] = { 0x1B,0x00 };
static char nt35596_ie_high_cmd21[2] = { 0x1C,0x04 };
static char nt35596_ie_high_cmd22[2] = { 0x1D,0x08 };
static char nt35596_ie_high_cmd23[2] = { 0x1E,0x0C };
static char nt35596_ie_high_cmd24[2] = { 0x1F,0x10 };
static char nt35596_ie_high_cmd25[2] = { 0x20,0x14 };
static char nt35596_ie_high_cmd26[2] = { 0x21,0x18 };
static char nt35596_ie_high_cmd27[2] = { 0x22,0x20 };
static char nt35596_ie_high_cmd28[2] = { 0x23,0x18 };
static char nt35596_ie_high_cmd29[2] = { 0x24,0x10 };
static char nt35596_ie_high_cmd30[2] = { 0x25,0x08 };
static char nt35596_ie_high_cmd31[2] = { 0x26,0x00 };
static char nt35596_ie_high_cmd32[2] = { 0x27,0x00 };
static char nt35596_ie_high_cmd33[2] = { 0x28,0x00 };
static char nt35596_ie_high_cmd34[2] = { 0x29,0x00 };
static char nt35596_ie_high_cmd35[2] = { 0x2A,0x00 };
static char nt35596_ie_high_cmd36[2] = { 0x2B,0x00 };
static char nt35596_ie_high_cmd37[2] = { 0x2F,0x00 };
static char nt35596_ie_high_cmd38[2] = { 0x30,0x00 };
static char nt35596_ie_high_cmd39[2] = { 0x31,0x00 };
static char nt35596_ie_high_cmd40[2] = { 0x32,0x80 };
static char nt35596_ie_high_cmd41[2] = { 0x33,0x80 };
static char nt35596_ie_high_cmd42[2] = { 0x34,0x82 };
static char nt35596_ie_high_cmd43[2] = { 0x35,0x04 };
static char nt35596_ie_high_cmd44[2] = { 0x36,0x06 };
static char nt35596_ie_high_cmd45[2] = { 0x37,0x08 };
static char nt35596_ie_high_cmd46[2] = { 0x38,0x0A };
static char nt35596_ie_high_cmd47[2] = { 0x39,0x0C };
static char nt35596_ie_high_cmd48[2] = { 0x3A,0x0E };
static char nt35596_ie_high_cmd49[2] = { 0x3B,0x10 };
static char nt35596_ie_high_cmd50[2] = { 0x3F,0x14 };
static char nt35596_ie_high_cmd51[2] = { 0x40,0x18 };
static char nt35596_ie_high_cmd52[2] = { 0x41,0x18 };
static char nt35596_ie_high_cmd53[2] = { 0x42,0x18 };
static char nt35596_ie_high_cmd54[2] = { 0x43,0x18 };
static char nt35596_ie_high_cmd55[2] = { 0x44,0x14 };
static char nt35596_ie_high_cmd56[2] = { 0x45,0x10 };
static char nt35596_ie_high_cmd57[2] = { 0x46,0x0E };
static char nt35596_ie_high_cmd58[2] = { 0x47,0x0C };
static char nt35596_ie_high_cmd59[2] = { 0x48,0x0A };
static char nt35596_ie_high_cmd60[2] = { 0x49,0x08 };
static char nt35596_ie_high_cmd61[2] = { 0x4A,0x06 };
static char nt35596_ie_high_cmd62[2] = { 0x4B,0x04 };
static char nt35596_ie_high_cmd63[2] = { 0x4C,0x82 };
static char nt35596_ie_high_cmd64[2] = { 0x1A,0x00 };
static char nt35596_ie_high_cmd65[2] = { 0x53,0x01 };
static char nt35596_ie_high_cmd66[2] = { 0x54,0x00 };
static char nt35596_ie_high_cmd67[2] = { 0x55,0x00 };
static char nt35596_ie_high_cmd68[2] = { 0x5B,0x00 };
static char nt35596_ie_high_cmd69[2] = { 0x63,0x00 };
static char nt35596_ie_high_cmd70[2] = { 0x14,0x00 };
static char nt35596_ie_high_cmd71[2] = { 0x4D,0x18 };
static char nt35596_ie_high_cmd72[2] = { 0x58,0x00 };
static char nt35596_ie_high_cmd73[2] = { 0x5F,0x2F };
static char nt35596_ie_high_cmd74[2] = { 0x60,0x00 };
#endif
								 
static struct dsi_cmd_desc lg_pannel_ce_low[] = {
	{{DTYPE_GEN_LWRITE, 1, 0, 0, 0, sizeof(r63311_ce_low)}, r63311_ce_low},
};
static struct dsi_cmd_desc lg_pannel_ce_middle[] = {
	{{DTYPE_GEN_LWRITE, 1, 0, 0, 0, sizeof(r63311_ce_middle)}, r63311_ce_middle},
};
static struct dsi_cmd_desc lg_pannel_ce_high[] = {
	{{DTYPE_GEN_LWRITE, 1, 0, 0, 0, sizeof(r63311_ce_high)}, r63311_ce_high},
};

static struct dsi_cmd_desc ys_panel_ce_low[] = {
    //{{DTYPE_DCS_WRITE1, 1, 0, 0, 0, sizeof(nt35596_ie_enable_cmd1)}, nt35596_ie_enable_cmd1},
	//{{DTYPE_DCS_WRITE1, 1, 0, 0, 0, sizeof(nt35596_ie_enable_cmd2)}, nt35596_ie_enable_cmd2},
    #if 1
	{{DTYPE_DCS_WRITE1, 1, 0, 0, 0, sizeof(nt35596_ie_cmd01)}, nt35596_ie_cmd01},
	//{{DTYPE_DCS_WRITE1, 1, 0, 0, 0, sizeof(nt35596_ie_cmd02)}, nt35596_ie_cmd02},
	{{DTYPE_DCS_WRITE1, 1, 0, 0, 0, sizeof(nt35596_ie_cmd00)}, nt35596_ie_cmd00},
	{{DTYPE_DCS_WRITE1, 1, 0, 0, 0, sizeof(nt35596_ie_low_cmd1 )}, nt35596_ie_low_cmd1 },
	{{DTYPE_DCS_WRITE1, 1, 0, 0, 0, sizeof(nt35596_ie_low_cmd2 )}, nt35596_ie_low_cmd2 },
	{{DTYPE_DCS_WRITE1, 1, 0, 0, 0, sizeof(nt35596_ie_low_cmd3 )}, nt35596_ie_low_cmd3 },
	{{DTYPE_DCS_WRITE1, 1, 0, 0, 0, sizeof(nt35596_ie_low_cmd4 )}, nt35596_ie_low_cmd4 },
	{{DTYPE_DCS_WRITE1, 1, 0, 0, 0, sizeof(nt35596_ie_low_cmd5 )}, nt35596_ie_low_cmd5 },
	{{DTYPE_DCS_WRITE1, 1, 0, 0, 0, sizeof(nt35596_ie_low_cmd6 )}, nt35596_ie_low_cmd6 },
	{{DTYPE_DCS_WRITE1, 1, 0, 0, 0, sizeof(nt35596_ie_low_cmd7 )}, nt35596_ie_low_cmd7 },
	{{DTYPE_DCS_WRITE1, 1, 0, 0, 0, sizeof(nt35596_ie_low_cmd8 )}, nt35596_ie_low_cmd8 },
	{{DTYPE_DCS_WRITE1, 1, 0, 0, 0, sizeof(nt35596_ie_low_cmd9 )}, nt35596_ie_low_cmd9 },
	{{DTYPE_DCS_WRITE1, 1, 0, 0, 0, sizeof(nt35596_ie_low_cmd10)}, nt35596_ie_low_cmd10},
	{{DTYPE_DCS_WRITE1, 1, 0, 0, 0, sizeof(nt35596_ie_low_cmd11)}, nt35596_ie_low_cmd11},
	{{DTYPE_DCS_WRITE1, 1, 0, 0, 0, sizeof(nt35596_ie_low_cmd12)}, nt35596_ie_low_cmd12},
	{{DTYPE_DCS_WRITE1, 1, 0, 0, 0, sizeof(nt35596_ie_low_cmd13)}, nt35596_ie_low_cmd13},
	{{DTYPE_DCS_WRITE1, 1, 0, 0, 0, sizeof(nt35596_ie_low_cmd14)}, nt35596_ie_low_cmd14},
	{{DTYPE_DCS_WRITE1, 1, 0, 0, 0, sizeof(nt35596_ie_low_cmd15)}, nt35596_ie_low_cmd15},
	{{DTYPE_DCS_WRITE1, 1, 0, 0, 0, sizeof(nt35596_ie_low_cmd16)}, nt35596_ie_low_cmd16},
	{{DTYPE_DCS_WRITE1, 1, 0, 0, 0, sizeof(nt35596_ie_low_cmd17)}, nt35596_ie_low_cmd17},
	{{DTYPE_DCS_WRITE1, 1, 0, 0, 0, sizeof(nt35596_ie_low_cmd18)}, nt35596_ie_low_cmd18},
	{{DTYPE_DCS_WRITE1, 1, 0, 0, 0, sizeof(nt35596_ie_low_cmd19)}, nt35596_ie_low_cmd19},
	{{DTYPE_DCS_WRITE1, 1, 0, 0, 0, sizeof(nt35596_ie_low_cmd20)}, nt35596_ie_low_cmd20},
	{{DTYPE_DCS_WRITE1, 1, 0, 0, 0, sizeof(nt35596_ie_low_cmd21)}, nt35596_ie_low_cmd21},
	{{DTYPE_DCS_WRITE1, 1, 0, 0, 0, sizeof(nt35596_ie_low_cmd22)}, nt35596_ie_low_cmd22},
	{{DTYPE_DCS_WRITE1, 1, 0, 0, 0, sizeof(nt35596_ie_low_cmd23)}, nt35596_ie_low_cmd23},
	{{DTYPE_DCS_WRITE1, 1, 0, 0, 0, sizeof(nt35596_ie_low_cmd24)}, nt35596_ie_low_cmd24},
	{{DTYPE_DCS_WRITE1, 1, 0, 0, 0, sizeof(nt35596_ie_low_cmd25)}, nt35596_ie_low_cmd25},
	{{DTYPE_DCS_WRITE1, 1, 0, 0, 0, sizeof(nt35596_ie_low_cmd26)}, nt35596_ie_low_cmd26},
	{{DTYPE_DCS_WRITE1, 1, 0, 0, 0, sizeof(nt35596_ie_low_cmd27)}, nt35596_ie_low_cmd27},
	{{DTYPE_DCS_WRITE1, 1, 0, 0, 0, sizeof(nt35596_ie_low_cmd28)}, nt35596_ie_low_cmd28},
	{{DTYPE_DCS_WRITE1, 1, 0, 0, 0, sizeof(nt35596_ie_low_cmd29)}, nt35596_ie_low_cmd29},
	{{DTYPE_DCS_WRITE1, 1, 0, 0, 0, sizeof(nt35596_ie_low_cmd30)}, nt35596_ie_low_cmd30},
	{{DTYPE_DCS_WRITE1, 1, 0, 0, 0, sizeof(nt35596_ie_low_cmd31)}, nt35596_ie_low_cmd31},
	{{DTYPE_DCS_WRITE1, 1, 0, 0, 0, sizeof(nt35596_ie_low_cmd32)}, nt35596_ie_low_cmd32},
	{{DTYPE_DCS_WRITE1, 1, 0, 0, 0, sizeof(nt35596_ie_low_cmd33)}, nt35596_ie_low_cmd33},
	{{DTYPE_DCS_WRITE1, 1, 0, 0, 0, sizeof(nt35596_ie_low_cmd34)}, nt35596_ie_low_cmd34},
	{{DTYPE_DCS_WRITE1, 1, 0, 0, 0, sizeof(nt35596_ie_low_cmd35)}, nt35596_ie_low_cmd35},
	{{DTYPE_DCS_WRITE1, 1, 0, 0, 0, sizeof(nt35596_ie_low_cmd36)}, nt35596_ie_low_cmd36},
	{{DTYPE_DCS_WRITE1, 1, 0, 0, 0, sizeof(nt35596_ie_low_cmd37)}, nt35596_ie_low_cmd37},
	{{DTYPE_DCS_WRITE1, 1, 0, 0, 0, sizeof(nt35596_ie_low_cmd38)}, nt35596_ie_low_cmd38},
	{{DTYPE_DCS_WRITE1, 1, 0, 0, 0, sizeof(nt35596_ie_low_cmd39)}, nt35596_ie_low_cmd39},
	{{DTYPE_DCS_WRITE1, 1, 0, 0, 0, sizeof(nt35596_ie_low_cmd40)}, nt35596_ie_low_cmd40},
	{{DTYPE_DCS_WRITE1, 1, 0, 0, 0, sizeof(nt35596_ie_low_cmd41)}, nt35596_ie_low_cmd41},
	{{DTYPE_DCS_WRITE1, 1, 0, 0, 0, sizeof(nt35596_ie_low_cmd42)}, nt35596_ie_low_cmd42},
	{{DTYPE_DCS_WRITE1, 1, 0, 0, 0, sizeof(nt35596_ie_low_cmd43)}, nt35596_ie_low_cmd43},
	{{DTYPE_DCS_WRITE1, 1, 0, 0, 0, sizeof(nt35596_ie_low_cmd44)}, nt35596_ie_low_cmd44},
	{{DTYPE_DCS_WRITE1, 1, 0, 0, 0, sizeof(nt35596_ie_low_cmd45)}, nt35596_ie_low_cmd45},
	{{DTYPE_DCS_WRITE1, 1, 0, 0, 0, sizeof(nt35596_ie_low_cmd46)}, nt35596_ie_low_cmd46},
	{{DTYPE_DCS_WRITE1, 1, 0, 0, 0, sizeof(nt35596_ie_low_cmd47)}, nt35596_ie_low_cmd47},
	{{DTYPE_DCS_WRITE1, 1, 0, 0, 0, sizeof(nt35596_ie_low_cmd48)}, nt35596_ie_low_cmd48},
	{{DTYPE_DCS_WRITE1, 1, 0, 0, 0, sizeof(nt35596_ie_low_cmd49)}, nt35596_ie_low_cmd49},
	{{DTYPE_DCS_WRITE1, 1, 0, 0, 0, sizeof(nt35596_ie_low_cmd50)}, nt35596_ie_low_cmd50},
	{{DTYPE_DCS_WRITE1, 1, 0, 0, 0, sizeof(nt35596_ie_low_cmd51)}, nt35596_ie_low_cmd51},
	{{DTYPE_DCS_WRITE1, 1, 0, 0, 0, sizeof(nt35596_ie_low_cmd52)}, nt35596_ie_low_cmd52},
	{{DTYPE_DCS_WRITE1, 1, 0, 0, 0, sizeof(nt35596_ie_low_cmd53)}, nt35596_ie_low_cmd53},
	{{DTYPE_DCS_WRITE1, 1, 0, 0, 0, sizeof(nt35596_ie_low_cmd54)}, nt35596_ie_low_cmd54},
	{{DTYPE_DCS_WRITE1, 1, 0, 0, 0, sizeof(nt35596_ie_low_cmd55)}, nt35596_ie_low_cmd55},
	{{DTYPE_DCS_WRITE1, 1, 0, 0, 0, sizeof(nt35596_ie_low_cmd56)}, nt35596_ie_low_cmd56},
	{{DTYPE_DCS_WRITE1, 1, 0, 0, 0, sizeof(nt35596_ie_low_cmd57)}, nt35596_ie_low_cmd57},
	{{DTYPE_DCS_WRITE1, 1, 0, 0, 0, sizeof(nt35596_ie_low_cmd58)}, nt35596_ie_low_cmd58},
	{{DTYPE_DCS_WRITE1, 1, 0, 0, 0, sizeof(nt35596_ie_low_cmd59)}, nt35596_ie_low_cmd59},
	{{DTYPE_DCS_WRITE1, 1, 0, 0, 0, sizeof(nt35596_ie_low_cmd60)}, nt35596_ie_low_cmd60},
	{{DTYPE_DCS_WRITE1, 1, 0, 0, 0, sizeof(nt35596_ie_low_cmd61)}, nt35596_ie_low_cmd61},
	{{DTYPE_DCS_WRITE1, 1, 0, 0, 0, sizeof(nt35596_ie_low_cmd62)}, nt35596_ie_low_cmd62},
	{{DTYPE_DCS_WRITE1, 1, 0, 0, 0, sizeof(nt35596_ie_low_cmd63)}, nt35596_ie_low_cmd63},
	{{DTYPE_DCS_WRITE1, 1, 0, 0, 0, sizeof(nt35596_ie_low_cmd64)}, nt35596_ie_low_cmd64},
	{{DTYPE_DCS_WRITE1, 1, 0, 0, 0, sizeof(nt35596_ie_low_cmd65)}, nt35596_ie_low_cmd65},
	{{DTYPE_DCS_WRITE1, 1, 0, 0, 0, sizeof(nt35596_ie_low_cmd66)}, nt35596_ie_low_cmd66},
	{{DTYPE_DCS_WRITE1, 1, 0, 0, 0, sizeof(nt35596_ie_low_cmd67)}, nt35596_ie_low_cmd67},
	{{DTYPE_DCS_WRITE1, 1, 0, 0, 0, sizeof(nt35596_ie_low_cmd68)}, nt35596_ie_low_cmd68},
	{{DTYPE_DCS_WRITE1, 1, 0, 0, 0, sizeof(nt35596_ie_low_cmd69)}, nt35596_ie_low_cmd69},
	{{DTYPE_DCS_WRITE1, 1, 0, 0, 0, sizeof(nt35596_ie_low_cmd70)}, nt35596_ie_low_cmd70},
	{{DTYPE_DCS_WRITE1, 1, 0, 0, 0, sizeof(nt35596_ie_low_cmd71)}, nt35596_ie_low_cmd71},
	{{DTYPE_DCS_WRITE1, 1, 0, 0, 0, sizeof(nt35596_ie_low_cmd72)}, nt35596_ie_low_cmd72},
	{{DTYPE_DCS_WRITE1, 1, 0, 0, 0, sizeof(nt35596_ie_low_cmd73)}, nt35596_ie_low_cmd73},
	{{DTYPE_DCS_WRITE1, 1, 0, 0, 0, sizeof(nt35596_ie_low_cmd74)}, nt35596_ie_low_cmd74},
    #endif
	{{DTYPE_DCS_WRITE1, 1, 0, 0, 0, sizeof(nt35596_ie_cmd01)}, nt35596_ie_cmd01},
	{{DTYPE_DCS_WRITE1, 1, 0, 0, 0, sizeof(nt35596_ie_cmd02)}, nt35596_ie_cmd02},
	{{DTYPE_DCS_WRITE1, 1, 0, 0, 0, sizeof(nt35596_ie_enable_cmd1)}, nt35596_ie_enable_cmd1},
	{{DTYPE_DCS_WRITE1, 1, 0, 0, 0, sizeof(nt35596_ie_enable_cmd2)}, nt35596_ie_enable_cmd2},
};
static struct dsi_cmd_desc ys_panel_ce_mid[] = {
    //{{DTYPE_DCS_WRITE1, 1, 0, 0, 0, sizeof(nt35596_ie_enable_cmd1)}, nt35596_ie_enable_cmd1},
	//{{DTYPE_DCS_WRITE1, 1, 0, 0, 0, sizeof(nt35596_ie_enable_cmd2)}, nt35596_ie_enable_cmd2},
	#if 1
    {{DTYPE_DCS_WRITE1, 1, 0, 0, 0, sizeof(nt35596_ie_cmd01)}, nt35596_ie_cmd01},
	//{{DTYPE_DCS_WRITE1, 1, 0, 0, 0, sizeof(nt35596_ie_cmd02)}, nt35596_ie_cmd02},
	{{DTYPE_DCS_WRITE1, 1, 0, 0, 0, sizeof(nt35596_ie_cmd00)}, nt35596_ie_cmd00},
	{{DTYPE_DCS_WRITE1, 1, 0, 0, 0, sizeof(nt35596_ie_mid_cmd1 )}, nt35596_ie_mid_cmd1 },
	{{DTYPE_DCS_WRITE1, 1, 0, 0, 0, sizeof(nt35596_ie_mid_cmd2 )}, nt35596_ie_mid_cmd2 },
	{{DTYPE_DCS_WRITE1, 1, 0, 0, 0, sizeof(nt35596_ie_mid_cmd3 )}, nt35596_ie_mid_cmd3 },
	{{DTYPE_DCS_WRITE1, 1, 0, 0, 0, sizeof(nt35596_ie_mid_cmd4 )}, nt35596_ie_mid_cmd4 },
	{{DTYPE_DCS_WRITE1, 1, 0, 0, 0, sizeof(nt35596_ie_mid_cmd5 )}, nt35596_ie_mid_cmd5 },
	{{DTYPE_DCS_WRITE1, 1, 0, 0, 0, sizeof(nt35596_ie_mid_cmd6 )}, nt35596_ie_mid_cmd6 },
	{{DTYPE_DCS_WRITE1, 1, 0, 0, 0, sizeof(nt35596_ie_mid_cmd7 )}, nt35596_ie_mid_cmd7 },
	{{DTYPE_DCS_WRITE1, 1, 0, 0, 0, sizeof(nt35596_ie_mid_cmd8 )}, nt35596_ie_mid_cmd8 },
	{{DTYPE_DCS_WRITE1, 1, 0, 0, 0, sizeof(nt35596_ie_mid_cmd9 )}, nt35596_ie_mid_cmd9 },
	{{DTYPE_DCS_WRITE1, 1, 0, 0, 0, sizeof(nt35596_ie_mid_cmd10)}, nt35596_ie_mid_cmd10},
	{{DTYPE_DCS_WRITE1, 1, 0, 0, 0, sizeof(nt35596_ie_mid_cmd11)}, nt35596_ie_mid_cmd11},
	{{DTYPE_DCS_WRITE1, 1, 0, 0, 0, sizeof(nt35596_ie_mid_cmd12)}, nt35596_ie_mid_cmd12},
	{{DTYPE_DCS_WRITE1, 1, 0, 0, 0, sizeof(nt35596_ie_mid_cmd13)}, nt35596_ie_mid_cmd13},
	{{DTYPE_DCS_WRITE1, 1, 0, 0, 0, sizeof(nt35596_ie_mid_cmd14)}, nt35596_ie_mid_cmd14},
	{{DTYPE_DCS_WRITE1, 1, 0, 0, 0, sizeof(nt35596_ie_mid_cmd15)}, nt35596_ie_mid_cmd15},
	{{DTYPE_DCS_WRITE1, 1, 0, 0, 0, sizeof(nt35596_ie_mid_cmd16)}, nt35596_ie_mid_cmd16},
	{{DTYPE_DCS_WRITE1, 1, 0, 0, 0, sizeof(nt35596_ie_mid_cmd17)}, nt35596_ie_mid_cmd17},
	{{DTYPE_DCS_WRITE1, 1, 0, 0, 0, sizeof(nt35596_ie_mid_cmd18)}, nt35596_ie_mid_cmd18},
	{{DTYPE_DCS_WRITE1, 1, 0, 0, 0, sizeof(nt35596_ie_mid_cmd19)}, nt35596_ie_mid_cmd19},
	{{DTYPE_DCS_WRITE1, 1, 0, 0, 0, sizeof(nt35596_ie_mid_cmd20)}, nt35596_ie_mid_cmd20},
	{{DTYPE_DCS_WRITE1, 1, 0, 0, 0, sizeof(nt35596_ie_mid_cmd21)}, nt35596_ie_mid_cmd21},
	{{DTYPE_DCS_WRITE1, 1, 0, 0, 0, sizeof(nt35596_ie_mid_cmd22)}, nt35596_ie_mid_cmd22},
	{{DTYPE_DCS_WRITE1, 1, 0, 0, 0, sizeof(nt35596_ie_mid_cmd23)}, nt35596_ie_mid_cmd23},
	{{DTYPE_DCS_WRITE1, 1, 0, 0, 0, sizeof(nt35596_ie_mid_cmd24)}, nt35596_ie_mid_cmd24},
	{{DTYPE_DCS_WRITE1, 1, 0, 0, 0, sizeof(nt35596_ie_mid_cmd25)}, nt35596_ie_mid_cmd25},
	{{DTYPE_DCS_WRITE1, 1, 0, 0, 0, sizeof(nt35596_ie_mid_cmd26)}, nt35596_ie_mid_cmd26},
	{{DTYPE_DCS_WRITE1, 1, 0, 0, 0, sizeof(nt35596_ie_mid_cmd27)}, nt35596_ie_mid_cmd27},
	{{DTYPE_DCS_WRITE1, 1, 0, 0, 0, sizeof(nt35596_ie_mid_cmd28)}, nt35596_ie_mid_cmd28},
	{{DTYPE_DCS_WRITE1, 1, 0, 0, 0, sizeof(nt35596_ie_mid_cmd29)}, nt35596_ie_mid_cmd29},
	{{DTYPE_DCS_WRITE1, 1, 0, 0, 0, sizeof(nt35596_ie_mid_cmd30)}, nt35596_ie_mid_cmd30},
	{{DTYPE_DCS_WRITE1, 1, 0, 0, 0, sizeof(nt35596_ie_mid_cmd31)}, nt35596_ie_mid_cmd31},
	{{DTYPE_DCS_WRITE1, 1, 0, 0, 0, sizeof(nt35596_ie_mid_cmd32)}, nt35596_ie_mid_cmd32},
	{{DTYPE_DCS_WRITE1, 1, 0, 0, 0, sizeof(nt35596_ie_mid_cmd33)}, nt35596_ie_mid_cmd33},
	{{DTYPE_DCS_WRITE1, 1, 0, 0, 0, sizeof(nt35596_ie_mid_cmd34)}, nt35596_ie_mid_cmd34},
	{{DTYPE_DCS_WRITE1, 1, 0, 0, 0, sizeof(nt35596_ie_mid_cmd35)}, nt35596_ie_mid_cmd35},
	{{DTYPE_DCS_WRITE1, 1, 0, 0, 0, sizeof(nt35596_ie_mid_cmd36)}, nt35596_ie_mid_cmd36},
	{{DTYPE_DCS_WRITE1, 1, 0, 0, 0, sizeof(nt35596_ie_mid_cmd37)}, nt35596_ie_mid_cmd37},
	{{DTYPE_DCS_WRITE1, 1, 0, 0, 0, sizeof(nt35596_ie_mid_cmd38)}, nt35596_ie_mid_cmd38},
	{{DTYPE_DCS_WRITE1, 1, 0, 0, 0, sizeof(nt35596_ie_mid_cmd39)}, nt35596_ie_mid_cmd39},
	{{DTYPE_DCS_WRITE1, 1, 0, 0, 0, sizeof(nt35596_ie_mid_cmd40)}, nt35596_ie_mid_cmd40},
	{{DTYPE_DCS_WRITE1, 1, 0, 0, 0, sizeof(nt35596_ie_mid_cmd41)}, nt35596_ie_mid_cmd41},
	{{DTYPE_DCS_WRITE1, 1, 0, 0, 0, sizeof(nt35596_ie_mid_cmd42)}, nt35596_ie_mid_cmd42},
	{{DTYPE_DCS_WRITE1, 1, 0, 0, 0, sizeof(nt35596_ie_mid_cmd43)}, nt35596_ie_mid_cmd43},
	{{DTYPE_DCS_WRITE1, 1, 0, 0, 0, sizeof(nt35596_ie_mid_cmd44)}, nt35596_ie_mid_cmd44},
	{{DTYPE_DCS_WRITE1, 1, 0, 0, 0, sizeof(nt35596_ie_mid_cmd45)}, nt35596_ie_mid_cmd45},
	{{DTYPE_DCS_WRITE1, 1, 0, 0, 0, sizeof(nt35596_ie_mid_cmd46)}, nt35596_ie_mid_cmd46},
	{{DTYPE_DCS_WRITE1, 1, 0, 0, 0, sizeof(nt35596_ie_mid_cmd47)}, nt35596_ie_mid_cmd47},
	{{DTYPE_DCS_WRITE1, 1, 0, 0, 0, sizeof(nt35596_ie_mid_cmd48)}, nt35596_ie_mid_cmd48},
	{{DTYPE_DCS_WRITE1, 1, 0, 0, 0, sizeof(nt35596_ie_mid_cmd49)}, nt35596_ie_mid_cmd49},
	{{DTYPE_DCS_WRITE1, 1, 0, 0, 0, sizeof(nt35596_ie_mid_cmd50)}, nt35596_ie_mid_cmd50},
	{{DTYPE_DCS_WRITE1, 1, 0, 0, 0, sizeof(nt35596_ie_mid_cmd51)}, nt35596_ie_mid_cmd51},
	{{DTYPE_DCS_WRITE1, 1, 0, 0, 0, sizeof(nt35596_ie_mid_cmd52)}, nt35596_ie_mid_cmd52},
	{{DTYPE_DCS_WRITE1, 1, 0, 0, 0, sizeof(nt35596_ie_mid_cmd53)}, nt35596_ie_mid_cmd53},
	{{DTYPE_DCS_WRITE1, 1, 0, 0, 0, sizeof(nt35596_ie_mid_cmd54)}, nt35596_ie_mid_cmd54},
	{{DTYPE_DCS_WRITE1, 1, 0, 0, 0, sizeof(nt35596_ie_mid_cmd55)}, nt35596_ie_mid_cmd55},
	{{DTYPE_DCS_WRITE1, 1, 0, 0, 0, sizeof(nt35596_ie_mid_cmd56)}, nt35596_ie_mid_cmd56},
	{{DTYPE_DCS_WRITE1, 1, 0, 0, 0, sizeof(nt35596_ie_mid_cmd57)}, nt35596_ie_mid_cmd57},
	{{DTYPE_DCS_WRITE1, 1, 0, 0, 0, sizeof(nt35596_ie_mid_cmd58)}, nt35596_ie_mid_cmd58},
	{{DTYPE_DCS_WRITE1, 1, 0, 0, 0, sizeof(nt35596_ie_mid_cmd59)}, nt35596_ie_mid_cmd59},
	{{DTYPE_DCS_WRITE1, 1, 0, 0, 0, sizeof(nt35596_ie_mid_cmd60)}, nt35596_ie_mid_cmd60},
	{{DTYPE_DCS_WRITE1, 1, 0, 0, 0, sizeof(nt35596_ie_mid_cmd61)}, nt35596_ie_mid_cmd61},
	{{DTYPE_DCS_WRITE1, 1, 0, 0, 0, sizeof(nt35596_ie_mid_cmd62)}, nt35596_ie_mid_cmd62},
	{{DTYPE_DCS_WRITE1, 1, 0, 0, 0, sizeof(nt35596_ie_mid_cmd63)}, nt35596_ie_mid_cmd63},
	{{DTYPE_DCS_WRITE1, 1, 0, 0, 0, sizeof(nt35596_ie_mid_cmd64)}, nt35596_ie_mid_cmd64},
	{{DTYPE_DCS_WRITE1, 1, 0, 0, 0, sizeof(nt35596_ie_mid_cmd65)}, nt35596_ie_mid_cmd65},
	{{DTYPE_DCS_WRITE1, 1, 0, 0, 0, sizeof(nt35596_ie_mid_cmd66)}, nt35596_ie_mid_cmd66},
	{{DTYPE_DCS_WRITE1, 1, 0, 0, 0, sizeof(nt35596_ie_mid_cmd67)}, nt35596_ie_mid_cmd67},
	{{DTYPE_DCS_WRITE1, 1, 0, 0, 0, sizeof(nt35596_ie_mid_cmd68)}, nt35596_ie_mid_cmd68},
	{{DTYPE_DCS_WRITE1, 1, 0, 0, 0, sizeof(nt35596_ie_mid_cmd69)}, nt35596_ie_mid_cmd69},
	{{DTYPE_DCS_WRITE1, 1, 0, 0, 0, sizeof(nt35596_ie_mid_cmd70)}, nt35596_ie_mid_cmd70},
	{{DTYPE_DCS_WRITE1, 1, 0, 0, 0, sizeof(nt35596_ie_mid_cmd71)}, nt35596_ie_mid_cmd71},
	{{DTYPE_DCS_WRITE1, 1, 0, 0, 0, sizeof(nt35596_ie_mid_cmd72)}, nt35596_ie_mid_cmd72},
	{{DTYPE_DCS_WRITE1, 1, 0, 0, 0, sizeof(nt35596_ie_mid_cmd73)}, nt35596_ie_mid_cmd73},
	{{DTYPE_DCS_WRITE1, 1, 0, 0, 0, sizeof(nt35596_ie_mid_cmd74)}, nt35596_ie_mid_cmd74},
    #endif
	{{DTYPE_DCS_WRITE1, 1, 0, 0, 0, sizeof(nt35596_ie_cmd01)}, nt35596_ie_cmd01},
	{{DTYPE_DCS_WRITE1, 1, 0, 0, 0, sizeof(nt35596_ie_cmd02)}, nt35596_ie_cmd02},
	{{DTYPE_DCS_WRITE1, 1, 0, 0, 0, sizeof(nt35596_ie_enable_cmd1)}, nt35596_ie_enable_cmd1},
	{{DTYPE_DCS_WRITE1, 1, 0, 0, 0, sizeof(nt35596_ie_enable_cmd2)}, nt35596_ie_enable_cmd2},
};
static struct dsi_cmd_desc ys_panel_ce_high[] = {
    //{{DTYPE_DCS_WRITE1, 1, 0, 0, 0, sizeof(nt35596_ie_enable_cmd1)}, nt35596_ie_enable_cmd1},
	//{{DTYPE_DCS_WRITE1, 1, 0, 0, 0, sizeof(nt35596_ie_enable_cmd2)}, nt35596_ie_enable_cmd2},
	#if 1
    {{DTYPE_DCS_WRITE1, 1, 0, 0, 0, sizeof(nt35596_ie_cmd01)}, nt35596_ie_cmd01},
	//{{DTYPE_DCS_WRITE1, 1, 0, 0, 0, sizeof(nt35596_ie_cmd02)}, nt35596_ie_cmd02},
	{{DTYPE_DCS_WRITE1, 1, 0, 0, 0, sizeof(nt35596_ie_cmd00)}, nt35596_ie_cmd00},
	{{DTYPE_DCS_WRITE1, 1, 0, 0, 0, sizeof(nt35596_ie_high_cmd1 )}, nt35596_ie_high_cmd1 },
	{{DTYPE_DCS_WRITE1, 1, 0, 0, 0, sizeof(nt35596_ie_high_cmd2 )}, nt35596_ie_high_cmd2 },
	{{DTYPE_DCS_WRITE1, 1, 0, 0, 0, sizeof(nt35596_ie_high_cmd3 )}, nt35596_ie_high_cmd3 },
	{{DTYPE_DCS_WRITE1, 1, 0, 0, 0, sizeof(nt35596_ie_high_cmd4 )}, nt35596_ie_high_cmd4 },
	{{DTYPE_DCS_WRITE1, 1, 0, 0, 0, sizeof(nt35596_ie_high_cmd5 )}, nt35596_ie_high_cmd5 },
	{{DTYPE_DCS_WRITE1, 1, 0, 0, 0, sizeof(nt35596_ie_high_cmd6 )}, nt35596_ie_high_cmd6 },
	{{DTYPE_DCS_WRITE1, 1, 0, 0, 0, sizeof(nt35596_ie_high_cmd7 )}, nt35596_ie_high_cmd7 },
	{{DTYPE_DCS_WRITE1, 1, 0, 0, 0, sizeof(nt35596_ie_high_cmd8 )}, nt35596_ie_high_cmd8 },
	{{DTYPE_DCS_WRITE1, 1, 0, 0, 0, sizeof(nt35596_ie_high_cmd9 )}, nt35596_ie_high_cmd9 },
	{{DTYPE_DCS_WRITE1, 1, 0, 0, 0, sizeof(nt35596_ie_high_cmd10)}, nt35596_ie_high_cmd10},
	{{DTYPE_DCS_WRITE1, 1, 0, 0, 0, sizeof(nt35596_ie_high_cmd11)}, nt35596_ie_high_cmd11},
	{{DTYPE_DCS_WRITE1, 1, 0, 0, 0, sizeof(nt35596_ie_high_cmd12)}, nt35596_ie_high_cmd12},
	{{DTYPE_DCS_WRITE1, 1, 0, 0, 0, sizeof(nt35596_ie_high_cmd13)}, nt35596_ie_high_cmd13},
	{{DTYPE_DCS_WRITE1, 1, 0, 0, 0, sizeof(nt35596_ie_high_cmd14)}, nt35596_ie_high_cmd14},
	{{DTYPE_DCS_WRITE1, 1, 0, 0, 0, sizeof(nt35596_ie_high_cmd15)}, nt35596_ie_high_cmd15},
	{{DTYPE_DCS_WRITE1, 1, 0, 0, 0, sizeof(nt35596_ie_high_cmd16)}, nt35596_ie_high_cmd16},
	{{DTYPE_DCS_WRITE1, 1, 0, 0, 0, sizeof(nt35596_ie_high_cmd17)}, nt35596_ie_high_cmd17},
	{{DTYPE_DCS_WRITE1, 1, 0, 0, 0, sizeof(nt35596_ie_high_cmd18)}, nt35596_ie_high_cmd18},
	{{DTYPE_DCS_WRITE1, 1, 0, 0, 0, sizeof(nt35596_ie_high_cmd19)}, nt35596_ie_high_cmd19},
	{{DTYPE_DCS_WRITE1, 1, 0, 0, 0, sizeof(nt35596_ie_high_cmd20)}, nt35596_ie_high_cmd20},
	{{DTYPE_DCS_WRITE1, 1, 0, 0, 0, sizeof(nt35596_ie_high_cmd21)}, nt35596_ie_high_cmd21},
	{{DTYPE_DCS_WRITE1, 1, 0, 0, 0, sizeof(nt35596_ie_high_cmd22)}, nt35596_ie_high_cmd22},
	{{DTYPE_DCS_WRITE1, 1, 0, 0, 0, sizeof(nt35596_ie_high_cmd23)}, nt35596_ie_high_cmd23},
	{{DTYPE_DCS_WRITE1, 1, 0, 0, 0, sizeof(nt35596_ie_high_cmd24)}, nt35596_ie_high_cmd24},
	{{DTYPE_DCS_WRITE1, 1, 0, 0, 0, sizeof(nt35596_ie_high_cmd25)}, nt35596_ie_high_cmd25},
	{{DTYPE_DCS_WRITE1, 1, 0, 0, 0, sizeof(nt35596_ie_high_cmd26)}, nt35596_ie_high_cmd26},
	{{DTYPE_DCS_WRITE1, 1, 0, 0, 0, sizeof(nt35596_ie_high_cmd27)}, nt35596_ie_high_cmd27},
	{{DTYPE_DCS_WRITE1, 1, 0, 0, 0, sizeof(nt35596_ie_high_cmd28)}, nt35596_ie_high_cmd28},
	{{DTYPE_DCS_WRITE1, 1, 0, 0, 0, sizeof(nt35596_ie_high_cmd29)}, nt35596_ie_high_cmd29},
	{{DTYPE_DCS_WRITE1, 1, 0, 0, 0, sizeof(nt35596_ie_high_cmd30)}, nt35596_ie_high_cmd30},
	{{DTYPE_DCS_WRITE1, 1, 0, 0, 0, sizeof(nt35596_ie_high_cmd31)}, nt35596_ie_high_cmd31},
	{{DTYPE_DCS_WRITE1, 1, 0, 0, 0, sizeof(nt35596_ie_high_cmd32)}, nt35596_ie_high_cmd32},
	{{DTYPE_DCS_WRITE1, 1, 0, 0, 0, sizeof(nt35596_ie_high_cmd33)}, nt35596_ie_high_cmd33},
	{{DTYPE_DCS_WRITE1, 1, 0, 0, 0, sizeof(nt35596_ie_high_cmd34)}, nt35596_ie_high_cmd34},
	{{DTYPE_DCS_WRITE1, 1, 0, 0, 0, sizeof(nt35596_ie_high_cmd35)}, nt35596_ie_high_cmd35},
	{{DTYPE_DCS_WRITE1, 1, 0, 0, 0, sizeof(nt35596_ie_high_cmd36)}, nt35596_ie_high_cmd36},
	{{DTYPE_DCS_WRITE1, 1, 0, 0, 0, sizeof(nt35596_ie_high_cmd37)}, nt35596_ie_high_cmd37},
	{{DTYPE_DCS_WRITE1, 1, 0, 0, 0, sizeof(nt35596_ie_high_cmd38)}, nt35596_ie_high_cmd38},
	{{DTYPE_DCS_WRITE1, 1, 0, 0, 0, sizeof(nt35596_ie_high_cmd39)}, nt35596_ie_high_cmd39},
	{{DTYPE_DCS_WRITE1, 1, 0, 0, 0, sizeof(nt35596_ie_high_cmd40)}, nt35596_ie_high_cmd40},
	{{DTYPE_DCS_WRITE1, 1, 0, 0, 0, sizeof(nt35596_ie_high_cmd41)}, nt35596_ie_high_cmd41},
	{{DTYPE_DCS_WRITE1, 1, 0, 0, 0, sizeof(nt35596_ie_high_cmd42)}, nt35596_ie_high_cmd42},
	{{DTYPE_DCS_WRITE1, 1, 0, 0, 0, sizeof(nt35596_ie_high_cmd43)}, nt35596_ie_high_cmd43},
	{{DTYPE_DCS_WRITE1, 1, 0, 0, 0, sizeof(nt35596_ie_high_cmd44)}, nt35596_ie_high_cmd44},
	{{DTYPE_DCS_WRITE1, 1, 0, 0, 0, sizeof(nt35596_ie_high_cmd45)}, nt35596_ie_high_cmd45},
	{{DTYPE_DCS_WRITE1, 1, 0, 0, 0, sizeof(nt35596_ie_high_cmd46)}, nt35596_ie_high_cmd46},
	{{DTYPE_DCS_WRITE1, 1, 0, 0, 0, sizeof(nt35596_ie_high_cmd47)}, nt35596_ie_high_cmd47},
	{{DTYPE_DCS_WRITE1, 1, 0, 0, 0, sizeof(nt35596_ie_high_cmd48)}, nt35596_ie_high_cmd48},
	{{DTYPE_DCS_WRITE1, 1, 0, 0, 0, sizeof(nt35596_ie_high_cmd49)}, nt35596_ie_high_cmd49},
	{{DTYPE_DCS_WRITE1, 1, 0, 0, 0, sizeof(nt35596_ie_high_cmd50)}, nt35596_ie_high_cmd50},
	{{DTYPE_DCS_WRITE1, 1, 0, 0, 0, sizeof(nt35596_ie_high_cmd51)}, nt35596_ie_high_cmd51},
	{{DTYPE_DCS_WRITE1, 1, 0, 0, 0, sizeof(nt35596_ie_high_cmd52)}, nt35596_ie_high_cmd52},
	{{DTYPE_DCS_WRITE1, 1, 0, 0, 0, sizeof(nt35596_ie_high_cmd53)}, nt35596_ie_high_cmd53},
	{{DTYPE_DCS_WRITE1, 1, 0, 0, 0, sizeof(nt35596_ie_high_cmd54)}, nt35596_ie_high_cmd54},
	{{DTYPE_DCS_WRITE1, 1, 0, 0, 0, sizeof(nt35596_ie_high_cmd55)}, nt35596_ie_high_cmd55},
	{{DTYPE_DCS_WRITE1, 1, 0, 0, 0, sizeof(nt35596_ie_high_cmd56)}, nt35596_ie_high_cmd56},
	{{DTYPE_DCS_WRITE1, 1, 0, 0, 0, sizeof(nt35596_ie_high_cmd57)}, nt35596_ie_high_cmd57},
	{{DTYPE_DCS_WRITE1, 1, 0, 0, 0, sizeof(nt35596_ie_high_cmd58)}, nt35596_ie_high_cmd58},
	{{DTYPE_DCS_WRITE1, 1, 0, 0, 0, sizeof(nt35596_ie_high_cmd59)}, nt35596_ie_high_cmd59},
	{{DTYPE_DCS_WRITE1, 1, 0, 0, 0, sizeof(nt35596_ie_high_cmd60)}, nt35596_ie_high_cmd60},
	{{DTYPE_DCS_WRITE1, 1, 0, 0, 0, sizeof(nt35596_ie_high_cmd61)}, nt35596_ie_high_cmd61},
	{{DTYPE_DCS_WRITE1, 1, 0, 0, 0, sizeof(nt35596_ie_high_cmd62)}, nt35596_ie_high_cmd62},
	{{DTYPE_DCS_WRITE1, 1, 0, 0, 0, sizeof(nt35596_ie_high_cmd63)}, nt35596_ie_high_cmd63},
	{{DTYPE_DCS_WRITE1, 1, 0, 0, 0, sizeof(nt35596_ie_high_cmd64)}, nt35596_ie_high_cmd64},
	{{DTYPE_DCS_WRITE1, 1, 0, 0, 0, sizeof(nt35596_ie_high_cmd65)}, nt35596_ie_high_cmd65},
	{{DTYPE_DCS_WRITE1, 1, 0, 0, 0, sizeof(nt35596_ie_high_cmd66)}, nt35596_ie_high_cmd66},
	{{DTYPE_DCS_WRITE1, 1, 0, 0, 0, sizeof(nt35596_ie_high_cmd67)}, nt35596_ie_high_cmd67},
	{{DTYPE_DCS_WRITE1, 1, 0, 0, 0, sizeof(nt35596_ie_high_cmd68)}, nt35596_ie_high_cmd68},
	{{DTYPE_DCS_WRITE1, 1, 0, 0, 0, sizeof(nt35596_ie_high_cmd69)}, nt35596_ie_high_cmd69},
	{{DTYPE_DCS_WRITE1, 1, 0, 0, 0, sizeof(nt35596_ie_high_cmd70)}, nt35596_ie_high_cmd70},
	{{DTYPE_DCS_WRITE1, 1, 0, 0, 0, sizeof(nt35596_ie_high_cmd71)}, nt35596_ie_high_cmd71},
	{{DTYPE_DCS_WRITE1, 1, 0, 0, 0, sizeof(nt35596_ie_high_cmd72)}, nt35596_ie_high_cmd72},
	{{DTYPE_DCS_WRITE1, 1, 0, 0, 0, sizeof(nt35596_ie_high_cmd73)}, nt35596_ie_high_cmd73},
	{{DTYPE_DCS_WRITE1, 1, 0, 0, 0, sizeof(nt35596_ie_high_cmd74)}, nt35596_ie_high_cmd74},
    #endif
	{{DTYPE_DCS_WRITE1, 1, 0, 0, 0, sizeof(nt35596_ie_cmd01)}, nt35596_ie_cmd01},
	{{DTYPE_DCS_WRITE1, 1, 0, 0, 0, sizeof(nt35596_ie_cmd02)}, nt35596_ie_cmd02},
	{{DTYPE_DCS_WRITE1, 1, 0, 0, 0, sizeof(nt35596_ie_enable_cmd1)}, nt35596_ie_enable_cmd1},
	{{DTYPE_DCS_WRITE1, 1, 0, 0, 0, sizeof(nt35596_ie_enable_cmd2)}, nt35596_ie_enable_cmd2},
};

static int panel_ce_proc_show(struct seq_file *m, void *v)
{
		
	seq_printf(m, "%d\n", current_ce_mode); 
	
	return 0;
}

static int panel_ce_proc_open(struct inode *inode, struct file *file)
{
	return single_open(file, panel_ce_proc_show, NULL);
}

static ssize_t panel_ce_proc_write(struct file *file, const char __user *buffer,
				    size_t count, loff_t *pos)
{	
	char ret;
	int rc;
	int i = 0;

	for (; i < count; i++)
	{
		rc = get_user(ret, buffer);
		if (rc)
		return rc;
		
		if (ret >= '0' && ret <= '9')
			ret = ret - '0';
	}

	if (ret == current_ce_mode)
	{
		mdd_debug("[zhaochenwei]:the current state has been set already!\n");
		return 1;
	}

	if(ret == lcd_ce_OFF)
	{		
		if (isyassy)
		{
			mdd_debug("[zhaochenwei]:yassy pannel ce off!\n");
			//mdss_dsi_cmds_tx(ctrl_pdata,	lg_pannel_off,
				//ARRAY_SIZE(lg_pannel_off));
		}
		else
		{
			mdd_debug("[zhaochenwei]:lg pannel ce off!\n");
			//mdss_dsi_cmds_tx(ctrl_pdata,	lg_pannel_off,
			// 	ARRAY_SIZE(lg_pannel_off));
		}
		current_ce_mode = ret;
	}
	else if(ret == lcd_ce_LOW)
	{
		if (isyassy)
		{
			mdd_debug("[zhaochenwei]:yassy pannel ce low!\n");
			mdss_dsi_cmds_tx(g_ctrl_pdata,	ys_panel_ce_low, 
				ARRAY_SIZE(ys_panel_ce_low));
		}
		else
		{
			mdd_debug("[zhaochenwei]:lg pannel ce low!\n");
			mdss_dsi_cmds_tx(g_ctrl_pdata,	lg_pannel_ce_low,
				ARRAY_SIZE(lg_pannel_ce_low));			
		}
		current_ce_mode = ret;
	}
	else if(ret == lcd_ce_MIDDLE)
	{
		if (isyassy)
		{
			mdd_debug("[zhaochenwei]:yassy pannel ce middle!\n");
			mdss_dsi_cmds_tx(g_ctrl_pdata,	ys_panel_ce_mid, 
				ARRAY_SIZE(ys_panel_ce_mid));
		}
		else
		{
			mdd_debug("[zhaochenwei]:lg pannel ce middle!\n");
			mdss_dsi_cmds_tx(g_ctrl_pdata,	lg_pannel_ce_middle,
				ARRAY_SIZE(lg_pannel_ce_middle));
		}
		current_ce_mode = ret;
	}
	else if(ret == lcd_ce_HIGH)
	{
		if (isyassy)
		{
			mdd_debug("[zhaochenwei]:yassy pannel ce high!\n");
			mdss_dsi_cmds_tx(g_ctrl_pdata,	ys_panel_ce_high, 
				ARRAY_SIZE(ys_panel_ce_high));
		}
		else
		{
			mdd_debug("[zhaochenwei]:lg pannel ce high!\n");
			mdss_dsi_cmds_tx(g_ctrl_pdata,	lg_pannel_ce_high,
				ARRAY_SIZE(lg_pannel_ce_high));
		}
		current_ce_mode = ret;
	}
	

	return 1;
}


static const struct file_operations panel_ce_proc_fops = {
	.open		= panel_ce_proc_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= single_release,
	.write		= panel_ce_proc_write,
};

static int panel_ce_proc_init(void)
{
 	struct proc_dir_entry *res;
	res = proc_create("panel_ce_switch", S_IWUGO | S_IRUGO, NULL,
			  &panel_ce_proc_fops);
	if (!res)
	{
		printk(KERN_INFO "failed to create /proc/panel_ce_switch\n");
		return -ENOMEM;
	}

	printk(KERN_INFO "created /proc/panel_ce_switch\n");
	return 0;
}

//end
/** ZTE_MODIFY end chenfei */ 

static int __devinit mdss_dsi_ctrl_probe(struct platform_device *pdev)
{
	int rc = 0;
	u32 index;
	struct mdss_dsi_ctrl_pdata *ctrl_pdata = NULL;
	struct device_node *dsi_pan_node = NULL;
	char panel_cfg[MDSS_MAX_PANEL_LEN];
	struct resource *mdss_dsi_mres;
	const char *ctrl_name;
	bool cmd_cfg_cont_splash = true;

	if (!mdss_is_ready()) {
		pr_err("%s: MDP not probed yet!\n", __func__);
		return -EPROBE_DEFER;
	}

	if (!pdev->dev.of_node) {
		pr_err("DSI driver only supports device tree probe\n");
		return -ENOTSUPP;
	}

	ctrl_pdata = platform_get_drvdata(pdev);
	if (!ctrl_pdata) {
		ctrl_pdata = devm_kzalloc(&pdev->dev,
					  sizeof(struct mdss_dsi_ctrl_pdata),
					  GFP_KERNEL);
		if (!ctrl_pdata) {
			pr_err("%s: FAILED: cannot alloc dsi ctrl\n",
			       __func__);
			rc = -ENOMEM;
			goto error_no_mem;
		}
		platform_set_drvdata(pdev, ctrl_pdata);
	}

	ctrl_name = of_get_property(pdev->dev.of_node, "label", NULL);
	if (!ctrl_name)
		pr_info("%s:%d, DSI Ctrl name not specified\n",
			__func__, __LINE__);
	else
		pr_info("%s: DSI Ctrl name = %s\n",
			__func__, ctrl_name);

	rc = of_property_read_u32(pdev->dev.of_node,
				  "cell-index", &index);
	if (rc) {
		dev_err(&pdev->dev,
			"%s: Cell-index not specified, rc=%d\n",
			__func__, rc);
		goto error_no_mem;
	}

	if (index == 0)
		pdev->id = 1;
	else
		pdev->id = 2;

	mdss_dsi_mres = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!mdss_dsi_mres) {
		pr_err("%s:%d unable to get the MDSS resources",
		       __func__, __LINE__);
		rc = -ENOMEM;
		goto error_no_mem;
	}

	mdss_dsi_base = ioremap(mdss_dsi_mres->start,
				resource_size(mdss_dsi_mres));
	if (!mdss_dsi_base) {
		pr_err("%s:%d unable to remap dsi resources",
		       __func__, __LINE__);
		rc = -ENOMEM;
		goto error_no_mem;
	}

	rc = of_platform_populate(pdev->dev.of_node,
				  NULL, NULL, &pdev->dev);
	if (rc) {
		dev_err(&pdev->dev,
			"%s: failed to add child nodes, rc=%d\n",
			__func__, rc);
		goto error_ioremap;
	}

	/* Parse the regulator information */
	rc = mdss_dsi_get_dt_vreg_data(&pdev->dev,
				       &ctrl_pdata->power_data);
	if (rc) {
		pr_err("%s: failed to get vreg data from dt. rc=%d\n",
		       __func__, rc);
		goto error_vreg;
	}

	/* DSI panels can be different between controllers */
	rc = mdss_dsi_get_panel_cfg(panel_cfg);
	if (!rc)
		/* dsi panel cfg not present */
		pr_warn("%s:%d:dsi specific cfg not present\n",
			__func__, __LINE__);

	/* find panel device node */
	dsi_pan_node = mdss_dsi_find_panel_of_node(pdev, panel_cfg);
	if (!dsi_pan_node) {
		pr_err("%s: can't find panel node %s\n", __func__, panel_cfg);
		goto error_pan_node;
	}

	cmd_cfg_cont_splash = mdss_panel_get_boot_cfg() ? true : false;

	rc = mdss_dsi_panel_init(dsi_pan_node, ctrl_pdata, cmd_cfg_cont_splash);
	if (rc) {
		pr_err("%s: dsi panel init failed\n", __func__);
		goto error_pan_node;
	}

	rc = dsi_panel_device_register(dsi_pan_node, ctrl_pdata);
	if (rc) {
		pr_err("%s: dsi panel dev reg failed\n", __func__);
		goto error_pan_node;
	}

	pr_debug("%s: Dsi Ctrl->%d initialized\n", __func__, index);

	/** ZTE_MODIFY chenfei added for LCD driver 2013/05/21 */ 
	if (index == 0)
	{
		panel_ce_proc_init();
		g_ctrl_pdata = ctrl_pdata;
	}
	/** ZTE_MODIFY end chenfei */ 
	return 0;

error_pan_node:
	of_node_put(dsi_pan_node);
error_vreg:
	mdss_dsi_put_dt_vreg_data(&pdev->dev, &ctrl_pdata->power_data);
error_ioremap:
	iounmap(mdss_dsi_base);
error_no_mem:
	devm_kfree(&pdev->dev, ctrl_pdata);

	return rc;
}

static int __devexit mdss_dsi_ctrl_remove(struct platform_device *pdev)
{
	struct msm_fb_data_type *mfd;
	struct mdss_dsi_ctrl_pdata *ctrl_pdata = platform_get_drvdata(pdev);

	if (!ctrl_pdata) {
		pr_err("%s: no driver data\n", __func__);
		return -ENODEV;
	}

	if (msm_dss_config_vreg(&pdev->dev,
			ctrl_pdata->power_data.vreg_config,
			ctrl_pdata->power_data.num_vreg, 1) < 0)
		pr_err("%s: failed to de-init vregs\n", __func__);
	mdss_dsi_put_dt_vreg_data(&pdev->dev, &ctrl_pdata->power_data);
	mfd = platform_get_drvdata(pdev);
	iounmap(mdss_dsi_base);
	return 0;
}

struct device dsi_dev;

int mdss_dsi_retrieve_ctrl_resources(struct platform_device *pdev, int mode,
			struct mdss_dsi_ctrl_pdata *ctrl)
{
	int rc = 0;
	u32 index;
	struct resource *mdss_dsi_mres;

	rc = of_property_read_u32(pdev->dev.of_node, "cell-index", &index);
	if (rc) {
		dev_err(&pdev->dev,
			"%s: Cell-index not specified, rc=%d\n",
						__func__, rc);
		return rc;
	}

	if (index == 0) {
		if (mode != DISPLAY_1) {
			pr_err("%s:%d Panel->Ctrl mapping is wrong",
				       __func__, __LINE__);
			return -EPERM;
		}
	} else if (index == 1) {
		if (mode != DISPLAY_2) {
			pr_err("%s:%d Panel->Ctrl mapping is wrong",
				       __func__, __LINE__);
			return -EPERM;
		}
	} else {
		pr_err("%s:%d Unknown Ctrl mapped to panel",
			       __func__, __LINE__);
		return -EPERM;
	}

	mdss_dsi_mres = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!mdss_dsi_mres) {
		pr_err("%s:%d unable to get the DSI ctrl resources",
			       __func__, __LINE__);
		return -ENOMEM;
	}

	ctrl->ctrl_base = ioremap(mdss_dsi_mres->start,
		resource_size(mdss_dsi_mres));
	if (!(ctrl->ctrl_base)) {
		pr_err("%s:%d unable to remap dsi resources",
			       __func__, __LINE__);
		return -ENOMEM;
	}

	ctrl->reg_size = resource_size(mdss_dsi_mres);

	pr_info("%s: dsi base=%x size=%x\n",
		__func__, (int)ctrl->ctrl_base, ctrl->reg_size);

	return 0;
}

int dsi_panel_device_register(struct device_node *pan_node,
				struct mdss_dsi_ctrl_pdata *ctrl_pdata)
{
	struct mipi_panel_info *mipi;
	int rc, i, len;
	struct device_node *dsi_ctrl_np = NULL;
	struct platform_device *ctrl_pdev = NULL;
	bool dynamic_fps;
	const char *data;
	struct mdss_panel_info *pinfo = &(ctrl_pdata->panel_data.panel_info);

	mipi  = &(pinfo->mipi);

	pinfo->type =
		((mipi->mode == DSI_VIDEO_MODE)
			? MIPI_VIDEO_PANEL : MIPI_CMD_PANEL);

	rc = mdss_dsi_clk_div_config(pinfo, mipi->frame_rate);
	if (rc) {
		pr_err("%s: unable to initialize the clk dividers\n", __func__);
		return rc;
	}

	dsi_ctrl_np = of_parse_phandle(pan_node,
				"qcom,mdss-dsi-panel-controller", 0);
	if (!dsi_ctrl_np) {
		pr_err("%s: Dsi controller node not initialized\n", __func__);
		return -EPROBE_DEFER;
	}

	ctrl_pdev = of_find_device_by_node(dsi_ctrl_np);

	rc = mdss_dsi_regulator_init(ctrl_pdev);
	if (rc) {
		pr_err("%s: failed to init regulator, rc=%d\n",
						__func__, rc);
		return rc;
	}

	data = of_get_property(ctrl_pdev->dev.of_node,
		"qcom,platform-strength-ctrl", &len);
	if ((!data) || (len != 2)) {
		pr_err("%s:%d, Unable to read Phy Strength ctrl settings",
			__func__, __LINE__);
		return -EINVAL;
	}
	pinfo->mipi.dsi_phy_db.strength[0] = data[0];
	pinfo->mipi.dsi_phy_db.strength[1] = data[1];

	data = of_get_property(ctrl_pdev->dev.of_node,
		"qcom,platform-regulator-settings", &len);
	if ((!data) || (len != 7)) {
		pr_err("%s:%d, Unable to read Phy regulator settings",
			__func__, __LINE__);
		return -EINVAL;
	}
	for (i = 0; i < len; i++) {
		pinfo->mipi.dsi_phy_db.regulator[i]
			= data[i];
	}

	data = of_get_property(ctrl_pdev->dev.of_node,
		"qcom,platform-bist-ctrl", &len);
	if ((!data) || (len != 6)) {
		pr_err("%s:%d, Unable to read Phy Bist Ctrl settings",
			__func__, __LINE__);
		return -EINVAL;
	}
	for (i = 0; i < len; i++) {
		pinfo->mipi.dsi_phy_db.bistctrl[i]
			= data[i];
	}

	data = of_get_property(ctrl_pdev->dev.of_node,
		"qcom,platform-lane-config", &len);
	if ((!data) || (len != 45)) {
		pr_err("%s:%d, Unable to read Phy lane configure settings",
			__func__, __LINE__);
		return -EINVAL;
	}
	for (i = 0; i < len; i++) {
		pinfo->mipi.dsi_phy_db.lanecfg[i] =
			data[i];
	}

	ctrl_pdata->shared_pdata.broadcast_enable = of_property_read_bool(
		pan_node, "qcom,mdss-dsi-panel-broadcast-mode");

	dynamic_fps = of_property_read_bool(pan_node,
					  "qcom,mdss-dsi-pan-enable-dynamic-fps");
	if (dynamic_fps) {
		pinfo->dynamic_fps = true;
		data = of_get_property(pan_node,
					  "qcom,mdss-dsi-pan-fps-update", NULL);
		if (data) {
			if (!strcmp(data, "dfps_suspend_resume_mode")) {
				pinfo->dfps_update =
						DFPS_SUSPEND_RESUME_MODE;
				pr_debug("%s: dfps mode: suspend/resume\n",
								__func__);
			} else if (!strcmp(data,
					    "dfps_immediate_clk_mode")) {
				pinfo->dfps_update =
						DFPS_IMMEDIATE_CLK_UPDATE_MODE;
				pr_debug("%s: dfps mode: Immediate clk\n",
								__func__);
			} else {
				pr_debug("%s: dfps to default mode\n",
								__func__);
				pinfo->dfps_update =
						DFPS_SUSPEND_RESUME_MODE;
				pr_debug("%s: dfps mode: suspend/resume\n",
								__func__);
			}
		} else {
			pr_debug("%s: dfps update mode not configured\n",
								__func__);
				pinfo->dynamic_fps =
								false;
				pr_debug("%s: dynamic FPS disabled\n",
								__func__);
		}
		pinfo->new_fps = pinfo->mipi.frame_rate;
	}

	ctrl_pdata->disp_en_gpio = of_get_named_gpio(ctrl_pdev->dev.of_node,
		"qcom,platform-enable-gpio", 0);

	if (!gpio_is_valid(ctrl_pdata->disp_en_gpio)) {
		pr_err("%s:%d, Disp_en gpio not specified\n",
						__func__, __LINE__);
	} else {
		rc = gpio_request(ctrl_pdata->disp_en_gpio, "disp_enable");
		if (rc) {
			pr_err("request reset gpio failed, rc=%d\n",
			       rc);
			gpio_free(ctrl_pdata->disp_en_gpio);
			return -ENODEV;
		}
	}

	if (pinfo->type == MIPI_CMD_PANEL) {
		ctrl_pdata->disp_te_gpio = of_get_named_gpio(ctrl_pdev->dev.of_node,
						"qcom,platform-te-gpio", 0);
		if (!gpio_is_valid(ctrl_pdata->disp_te_gpio)) {
			pr_err("%s:%d, Disp_te gpio not specified\n",
						__func__, __LINE__);
		}
	}

	if (gpio_is_valid(ctrl_pdata->disp_te_gpio)) {
		rc = gpio_request(ctrl_pdata->disp_te_gpio, "disp_te");
		if (rc) {
			pr_err("request TE gpio failed, rc=%d\n",
			       rc);
			gpio_free(ctrl_pdata->disp_te_gpio);
			return -ENODEV;
		}
		rc = gpio_tlmm_config(GPIO_CFG(
				ctrl_pdata->disp_te_gpio, 1,
				GPIO_CFG_INPUT,
				GPIO_CFG_PULL_DOWN,
				GPIO_CFG_2MA),
				GPIO_CFG_ENABLE);

		if (rc) {
			pr_err("%s: unable to config tlmm = %d\n",
				__func__, ctrl_pdata->disp_te_gpio);
			gpio_free(ctrl_pdata->disp_te_gpio);
			return -ENODEV;
		}

		rc = gpio_direction_input(ctrl_pdata->disp_te_gpio);
		if (rc) {
			pr_err("set_direction for disp_en gpio failed, rc=%d\n",
			       rc);
			gpio_free(ctrl_pdata->disp_te_gpio);
			return -ENODEV;
		}
		pr_debug("%s: te_gpio=%d\n", __func__,
					ctrl_pdata->disp_te_gpio);
	}

	ctrl_pdata->rst_gpio = of_get_named_gpio(ctrl_pdev->dev.of_node,
			 "qcom,platform-reset-gpio", 0);
	if (!gpio_is_valid(ctrl_pdata->rst_gpio)) {
		pr_err("%s:%d, reset gpio not specified\n",
						__func__, __LINE__);
	} else {
		rc = gpio_request(ctrl_pdata->rst_gpio, "disp_rst_n");
		if (rc) {
			pr_err("request reset gpio failed, rc=%d\n",
				rc);
			gpio_free(ctrl_pdata->rst_gpio);
			if (gpio_is_valid(ctrl_pdata->disp_en_gpio))
				gpio_free(ctrl_pdata->disp_en_gpio);
			return -ENODEV;
		}
	}

	if (pinfo->mode_gpio_state != MODE_GPIO_NOT_VALID) {

		ctrl_pdata->mode_gpio = of_get_named_gpio(
					ctrl_pdev->dev.of_node,
					"qcom,platform-mode-gpio", 0);
		if (!gpio_is_valid(ctrl_pdata->mode_gpio)) {
			pr_info("%s:%d, mode gpio not specified\n",
							__func__, __LINE__);
		} else {
			rc = gpio_request(ctrl_pdata->mode_gpio, "panel_mode");
			if (rc) {
				pr_err("request panel mode gpio failed,rc=%d\n",
									rc);
				gpio_free(ctrl_pdata->mode_gpio);
				if (gpio_is_valid(ctrl_pdata->disp_en_gpio))
					gpio_free(ctrl_pdata->disp_en_gpio);
				if (gpio_is_valid(ctrl_pdata->rst_gpio))
					gpio_free(ctrl_pdata->rst_gpio);
				if (gpio_is_valid(ctrl_pdata->disp_te_gpio))
					gpio_free(ctrl_pdata->disp_te_gpio);
				return -ENODEV;
			}
		}
	}

	if (mdss_dsi_clk_init(ctrl_pdev, ctrl_pdata)) {
		pr_err("%s: unable to initialize Dsi ctrl clks\n", __func__);
		return -EPERM;
	}

	if (mdss_dsi_retrieve_ctrl_resources(ctrl_pdev,
					     pinfo->pdest,
					     ctrl_pdata)) {
		pr_err("%s: unable to get Dsi controller res\n", __func__);
		return -EPERM;
	}

	ctrl_pdata->panel_data.event_handler = mdss_dsi_event_handler;

	if (ctrl_pdata->bklt_ctrl == BL_PWM)
		mdss_dsi_panel_pwm_cfg(ctrl_pdata);

	mdss_dsi_ctrl_init(ctrl_pdata);
	/*
	 * register in mdp driver
	 */

	ctrl_pdata->pclk_rate = mipi->dsi_pclk_rate;
	ctrl_pdata->byte_clk_rate = pinfo->clk_rate / 8;
	pr_debug("%s: pclk=%d, bclk=%d\n", __func__,
			ctrl_pdata->pclk_rate, ctrl_pdata->byte_clk_rate);

	ctrl_pdata->ctrl_state = CTRL_STATE_UNKNOWN;

	if (pinfo->cont_splash_enabled) {
		pinfo->panel_power_on = 1;
		rc = mdss_dsi_panel_power_on(&(ctrl_pdata->panel_data), 1);
		if (rc) {
			pr_err("%s: Panel power on failed\n", __func__);
			return rc;
		}

		mdss_dsi_clk_ctrl(ctrl_pdata, 1);
		ctrl_pdata->ctrl_state |=
			(CTRL_STATE_PANEL_INIT | CTRL_STATE_MDP_ACTIVE);
	} else {
		pinfo->panel_power_on = 0;
	}

	rc = mdss_register_panel(ctrl_pdev, &(ctrl_pdata->panel_data));
	if (rc) {
		pr_err("%s: unable to register MIPI DSI panel\n", __func__);
		if (ctrl_pdata->rst_gpio)
			gpio_free(ctrl_pdata->rst_gpio);
		if (gpio_is_valid(ctrl_pdata->disp_en_gpio))
			gpio_free(ctrl_pdata->disp_en_gpio);
		return rc;
	}

	if (pinfo->pdest == DISPLAY_1) {
		mdss_debug_register_base("dsi0",
			ctrl_pdata->ctrl_base, ctrl_pdata->reg_size);
		ctrl_pdata->ndx = 0;
	} else {
		mdss_debug_register_base("dsi1",
			ctrl_pdata->ctrl_base, ctrl_pdata->reg_size);
		ctrl_pdata->ndx = 1;
	}

	pr_debug("%s: Panel data initialized\n", __func__);
	return 0;
}

static const struct of_device_id mdss_dsi_ctrl_dt_match[] = {
	{.compatible = "qcom,mdss-dsi-ctrl"},
	{}
};
MODULE_DEVICE_TABLE(of, mdss_dsi_ctrl_dt_match);

static struct platform_driver mdss_dsi_ctrl_driver = {
	.probe = mdss_dsi_ctrl_probe,
	.remove = __devexit_p(mdss_dsi_ctrl_remove),
	.shutdown = NULL,
	.driver = {
		.name = "mdss_dsi_ctrl",
		.of_match_table = mdss_dsi_ctrl_dt_match,
	},
};

static int mdss_dsi_register_driver(void)
{
	return platform_driver_register(&mdss_dsi_ctrl_driver);
}

static int __init mdss_dsi_driver_init(void)
{
	int ret;

	ret = mdss_dsi_register_driver();
	if (ret) {
		pr_err("mdss_dsi_register_driver() failed!\n");
		return ret;
	}

	return ret;
}
module_init(mdss_dsi_driver_init);

static void __exit mdss_dsi_driver_cleanup(void)
{
	iounmap(mdss_dsi_base);
	platform_driver_unregister(&mdss_dsi_ctrl_driver);
}
module_exit(mdss_dsi_driver_cleanup);

MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("DSI controller driver");
MODULE_AUTHOR("Chandan Uddaraju <chandanu@codeaurora.org>");

/* Copyright (c) 2009-2013, The Linux Foundation. All rights reserved.
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

 
 
#define pr_fmt(fmt) "%s:%d " fmt, __func__, __LINE__

#include "msm_led_flash.h"
#include "msm_cci.h"
#include "msm_camera_io_util.h"

/*#define CONFIG_MSMB_CAMERA_DEBUG*/
#undef CDBG
#ifdef CONFIG_MSMB_CAMERA_DEBUG
#define CDBG(fmt, args...) pr_err(fmt, ##args)
#else
#define CDBG(fmt, args...) do { } while (0)
#endif

static long msm_led_flash_subdev_ioctl(struct v4l2_subdev *sd,
	unsigned int cmd, void *arg)
{
	struct msm_led_flash_ctrl_t *fctrl = NULL;
	void __user *argp = (void __user *)arg;
	if (!sd) {
		pr_err("sd NULL\n");
		return -EINVAL;
	}
	fctrl = v4l2_get_subdevdata(sd);
	if (!fctrl) {
		pr_err("fctrl NULL\n");
		return -EINVAL;
	}
	switch (cmd) {
	case VIDIOC_MSM_SENSOR_GET_SUBDEV_ID:
		return fctrl->func_tbl->flash_get_subdev_id(fctrl, argp);
	case VIDIOC_MSM_FLASH_LED_DATA_CFG:
		return fctrl->func_tbl->flash_led_config(fctrl, argp);
	default:
		pr_err("invalid cmd %d\n", cmd);
		return -ENOIOCTLCMD;
	}
}


static struct v4l2_subdev_core_ops msm_flash_subdev_core_ops = {
	.ioctl = msm_led_flash_subdev_ioctl,
};

static struct v4l2_subdev_ops msm_flash_subdev_ops = {
	.core = &msm_flash_subdev_core_ops,
};

static int msm_led_flash_open(struct v4l2_subdev *sd,
	struct v4l2_subdev_fh *fh) {
	
	int rc = 0;
	struct msm_led_flash_ctrl_t *a_ctrl =  v4l2_get_subdevdata(sd);
	printk("liyibo Enter %s %d\n" ,__func__,__LINE__);
	if (!a_ctrl) {
		printk("failed\n");
		return -EINVAL;
	}
	
	if (a_ctrl->led_device_type == MSM_CAMERA_PLATFORM_DEVICE) {
		rc = a_ctrl->flash_i2c_client.i2c_func_tbl->i2c_util(
			&a_ctrl->flash_i2c_client, MSM_CCI_INIT);
		if (rc < 0)
			printk("cci_init failed\n");
	}

	printk("liyibo%s  Exit %d  rc = %d\n" , __func__ ,__LINE__ ,rc);
	return rc;
}

static int msm_led_flash_close(struct v4l2_subdev *sd,
	struct v4l2_subdev_fh *fh) {
	int rc = 0;
	struct msm_led_flash_ctrl_t *a_ctrl =  v4l2_get_subdevdata(sd);
	printk("liyibo Enter %s %d\n" ,__func__,__LINE__);
	if (!a_ctrl) {
		printk("failed\n");
		return -EINVAL;
	}
	printk("liyibo Enter %s %d\n" ,__func__,__LINE__);
	if (a_ctrl->led_device_type == MSM_CAMERA_PLATFORM_DEVICE) {
		rc = a_ctrl->flash_i2c_client.i2c_func_tbl->i2c_util(
			&a_ctrl->flash_i2c_client, MSM_CCI_RELEASE);
		if (rc < 0)
			printk("cci_init failed\n");
	}
	printk("liyibo%s  Exit %d\n" , __func__ ,__LINE__);
	return rc;
}

static const struct v4l2_subdev_internal_ops msm_flash_internal_ops = {
	.open = msm_led_flash_open,
	.close = msm_led_flash_close,
};

int32_t msm_led_flash_create_v4lsubdev(struct platform_device *pdev, struct msm_led_flash_ctrl_t *fctrl)
{
	CDBG("Enter\n");

	if (!fctrl) {
		pr_err("fctrl NULL\n");
		return -EINVAL;
	}

	/* Initialize sub device */
	v4l2_subdev_init(&fctrl->msm_sd.sd, &msm_flash_subdev_ops);
	v4l2_set_subdevdata(&fctrl->msm_sd.sd, fctrl);

	fctrl->pdev = pdev;
	fctrl->msm_sd.sd.internal_ops = &msm_flash_internal_ops;
	fctrl->msm_sd.sd.flags |= V4L2_SUBDEV_FL_HAS_DEVNODE;
	snprintf(fctrl->msm_sd.sd.name, ARRAY_SIZE(fctrl->msm_sd.sd.name),
		"msm_flash");
	media_entity_init(&fctrl->msm_sd.sd.entity, 0, NULL, 0);
	fctrl->msm_sd.sd.entity.type = MEDIA_ENT_T_V4L2_SUBDEV;
	fctrl->msm_sd.sd.entity.group_id = MSM_CAMERA_SUBDEV_LED_FLASH;
	msm_sd_register(&fctrl->msm_sd);

	CDBG("probe success\n");
	return 0;
}
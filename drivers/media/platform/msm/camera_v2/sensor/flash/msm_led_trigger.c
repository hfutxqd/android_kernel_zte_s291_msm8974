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
 
 

#define pr_fmt(fmt) "%s:%d " fmt, __func__, __LINE__

#include <linux/module.h>
#include "msm_led_flash.h"
/* ZTE_MODIFY : added  begin by liyibo 2013-6-18 */
#include "msm_cci.h"
#include "msm_camera_io_util.h"

DEFINE_MSM_MUTEX(msm_led_flash_mutex);
/* ZTE_MODIFY : modify end by liyibo 2013-6-18 */
/*#define CONFIG_MSMB_CAMERA_DEBUG*/
#undef CDBG
#ifdef CONFIG_MSMB_CAMERA_DEBUG
#define CDBG(fmt, args...) pr_err(fmt, ##args)
#else
#define CDBG(fmt, args...) do { } while (0)
#endif

extern int32_t msm_led_torch_create_classdev(
				struct platform_device *pdev, void *data);

//static struct msm_led_flash_ctrl_t fctrl; /* ZTE_MODIFY : deleted by liyibo 2013-6-18 */

static int32_t msm_led_trigger_get_subdev_id(struct msm_led_flash_ctrl_t *fctrl,
	void *arg)
{
	uint32_t *subdev_id = (uint32_t *)arg;
	if (!subdev_id) {
		pr_err("%s:%d failed\n", __func__, __LINE__);
		return -EINVAL;
	}
	*subdev_id = fctrl->pdev->id;
	CDBG("%s:%d subdev_id %d\n", __func__, __LINE__, *subdev_id);
	return 0;
}

static int32_t msm_led_trigger_config(struct msm_led_flash_ctrl_t *fctrl,
	void *data)
{
	int rc = 0;
	struct msm_camera_led_cfg_t *cfg = (struct msm_camera_led_cfg_t *)data;
	CDBG("called led_state %d\n", cfg->cfgtype);

/* ZTE_MODIFY : modify begin by liyibo 2013-6-18 */


	switch (cfg->cfgtype) {
	case MSM_CAMERA_LED_OFF:
	  rc = fctrl->flash_i2c_client.i2c_func_tbl->i2c_write(&fctrl->flash_i2c_client, 
		       0x0A, 0x00 , MSM_CAMERA_I2C_BYTE_DATA);
		break;

	case MSM_CAMERA_LED_LOW:
		/** ZTE_MODIFY zhangwenna added for preflash and torch 2013.6.19*/
		rc = fctrl->flash_i2c_client.i2c_func_tbl->i2c_write(&fctrl->flash_i2c_client, 
		       0x09, 0x7A , MSM_CAMERA_I2C_BYTE_DATA);
		rc = fctrl->flash_i2c_client.i2c_func_tbl->i2c_write(&fctrl->flash_i2c_client, 
		       0x0A, 0x22 , MSM_CAMERA_I2C_BYTE_DATA);
		/** ZTE_MODIFY zhangwenna added end*/
		break;

	case MSM_CAMERA_LED_HIGH:
       /** ZTE_MODIFY zhangwenna added for camera in low light 2013.6.19*/
     		 rc = fctrl->flash_i2c_client.i2c_func_tbl->i2c_write(&fctrl->flash_i2c_client, 
		       0x08, 0x7f , MSM_CAMERA_I2C_BYTE_DATA);
     		 rc = fctrl->flash_i2c_client.i2c_func_tbl->i2c_write(&fctrl->flash_i2c_client, 
		       0x09, 0x7F , MSM_CAMERA_I2C_BYTE_DATA);
       /** ZTE_MODIFY zhangwenna added end*/
    		rc = fctrl->flash_i2c_client.i2c_func_tbl->i2c_write(&fctrl->flash_i2c_client, 
		       0x0A, 0x23 , MSM_CAMERA_I2C_BYTE_DATA);
		printk("liyibo %s  %d rc = %d \n" , __func__ ,__LINE__ , rc);
		break;

	case MSM_CAMERA_LED_INIT:
	case MSM_CAMERA_LED_RELEASE:
		break;
/* ZTE_MODIFY : modify end by liyibo 2013-6-18 */
	default:
		rc = -EFAULT;
		break;
	}
	CDBG("flash_set_led_state: return %d\n", rc);
	return rc;
}
/* ZTE_MODIFY : added begin by liyibo 2013-6-18 */
static int32_t msm_led_get_dt_vreg_data(struct device_node *of_node, struct camera_vreg_t *cam_vreg)
{
	int32_t rc = 0, i = 0;
	uint32_t count = 0;
	uint32_t vreg_type = 0;

	count = of_property_count_strings(of_node, "qcom,cam-vreg-name");
	CDBG("%s qcom,cam-vreg-name count %d\n", __func__, count);

	if (!count)
		return 0;



	rc = of_property_read_string_index(of_node, "qcom,cam-vreg-name", i,
			&cam_vreg->reg_name);
	printk("%s reg_nam = %s\n", __func__, cam_vreg->reg_name);
	if (rc < 0) {
		pr_err("%s failed %d\n", __func__, __LINE__);
		return rc;
	}
	
	rc = of_property_read_u32_array(of_node, "qcom,cam-vreg-type",
		&vreg_type, count);
	if (rc < 0) {
		pr_err("%s failed %d\n", __func__, __LINE__);
		return rc;
	}

	cam_vreg->type = vreg_type;
	printk("%s cam_vreg->type = %d\n", __func__, (int)cam_vreg->type);
	
	return rc;
}

static struct msm_camera_i2c_fn_t msm_led_cci_func_tbl = {
	.i2c_read = msm_camera_cci_i2c_read,
	.i2c_read_seq = msm_camera_cci_i2c_read_seq,
	.i2c_write = msm_camera_cci_i2c_write,
	.i2c_write_table = msm_camera_cci_i2c_write_table,
	.i2c_write_seq_table = msm_camera_cci_i2c_write_seq_table,
	.i2c_write_table_w_microdelay =
		msm_camera_cci_i2c_write_table_w_microdelay,
	.i2c_util = msm_sensor_cci_i2c_util,
};
/* ZTE_MODIFY : added end by liyibo 2013-6-18 */

static const struct of_device_id msm_led_trigger_dt_match[] = {
	{.compatible = "qcom,lm3642", .data = NULL},   /* ZTE_MODIFY : added begin by liyibo 2013-6-18 */
	{}
};
/* ZTE_MODIFY : modify begin by liyibo 2013-6-18 */
#if 0
MODULE_DEVICE_TABLE(of, msm_led_trigger_dt_match);

static struct platform_driver msm_led_trigger_driver = {
	.driver = {
		.name = FLASH_NAME,
		.owner = THIS_MODULE,
		.of_match_table = msm_led_trigger_dt_match,
	},
};
#endif

static struct msm_flash_fn_t msm_led_trigger_func_tbl = {
	.flash_get_subdev_id = msm_led_trigger_get_subdev_id,
	.flash_led_config = msm_led_trigger_config,
};
/* ZTE_MODIFY : modify end by liyibo 2013-6-18 */

static int32_t msm_led_trigger_probe(struct platform_device *pdev)
{
	int32_t rc = 0;
//	const struct of_device_id *match;
	struct msm_camera_cci_client *cci_client = NULL;
	struct msm_led_flash_ctrl_t *s_ctrl;
	printk("liyibo Enter %s %d\n" ,__func__,__LINE__);

      	if (!pdev->dev.of_node) {		
	       pr_err("of_node NULL\n");		
		return -EINVAL;	
	}

	s_ctrl = kzalloc(sizeof(struct msm_led_flash_ctrl_t), GFP_KERNEL);
	if(!s_ctrl){
		pr_err("%s:%d failed no memory\n", __func__, __LINE__);
		return -ENOMEM;
	}
  
       s_ctrl->pdev = pdev;
       s_ctrl->dev = &pdev->dev;
	if (!pdev->dev.of_node) {
		printk("of_node NULL\n");
		return -EINVAL;
	}
	rc = of_property_read_u32((&pdev->dev)->of_node, "cell-index",
		&pdev->id);
	printk("liyibo cell-index %d, rc %d\n", pdev->id, rc);
	if (rc < 0) {
		printk("failed rc %d\n", rc);
		return rc;
	}

	rc = of_property_read_u32((&pdev->dev)->of_node, "qcom,cci-master",
		&s_ctrl->cci_master);
	printk("liyibo qcom,cci-master %d, rc %d\n", s_ctrl->cci_master, rc);
	if (rc < 0) {
		printk("failed rc %d\n", rc);
		return rc;
	}

	msm_led_get_dt_vreg_data((&pdev->dev)->of_node, &s_ctrl->cam_vreg);
	
	/* Set device type as platform device */
	s_ctrl->led_device_type = MSM_CAMERA_PLATFORM_DEVICE;
	s_ctrl->flash_i2c_client.cci_client = kzalloc(sizeof(
		struct msm_camera_cci_client), GFP_KERNEL);
	if (!s_ctrl->flash_i2c_client.cci_client) {
		printk("failed no memory\n");
		return -ENOMEM;
	}

	/*rc = msm_led_flash_create_v4lsubdev(pdev, &fctrl);
	if (!rc)
		msm_led_torch_create_classdev(pdev, &fctrl);*/
	
	cci_client = s_ctrl->flash_i2c_client.cci_client;
	cci_client->cci_subdev = msm_cci_get_subdev();
	cci_client->cci_i2c_master = s_ctrl->cci_master;
//	cci_client->sid = 0x63 >> 1;
	cci_client->sid = 0x63;
	cci_client->retries = 3;
	cci_client->id_map = 0;

 	s_ctrl->flash_i2c_client.i2c_func_tbl = &msm_led_cci_func_tbl;
	s_ctrl->flash_i2c_client.addr_type = MSM_CAMERA_I2C_BYTE_ADDR;
 
	s_ctrl->led_flash_mutex = &msm_led_flash_mutex;
//	s_ctrl->cam_name = pdev->id;

	/* Set platform device handle */
	s_ctrl->pdev = pdev;

       s_ctrl->func_tbl = &msm_led_trigger_func_tbl;
	//msm_led_trigger_power_up(s_ctrl);	/* ZTE_MODIFY deleted by liyibo for led flash 2013-07-17*/
	rc = msm_led_flash_create_v4lsubdev(pdev, s_ctrl);

	return rc;
}

MODULE_DEVICE_TABLE(of, msm_led_trigger_dt_match);

static struct platform_driver msm_led_trigger_driver = {
	.driver = {
		.name = "qcom,lm3642",
		.owner = THIS_MODULE,
		.of_match_table = msm_led_trigger_dt_match,
	},
};
/* ZTE_MODIFY : deleted end by liyibo 2013-6-18 */
static int __init msm_led_trigger_add_driver(void)
{
	CDBG("called\n");
	return platform_driver_probe(&msm_led_trigger_driver,
		msm_led_trigger_probe);
}
/* ZTE_MODIFY : deleted begin by liyibo 2013-6-18  */
#if 0
static struct msm_flash_fn_t msm_led_trigger_func_tbl = {
	.flash_get_subdev_id = msm_led_trigger_get_subdev_id,
	.flash_led_config = msm_led_trigger_config,
};

static struct msm_led_flash_ctrl_t fctrl = {
	.func_tbl = &msm_led_trigger_func_tbl,
};
#endif
/* ZTE_MODIFY : deleted end by liyibo 2013-6-18  */
module_init(msm_led_trigger_add_driver);
MODULE_DESCRIPTION("LED TRIGGER FLASH");
MODULE_LICENSE("GPL v2");

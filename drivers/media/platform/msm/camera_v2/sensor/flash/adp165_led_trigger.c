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
#include "msm_cci.h"
#include "msm_camera_io_util.h"

DEFINE_MSM_MUTEX(msm_led_flash_mutex);
/*#define CONFIG_MSMB_CAMERA_DEBUG*/
#undef CDBG
#ifdef CONFIG_MSMB_CAMERA_DEBUG
#define CDBG(fmt, args...) pr_err(fmt, ##args)
#else
#define CDBG(fmt, args...) do { } while (0)
#endif

#define DEVICE_ID_REG 0x0
#define VERG_TIMER_REG 0x02
#define CURRENT_SET_REG 0x03
#define OUTPUT_MODE_REG 0x04
#define ADP165_ID 0x22
#define FLASH_MODE 1
#define TORCH_MODE 2
#define ADP165_FLASH_LED_EN 13
#define ADP165_FLASH_STROBE 24
#define ADP165_FLASH_TORCH_EN 79
static int32_t adp165_flash_trigger_get_subdev_id(struct msm_led_flash_ctrl_t *fctrl,
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

static int adp165_set_off(struct msm_led_flash_ctrl_t *fctrl)
{
	int rc = 0;
	uint16_t output_mode = 0;

	///set  led mode to standby mode and disable output_en 
	rc = fctrl->flash_i2c_client.i2c_func_tbl->i2c_read(&fctrl->flash_i2c_client, 
			OUTPUT_MODE_REG, &output_mode, MSM_CAMERA_I2C_BYTE_DATA);
	output_mode &= 0xfc;
	output_mode &= ~(1 << 3);
	rc = fctrl->flash_i2c_client.i2c_func_tbl->i2c_write(&fctrl->flash_i2c_client, 
		       OUTPUT_MODE_REG, output_mode , MSM_CAMERA_I2C_BYTE_DATA);

	return rc;

	
}
static int adp165_set_current(struct msm_led_flash_ctrl_t *a_ctrl, int value, int led_mode)
{
	uint16_t current_set = 0;
	int rc = 0;

	rc = a_ctrl->flash_i2c_client.i2c_func_tbl->i2c_read(&a_ctrl->flash_i2c_client, 
			CURRENT_SET_REG, &current_set, MSM_CAMERA_I2C_BYTE_DATA);
		
	if(FLASH_MODE == led_mode) {
		if(value > 0x18) {
			value = 0x18;
			printk("chengjiatest: flash current value is too high to support, just max it\n");
		}
		current_set &= 0x07;
		current_set |= (value << 3);
	}
	else if(TORCH_MODE == led_mode) {
		if(value > 0x7) {
			value = 0x7;
			printk("chengjiatest: torch current value is too high to support, just max it\n");	
		}
		current_set &= 0xf8;
		current_set |= value;
	}
	rc = a_ctrl->flash_i2c_client.i2c_func_tbl->i2c_write(&a_ctrl->flash_i2c_client, 
		       CURRENT_SET_REG, current_set , MSM_CAMERA_I2C_BYTE_DATA);

	return rc;
}

static int adp165_set_timer(struct msm_led_flash_ctrl_t *a_ctrl, int value) {
	uint16_t timer_set = 0;
	int rc = 0;

	if(value > 0xf) {
		value = 0xf;
		printk("chengjiatest: flash timer value is too high to support, just max it\n");
	}
	rc = a_ctrl->flash_i2c_client.i2c_func_tbl->i2c_read(&a_ctrl->flash_i2c_client, 
	       VERG_TIMER_REG, &timer_set, MSM_CAMERA_I2C_BYTE_DATA);
	timer_set &= 0xf0;
	timer_set |= value;
	rc = a_ctrl->flash_i2c_client.i2c_func_tbl->i2c_write(&a_ctrl->flash_i2c_client, 
		 VERG_TIMER_REG, timer_set , MSM_CAMERA_I2C_BYTE_DATA);

	return rc;
	
}

static int adp165_flash(struct msm_led_flash_ctrl_t *a_ctrl)
{
	uint16_t output_mode = 0;
	int rc = 0;

      	rc = a_ctrl->flash_i2c_client.i2c_func_tbl->i2c_read(&a_ctrl->flash_i2c_client, 
			OUTPUT_MODE_REG, &output_mode, MSM_CAMERA_I2C_BYTE_DATA);
	///set software strobe mode
	output_mode &= ~(1 << 2);
	rc = a_ctrl->flash_i2c_client.i2c_func_tbl->i2c_write(&a_ctrl->flash_i2c_client, 
 		       OUTPUT_MODE_REG, output_mode , MSM_CAMERA_I2C_BYTE_DATA);
	///set  led mode to flash mode and enable output_en 
	output_mode |=0x03;
	output_mode |= (1 << 3);
	rc = a_ctrl->flash_i2c_client.i2c_func_tbl->i2c_write(&a_ctrl->flash_i2c_client, 
 		       OUTPUT_MODE_REG, output_mode , MSM_CAMERA_I2C_BYTE_DATA);

	return rc;
}

static int adp165_torch(struct msm_led_flash_ctrl_t *a_ctrl)
{
	uint16_t output_mode = 0;
	uint16_t gpio_cfg = 0;
	int rc = 0;

      	rc = a_ctrl->flash_i2c_client.i2c_func_tbl->i2c_read(&a_ctrl->flash_i2c_client, 
			0x02, &gpio_cfg, MSM_CAMERA_I2C_BYTE_DATA);
	gpio_cfg &= 0xcf;
	gpio_cfg |= 0x10;
	rc = a_ctrl->flash_i2c_client.i2c_func_tbl->i2c_write(&a_ctrl->flash_i2c_client, 
 		       0x02, gpio_cfg , MSM_CAMERA_I2C_BYTE_DATA);

	///set  led mode to standby mode and enable output_en 
      	rc = a_ctrl->flash_i2c_client.i2c_func_tbl->i2c_read(&a_ctrl->flash_i2c_client, 
			OUTPUT_MODE_REG, &output_mode, MSM_CAMERA_I2C_BYTE_DATA);
	output_mode &= 0xfc;
	output_mode |= (1 << 3);
	rc = a_ctrl->flash_i2c_client.i2c_func_tbl->i2c_write(&a_ctrl->flash_i2c_client, 
 		       OUTPUT_MODE_REG, output_mode , MSM_CAMERA_I2C_BYTE_DATA);

	return rc;
}

static struct msm_sensor_power_setting adp165_flash_power_setting[] = {	
{		
	.seq_type = SENSOR_VREG,		
	.seq_val = CAM_VIO,		
	.config_val = 0,		
	.delay = 2,	
	},
};

static int adp165_flash_trigger_power_up(struct msm_led_flash_ctrl_t *a_ctrl)
{
	int rc = 0;
	
	printk("chengjiatest: adp165 flash trigger power up\n");
	msm_camera_config_single_vreg(a_ctrl->dev, 
		&a_ctrl->cam_vreg, (struct regulator **)&adp165_flash_power_setting->data[0], 1);
	if((rc = gpio_request(ADP165_FLASH_TORCH_EN, "adp165_torch_enable")) < 0) {
		printk("chengjiatest: request gpio ADP165_FLASH_TORCH_EN 79 error!\n");
		return rc;
	}	
 	gpio_direction_output(ADP165_FLASH_TORCH_EN, 0);
	if((rc = gpio_request(ADP165_FLASH_STROBE, "adp165_flash_strobe")) < 0) {
		printk("chengjiatest: request gpio ADP165_FLASH_STROBE 24  error!\n");
		return rc;
	}	
 	gpio_direction_output(ADP165_FLASH_STROBE, 0);	
  	if((rc = gpio_request(ADP165_FLASH_LED_EN, "adp165_power_enable")) < 0) {
		printk("chengjiatest: request gpio ADP165_FLASH_LED_EN  13 error!\n");
		return rc;
  	}
	gpio_direction_output(ADP165_FLASH_LED_EN, 1);
	msleep(50);

	return rc;
}

static int adp165_flash_trigger_power_down(struct msm_led_flash_ctrl_t *a_ctrl)
{
	int rc = 0;
	printk("chengjiatest: adp165 flash trigger power down\n");
	
	gpio_set_value(ADP165_FLASH_LED_EN, 0);
	gpio_set_value(ADP165_FLASH_TORCH_EN, 0);
	gpio_set_value(ADP165_FLASH_STROBE, 0);
	gpio_free(ADP165_FLASH_LED_EN);
	gpio_free(ADP165_FLASH_TORCH_EN);
	gpio_free(ADP165_FLASH_STROBE);
	
	msm_camera_config_single_vreg(a_ctrl->dev, 
		&a_ctrl->cam_vreg, (struct regulator **)&adp165_flash_power_setting->data[0], 0);

	return rc;
}
static int adp165_flash_trigger_config(struct msm_led_flash_ctrl_t *fctrl,
	void *data)
{
	int rc = 0;
	struct msm_camera_led_cfg_t *cfg = (struct msm_camera_led_cfg_t *)data;
	printk("chengjiatest: called led_state %d\n", cfg->cfgtype);

	switch (cfg->cfgtype) {
	case MSM_CAMERA_LED_OFF:
		printk("chengjiatest: set adp165 off!\n");
		gpio_set_value(ADP165_FLASH_TORCH_EN, 0);
		rc = adp165_set_off(fctrl);
		break;
	case MSM_CAMERA_LED_LOW:
		printk("chengjiatest: set adp165 low!\n");
		gpio_set_value(ADP165_FLASH_TORCH_EN,0);
		adp165_set_current(fctrl, 0x0, TORCH_MODE);
		adp165_torch(fctrl);
		gpio_set_value(ADP165_FLASH_TORCH_EN, 1);
		break;
	case MSM_CAMERA_LED_HIGH:
		printk("chengjiatest: set adp165 high!\n");
		adp165_set_current(fctrl, 0x20, FLASH_MODE);
		adp165_set_timer(fctrl, 0xa);
		adp165_flash(fctrl);
		break;
	case MSM_CAMERA_LED_INIT:
		printk("chengjiatest: set adp165 init!\n");
		adp165_flash_trigger_power_up(fctrl);
		break;
	case MSM_CAMERA_LED_RELEASE:
		printk("chengjiatest: set adp165 release!\n");
		adp165_flash_trigger_power_down(fctrl);
		break;
	default:
		rc = -EFAULT;
		break;
	}
	CDBG("flash_set_led_state: return %d\n", rc);
	return rc;
}

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


static const struct of_device_id adp165_led_trigger_dt_match[] = {
	{.compatible = "qcom,adp165", .data = NULL},   
	{}
};

static struct msm_flash_fn_t adp165_flash_trigger_func_tbl = {
	.flash_get_subdev_id = adp165_flash_trigger_get_subdev_id,
	.flash_led_config = adp165_flash_trigger_config,
};

#define ADP165_TORCH_MODE 2
#define ADP165_FLASH_MODE 1

static int check_adp165_device_id(struct msm_led_flash_ctrl_t *a_ctrl)
{
	int rc = 0;
	uint16_t chipid = 0;

	adp165_flash_trigger_power_up(a_ctrl);
	
	rc = a_ctrl->flash_i2c_client.i2c_func_tbl->i2c_util(
		&a_ctrl->flash_i2c_client, MSM_CCI_INIT);
	if (rc < 0) {
		printk("chengjiatest: cci_init failed\n");
		return rc;
	}
	rc = a_ctrl->flash_i2c_client.i2c_func_tbl->i2c_read(&a_ctrl->flash_i2c_client, 
		DEVICE_ID_REG, &chipid, MSM_CAMERA_I2C_BYTE_DATA);
	printk("chengjiatest: read from device adp165 id is 0x%x\n", chipid);
	if(rc < 0) {
		printk("chengjiatest: i2c read adp165 error!\n");
		return rc;
	}
	if(ADP165_ID != chipid) {
		printk("chengjiatest: adp165 id is mismatch!\n");
		return -1;
	}
	rc = a_ctrl->flash_i2c_client.i2c_func_tbl->i2c_util(
		&a_ctrl->flash_i2c_client, MSM_CCI_RELEASE);
	if (rc < 0) {
		printk("chengjiatest: cci_release failed\n");
		return rc;
	}

	adp165_flash_trigger_power_down(a_ctrl);
	return 0;	
}


static int32_t adp165_led_trigger_probe(struct platform_device *pdev)
{
	int32_t rc = 0;
	struct msm_camera_cci_client *cci_client = NULL;
	struct msm_led_flash_ctrl_t *s_ctrl;

	printk("chengjiatest: adp165 led trigger probe\n");
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
	rc = of_property_read_u32((&pdev->dev)->of_node, "cell-index",
		&pdev->id);
	if (rc < 0) {
		printk("failed rc %d\n", rc);
		return rc;
	}
	rc = of_property_read_u32((&pdev->dev)->of_node, "qcom,cci-master",
		&s_ctrl->cci_master);
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
	
	cci_client = s_ctrl->flash_i2c_client.cci_client;
	cci_client->cci_subdev = msm_cci_get_subdev();
	cci_client->cci_i2c_master = s_ctrl->cci_master;
	cci_client->sid = 0x30;
	cci_client->retries = 3;
	cci_client->id_map = 0;

 	s_ctrl->flash_i2c_client.i2c_func_tbl = &msm_led_cci_func_tbl;
	s_ctrl->flash_i2c_client.addr_type = MSM_CAMERA_I2C_BYTE_ADDR; 
	s_ctrl->led_flash_mutex = &msm_led_flash_mutex;
	s_ctrl->pdev = pdev;
       s_ctrl->func_tbl = &adp165_flash_trigger_func_tbl;

	printk("chengjiatest: cell-index %d, cci-master %d\n", pdev->id, cci_client->cci_i2c_master);
	if((rc = check_adp165_device_id(s_ctrl)) != 0) {
		return rc;
	}
	rc = msm_led_flash_create_v4lsubdev(pdev, s_ctrl);
	
	return rc;
}

MODULE_DEVICE_TABLE(of, msm_led_trigger_dt_match);

static struct platform_driver adp165_led_trigger_driver = {
	.driver = {
		.name = "qcom,adp165",
		.owner = THIS_MODULE,
		.of_match_table = adp165_led_trigger_dt_match,
	},
};

static int __init adp165_led_trigger_add_driver(void)
{
	printk("chengjiatest: adp165_led_trigger_add_driver!\n");
	return platform_driver_probe(&adp165_led_trigger_driver,
		adp165_led_trigger_probe);
}

module_init(adp165_led_trigger_add_driver);
MODULE_DESCRIPTION("LED TRIGGER FLASH");
MODULE_LICENSE("GPL v2");

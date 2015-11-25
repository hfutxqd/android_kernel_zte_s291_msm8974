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
 
 
#ifndef MSM_LED_FLASH_H
#define MSM_LED_FLASH_H

#include <linux/i2c.h>
#include <linux/gpio.h>
#include <mach/camera2.h>
#include <media/v4l2-subdev.h>
#include <media/msmb_camera.h>
#include "msm_camera_i2c.h"
#include "msm_sd.h"

#define DEFINE_MSM_MUTEX(mutexname) \
	static struct mutex mutexname = __MUTEX_INITIALIZER(mutexname)

struct msm_led_flash_ctrl_t;

struct msm_flash_fn_t {
	int32_t (*flash_get_subdev_id)(struct msm_led_flash_ctrl_t *, void *);
	int32_t (*flash_led_config)(struct msm_led_flash_ctrl_t *, void *);
	int32_t (*flash_led_init)(struct msm_led_flash_ctrl_t *);
	int32_t (*flash_led_release)(struct msm_led_flash_ctrl_t *);
	int32_t (*flash_led_off)(struct msm_led_flash_ctrl_t *);
	int32_t (*flash_led_low)(struct msm_led_flash_ctrl_t *);
	int32_t (*flash_led_high)(struct msm_led_flash_ctrl_t *);
};

struct msm_led_flash_ctrl_t {
	struct platform_device *pdev;
	struct msm_camera_i2c_client  flash_i2c_client;
	enum msm_camera_device_type_t led_device_type;
	struct msm_sd_subdev msm_sd;
	uint32_t subdev_id;
	struct mutex *led_flash_mutex;
	struct v4l2_subdev_ops *led_v4l2_subdev_ops;
       struct device *dev;
	uint16_t *step_position_table;
       struct msm_sensor_power_setting *power_setting;
       struct camera_vreg_t  cam_vreg;
	enum cci_i2c_master_t cci_master;
    struct msm_flash_fn_t *func_tbl;
};

int32_t msm_led_flash_create_v4lsubdev(struct platform_device *pdev, struct msm_led_flash_ctrl_t *fctrl);

#endif

/**Ak8989 switch interface*/
#include	<linux/fs.h>
#include	<linux/gpio.h>
#include	<linux/irq.h>
#include	<linux/err.h>
#include <linux/kernel.h>
#include <linux/i2c.h>
#include <linux/errno.h>
#include <linux/delay.h>
#include <linux/string.h>
#include <linux/mutex.h>
#include <linux/unistd.h>
#include <linux/interrupt.h>
#include <linux/platform_device.h>
#include <linux/input.h>
#include <linux/slab.h>
#include <linux/pm.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/workqueue.h>

#define ak8789_gpio 68

struct ak8789_chip {
	struct mutex lock;
	struct input_dev *ak8789_idev;
	struct work_struct work;
	int irq_sensor;
	bool ak8789_enabled;
}*ak8789_chip_data;

extern void synaptics_touchscreen_gloved_finger(bool enable); /*ZTE_MODIFY wamgzhanmeng,add for changing  gloved function dynamic 20130817 */

static irqreturn_t ak8789_enable_irq(int irq, void *handle)
{
    schedule_work(&ak8789_chip_data->work);
    return IRQ_HANDLED;
}

static void ak8789_work_func(struct work_struct * work) 
{
	int value;
	
	value=gpio_get_value(ak8789_gpio);	

	if(ak8789_chip_data->ak8789_enabled==1){
/** ZTE_MODIFY yuehongliang add hall sensor, 2013-07-09 */ 
		if(value==1){
			synaptics_touchscreen_gloved_finger(false);/*ZTE_MODIFY wamgzhanmeng,add for changing  gloved function dynamic 20130817 */
			input_report_key(ak8789_chip_data->ak8789_idev, KEY_HALL_SENSOR_UP, 1);
			input_sync(ak8789_chip_data->ak8789_idev);
			input_report_key(ak8789_chip_data->ak8789_idev, KEY_HALL_SENSOR_UP, 0);
		} else {
			synaptics_touchscreen_gloved_finger(true);/*ZTE_MODIFY wamgzhanmeng,add for changing  gloved function dynamic 20130817 */
			input_report_key(ak8789_chip_data->ak8789_idev, KEY_HALL_SENSOR_DOWN, 1);
			input_sync(ak8789_chip_data->ak8789_idev);
			input_report_key(ak8789_chip_data->ak8789_idev, KEY_HALL_SENSOR_DOWN, 0);
		}
		input_sync(ak8789_chip_data->ak8789_idev);
/** ZTE_MODIFY end yuehongliang, 2013-07-09 */
	}
}


static ssize_t ak8789_device_enable_show(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	int val;
	val=gpio_get_value(ak8789_gpio);
	return sprintf(buf, "%d\n", val);
}

static ssize_t  ak8789_device_enable(struct device *dev,
				struct device_attribute *attr,
				const char *buf, size_t size)
{

	bool value;

	if (strtobool(buf, &value))
		return -EINVAL;

	//printk("%s:zhangxiaolei test value=%d,size=%d\n",__func__,value,size);
	if (value)	
	{
	if(ak8789_chip_data->ak8789_enabled==0)
		{
		ak8789_chip_data->ak8789_enabled=1;
		}
	}
	else
	{
	if(ak8789_chip_data->ak8789_enabled==1)
		{
		ak8789_chip_data->ak8789_enabled=0;
		}
    }	
	return size;
}

static struct device_attribute ak8789_attrs[] = {
	__ATTR(ak8789_enable, 0444, ak8789_device_enable_show, ak8789_device_enable),
};

static int add_sysfs_interfaces(struct device *dev,
	struct device_attribute *a, int size)
{
	int i;
	for (i = 0; i < size; i++)
		if (device_create_file(dev, a + i))
			goto undo;
	return 0;
undo:
	for (; i >= 0 ; i--)
		device_remove_file(dev, a + i);
	printk("%s: failed to create sysfs interface\n", __func__);
	return -ENODEV;
}


static int __init ak8789_init(void)
{

	int err,ret;
	//printk(KERN_INFO "zhangxiaolei ak8789 : init\n");

	ak8789_chip_data = kzalloc(sizeof(struct ak8789_chip), GFP_KERNEL);
	if (!ak8789_chip_data) {
		ret = -ENOMEM;
		printk("%s:chip kzalloc failed\n",__func__);
		return ret;
	}
	mutex_init(&ak8789_chip_data->lock);

       INIT_WORK(&(ak8789_chip_data->work),ak8789_work_func);
	
	ak8789_chip_data->ak8789_idev = input_allocate_device();
	if (!ak8789_chip_data->ak8789_idev) {
		printk("%s:ak8789 error1\n",__func__);
		ret = -ENODEV;
		return ret;
	}
/** ZTE_MODIFY yuehongliang add hall sensor, 2013-07-09 */ 
	ak8789_chip_data->ak8789_idev->name = "AK8789";
	set_bit(KEY_POWER, ak8789_chip_data->ak8789_idev->keybit);
        set_bit(KEY_HALL_SENSOR_DOWN, ak8789_chip_data->ak8789_idev->keybit);
        set_bit(KEY_HALL_SENSOR_UP, ak8789_chip_data->ak8789_idev->keybit);              
	set_bit(EV_KEY, ak8789_chip_data->ak8789_idev->evbit);
/** ZTE_MODIFY end yuehongliang, 2013-07-09 */	
	ret = input_register_device(ak8789_chip_data->ak8789_idev);
	if (ret) {
		input_free_device(ak8789_chip_data->ak8789_idev);
		printk("%s:ak8789 error2\n",__func__);
		return ret;
	}

	ret = add_sysfs_interfaces(&ak8789_chip_data->ak8789_idev->dev,
			ak8789_attrs, ARRAY_SIZE(ak8789_attrs));
	if (ret)
		goto input_a_sysfs_failed;
	
	ak8789_chip_data->irq_sensor = gpio_to_irq(ak8789_gpio); 
		/* Check if GPIO_49 pin is in use */ 
	err=gpio_request(ak8789_gpio, "ak8789"); 

	if (err) {
		printk("%s:ak8789 error3\n",__func__);
		}
		/* Set GPIO_49 pin as input */ 
	err=gpio_direction_input(ak8789_gpio); 
		
	ret = request_threaded_irq(ak8789_chip_data->irq_sensor, NULL, ak8789_enable_irq,
		      IRQ_TYPE_EDGE_BOTH,
		      "ak8789", NULL);
	if (ret) {
		printk("%s:ak8789 error4\n",__func__);
			goto input_a_sysfs_failed;
		}
		/* Enable GPIO_49 as wakeup source */ 
	enable_irq_wake(ak8789_chip_data->irq_sensor);
	ak8789_chip_data->ak8789_enabled=1;


	
	printk("%s:Ak8789 Init OK!\n",__func__);
	return 0;

input_a_sysfs_failed:
	input_unregister_device(ak8789_chip_data->ak8789_idev);
	printk("%s:Ak8789 Init Failed!\n",__func__);
	return ret;
}

		
static void __exit ak8789_exit(void)
{
	gpio_free(ak8789_gpio);
}

module_init(ak8789_init);
module_exit(ak8789_exit);

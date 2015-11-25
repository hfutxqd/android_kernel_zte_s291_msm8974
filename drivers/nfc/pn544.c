/*
 * Copyright (c) 2010 Trusted Logic S.A.
 * All Rights Reserved.
 *
 * This software is the confidential and proprietary information of
 * Trusted Logic S.A. ("Confidential Information"). You shall not
 * disclose such Confidential Information and shall use it only in
 * accordance with the terms of the license agreement you entered
 * into with Trusted Logic S.A.
 *
 * TRUSTED LOGIC S.A. MAKES NO REPRESENTATIONS OR WARRANTIES ABOUT THE
 * SUITABILITY OF THE SOFTWARE, EITHER EXPRESS OR IMPLIED, INCLUDING
 * BUT NOT LIMITED TO THE IMPLIED WARRANTIES OF MERCHANTABILITY, FITNESS
 * FOR A PARTICULAR PURPOSE, OR NON-INFRINGEMENT. TRUSTED LOGIC S.A. SHALL
 * NOT BE LIABLE FOR ANY DAMAGES SUFFERED BY LICENSEE AS A RESULT OF USING,
 * MODIFYING OR DISTRIBUTING THIS SOFTWARE OR ITS DERIVATIVES.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/fs.h>
#include <linux/slab.h>
#include <linux/init.h>
#include <linux/list.h>
#include <linux/i2c.h>
#include <linux/jiffies.h>
#include <asm/uaccess.h>
#include <linux/gpio.h>
#include <linux/nfc/pn544.h>

#include <linux/delay.h>
#include <linux/hrtimer.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/platform_device.h>
#include <asm-generic/gpio.h>


#include <linux/mfd/pm8xxx/pm8921.h>

#include <linux/string.h>	/* zte-ccb-20130128 */
#include <linux/of_gpio.h>


/* Macros assume PMIC GPIOs and MPPs start at 1 */
//#define PMIC_GPIO_BASE		NR_GPIO_IRQS	//zte-ccb-20130315

//#define 1
//#define MAX_BUFFER_SIZE 256

//fix the issue when fireware update because max buf is 512bytes
#define MAX_BUFFER_SIZE 512

#define PN544_MAJOR     0  /* Defaults to dynamic major number */
#define PN544_MINORS    1  /* Register for only one minor */

#define PN544_DRIVER_NAME         "pn544"
#define CLASSNAME       			"nfc-dev"

#ifdef LOG_TAG
#undef LOG_TAG
#endif

#define LOG_TAG				"pn544 "

static DEFINE_MUTEX(nfc_mutex);	/* Used to walk the nfc */

struct pn544_dev {
   wait_queue_head_t read_queue; /* Used to save blocked callers */
   struct semaphore  sem;        /* Used to synchronize IRQ value reading */
   struct i2c_client *client;    /* The structure for use with i2c functions */
   struct device     *dev;       /* The char device structure */
   int               use_irq;    /* Flag set to 1 if using IRQ, 0 if polling */
   struct hrtimer    timer;      /* Timer used for polling */
   unsigned int 	ven_gpio;
   unsigned int 	firm_gpio;
   unsigned int		irq_gpio;
   unsigned int       dcdc_gpio;
   unsigned int       clock_gpio;
   unsigned int       int_active_low;
};

static struct pn544_dev    *pn544_dev = NULL;

/*
 * Our parameters which can be set at load time.
 */

int pn544_major = PN544_MAJOR;
int pn544_minor = 0;
int pn544_disable_irq = false;
int pn544_poll_value = 10000000;

module_param(pn544_major, int, S_IRUGO);
module_param(pn544_minor, int, S_IRUGO);
module_param(pn544_disable_irq, int, S_IRUGO);
module_param(pn544_poll_value, int, S_IRUGO);

MODULE_AUTHOR("Trusted Logic S.A. (Sylvain Fonteneau)");
MODULE_DESCRIPTION("PN544 /dev/nfc entry driver");
MODULE_LICENSE("GPL");

/* ------------------------------------------------------------------------- */

static irqreturn_t pn544_dev_irq_handler(int irq, void *dev_id)
{
   struct pn544_dev *pn544 = dev_id;

#if 1
  printk("pn544-dev: interrupt  occur!!!!.\n");
#endif

   /* Wake up readers */
   wake_up_interruptible(&pn544->read_queue);

   return IRQ_HANDLED;
}

static enum hrtimer_restart pn544_dev_timer_handler(struct hrtimer *timer)
{
   struct pn544_dev *pn544 = container_of(timer, struct pn544_dev, timer);

   /* Wake up readers */
   wake_up_interruptible(&pn544->read_queue);

   /* Restart timer */
   hrtimer_start(&pn544->timer, ktime_set(0, pn544_poll_value), HRTIMER_MODE_REL);
   return HRTIMER_NORESTART;
}

#if 1
static void printk_buffer(char * buffer, int count)
{
   int i;

   printk("[ ");
   for(i=0;i<count;i++) {
      printk("%02X ", buffer[i]);
   }
   printk("]");
}
#else
/* Utility to display buffer in logs */
static void pr_debug_buffer(char * buffer, int count)
{
   int i;

   pr_debug("[ ");
   for(i=0;i<count;i++) {
      pr_debug("%02X ", buffer[i]);
   }
   pr_debug("]");
}
#endif

/* ------------------------------------------------------------------------- */
static ssize_t pn544_dev_read (struct file *filp, char __user *buf, size_t count,
                              loff_t *offset)
{
   struct pn544_dev  *pn544 = (struct pn544_dev *)filp->private_data;
   struct i2c_client *client = (struct i2c_client *)pn544->client;
   char *tmp;
   int ret;
  //int irq_gpio = irq_to_gpio(client->irq);
  //int irq_gpio = INT_TO_MSM_GPIO(client->irq);	//8974 platform irq=-692; client->irq=328; pin is 59
  int irq_gpio = pn544_dev->irq_gpio;				//

   if (count > MAX_BUFFER_SIZE) {
      count = MAX_BUFFER_SIZE;
   }

   tmp = kmalloc(count,GFP_KERNEL);
   if (tmp == NULL) {
      return -ENOMEM;
   }

#if  1
   printk("pn544-dev: reading %zu bytes.\n", count);
#else
   pr_debug("pn544-dev: reading %zu bytes.\n", count);
#endif

   /* Lock semaphore */
   if (down_interruptible(&pn544->sem)) {
      return -ERESTARTSYS;
   }
   /* Wait for IRQ if not already pending */
   if(pn544_dev->int_active_low==1)
   {
        while (gpio_get_value(irq_gpio)) {
           /* Not ready to read data, release semaphore */
           up(&pn544->sem);
           /* Handle non-blocking calls */
           if (filp->f_flags & O_NONBLOCK) {
              return -EAGAIN;
           }
     
     #if  1
         printk("pn544-dev: wait for incoming data1.\n");
     #else
           pr_debug("pn544-dev: wait for incoming data1.\n");
     #endif

#if 1
        if (wait_event_interruptible(pn544->read_queue, (gpio_get_value(irq_gpio)==0))) {
                             printk("pn544-dev: wait for interrupt error1.\n");
				 return -ERESTARTSYS; /* signal: tell the fs layer to handle it */
              	}
#else
	 if(wait_event_interruptible_timeout(pn544->read_queue, 
				(gpio_get_value(irq_gpio)==0), 
				msecs_to_jiffies(5000))<=0){
				
		printk("pn544-dev: wait for interrupt error1.\n");
		return -ERESTARTSYS; /* signal: tell the fs layer to handle it */
     	}
#endif
     
           /* Loop, but first reacquire the lock (to avoid multiple read concurrency) */
           if (down_interruptible(&pn544->sem)) {
		printk("pn544-dev: wait for interrupt sem error1.\n");   	
              return -ERESTARTSYS;
           }
        }
   }
   else
   {
           while (!gpio_get_value(irq_gpio)) {
           /* Not ready to read data, release semaphore */
           up(&pn544->sem);
           /* Handle non-blocking calls */
           if (filp->f_flags & O_NONBLOCK) {
              return -EAGAIN;
           }
     
     #if  1
          printk("pn544-dev: wait for incoming data2.\n");
     #else
           pr_debug("pn544-dev: wait for incoming data2.\n");
     #endif

#if 1
         if (wait_event_interruptible(pn544->read_queue, (gpio_get_value(irq_gpio)!=0))) {
                             printk("pn544-dev: wait for interrupt error2.\n");
				 return -ERESTARTSYS; /* signal: tell the fs layer to handle it */
     		}
#else
         if(wait_event_interruptible_timeout(pn544->read_queue, 
				(gpio_get_value(irq_gpio)!=0), 
				msecs_to_jiffies(5000))<=0){
		printk("pn544-dev: wait for interrupt error2.\n");
		return -ERESTARTSYS; /* signal: tell the fs layer to handle it */
     	}
#endif
     
           /* Loop, but first reacquire the lock (to avoid multiple read concurrency) */
           if (down_interruptible(&pn544->sem)) {
		printk("pn544-dev: wait for interrupt sem error2.\n");   
              return -ERESTARTSYS;
           }
        }
  }

   /* Read data */
   ret = i2c_master_recv(client, tmp, count);
   if (ret >= 0) {
      ret = copy_to_user(buf, tmp, count)?-EFAULT:ret;

#if  1 
      printk("pn544_dev_read: received ");
      printk_buffer(tmp, count);
      printk("\n");
#else	
      pr_debug("pn544_dev_read: received ");
      pr_debug_buffer(tmp, count);
      pr_debug("\n");
#endif	  
   }
   else if (ret < 0) {
      printk("pn544_dev: failed to read from i2c (error code  %d)\n", ret);
   }

   /* Release semaphore */
   up (&pn544->sem);
   kfree(tmp);

   return ret;
}

static ssize_t pn544_dev_write (struct file *filp, const char __user *buf, size_t count,
                             loff_t *offset)
{
   int ret;
   char *tmp;

   struct pn544_dev  *pn544 = (struct pn544_dev *)filp->private_data;
   struct i2c_client *client = (struct i2c_client *)pn544->client;

   if (count > MAX_BUFFER_SIZE) {
      count = MAX_BUFFER_SIZE;
   }

   tmp = kmalloc(count,GFP_KERNEL);
   if (tmp == NULL)  {
      printk("pn544_dev_write: failed to allocate buffer\n");
      return -ENOMEM;
   }
   if (copy_from_user(tmp, buf, count)) {
      printk("pn544_dev_write: failed to copy from user space\n");
      kfree(tmp);
      return -EFAULT;
   }

#if 1
   printk("pn544-dev: writing %zu bytes.\n", count);
#else
   pr_debug("pn544-dev: writing %zu bytes.\n", count);
#endif

   /* Write data (already semaphore-protected) */
   ret = i2c_master_send(client, tmp, count);
   if (ret < 0) {
      printk("pn544_dev_write: i2c_master_send() returned %d\n", ret);
   }
   else
   {
#if 1 
      printk("pn544_dev_write: sent ");
      printk_buffer(tmp, count);
      printk("\n");
#else
      pr_debug("pn544_dev_write: sent ");
      pr_debug_buffer(tmp, count);
      pr_debug("\n");
#endif	  
   }
   kfree(tmp);
   return ret;
}

static int pn544_dev_open(struct inode *inode, struct file *filp)
{
   unsigned int minor = iminor(inode);
   int ret = 0;

   printk("pn544_dev_open: %d,%d\n", imajor(inode), iminor(inode));

//lock_kernel();
   mutex_lock(&nfc_mutex);

   if (minor != pn544_minor) {
      ret = -ENODEV;
      goto out;
   }

   filp->private_data = pn544_dev;

out:
	//   unlock_kernel();
	mutex_unlock(&nfc_mutex);
   return ret;
}
static int pn544_dev_release(struct inode *inode, struct file *file)
{
   printk("pn544_dev_release: oha!\n");
   return 0;
}

static long pn544_dev_ioctl(struct file *filp,unsigned int cmd, unsigned long arg)
{

    

	switch (cmd) {
	case PN544_SET_PWR:
		if (arg == 2) {
			/* power on with firmware download (requires hw reset) */
		 
			printk("%s power on with firmware\n", __func__);
			gpio_set_value_cansleep(pn544_dev->ven_gpio, 0);
			printk("cuilinwei set firmware power %d\n",pn544_dev->ven_gpio);
			gpio_set_value(pn544_dev->firm_gpio, 1);
		       printk("cuilinwei pn544 set firmware power   1\n");
			msleep(20);
			gpio_set_value_cansleep(pn544_dev->ven_gpio, 1);
			printk("cuilinwei   pn544  set firmware OK\n");
			msleep(60);
			gpio_set_value_cansleep(pn544_dev->ven_gpio, 0);
			msleep(20);
		} else if (arg == 1) {
			/* power on */

			printk("cuilinwei  pn544 %s power on with ioctl cmd 1\n", __func__);
		//	gpio_set_value_cansleep(pn544_dev->ven_gpio, 0);
		       gpio_set_value(pn544_dev->firm_gpio, 0);
		
		//	msleep(20);
		//	gpio_set_value_cansleep(pn544_dev->ven_gpio, 1);
		//	msleep(50);
			gpio_set_value_cansleep(pn544_dev->ven_gpio, 0);
			msleep(60);
		} else  if (arg == 0) {
			/* power off */
			
			printk("cuilinwei pn544 %s power off with ioctl cmd 0\n", __func__);
		       //disable_irq(client->irq);
		       gpio_set_value(pn544_dev->firm_gpio, 0);
		       gpio_set_value_cansleep(pn544_dev->ven_gpio, 1);
			msleep(60);
		} else {
			printk("%s bad arg %d\n", __func__, (int)(arg));
			return -EINVAL;
		}
		break;
	default:
		printk("%s bad ioctl %d\n", __func__, cmd);
		return -EINVAL;
	}

	return 0;
}

static int nxp_pn544_reset(void)
{
	int rc;

#if 0
	/*******************************************************/
       static struct regulator *reg_l15;

       reg_l15 = regulator_get(NULL,	"8921_l15");
	if (IS_ERR(reg_l15)) {
			pr_err("could not get 8921_l15, rc = %ld\n",
				PTR_ERR(reg_l15));
			return -ENODEV;
		}

	rc = regulator_set_voltage(reg_l15, 2850000, 2850000);
	if (rc) {
			pr_err("set_voltage l15 failed, rc=%d\n", rc);
			return -EINVAL;
		   }

	rc = regulator_enable(reg_l15);
	if (rc) {
			pr_err("enable l15 failed, rc=%d\n", rc);
			return -ENODEV;
		}

	/********************************************************/
#endif

	
          rc = gpio_tlmm_config(GPIO_CFG(pn544_dev->irq_gpio, 0,
    				GPIO_CFG_INPUT, GPIO_CFG_PULL_DOWN,
    				GPIO_CFG_2MA), GPIO_CFG_ENABLE);
    	  if (rc) {
    			printk( "%s: Could not configure nfc gpio %d\n",
    					__func__, pn544_dev->irq_gpio);
    			 return -EIO;
    		   }
    
    	 rc = gpio_request(pn544_dev->irq_gpio, "nxp_pn544_IRQ");
    	 if (rc) {
    			printk( "%s: unable to request nfc gpio %d (%d)\n",
    					__func__, pn544_dev->irq_gpio, rc);
    			 return -EIO;
    		    }
     
     
          rc = gpio_tlmm_config(GPIO_CFG(pn544_dev->firm_gpio, 0,
    				GPIO_CFG_OUTPUT, GPIO_CFG_PULL_DOWN,
    				GPIO_CFG_2MA), GPIO_CFG_ENABLE);
	 printk("cuilinwei  pn544 config firmgpio pull down\n");
    	  if (rc) {
    			printk( "%s: Could not configure nfc gpio %d\n",
    					__func__, pn544_dev->firm_gpio);
    			 return -EIO;
    		   }
    
    	 rc = gpio_request(pn544_dev->firm_gpio, "nxp_pn544_download");
    	 if (rc) {
    			printk( "%s: unable to request nfc gpio %d (%d)\n",
    					__func__, pn544_dev->firm_gpio, rc);
    			 return -EIO;
    		    }
        gpio_direction_output(pn544_dev->firm_gpio, 1);   //by cuilinwei
     
	 	 

	   
     
     		rc = gpio_request(pn544_dev->ven_gpio, "nxp_pn544_en");
            	if (rc) {
            			printk( "%s: unable to request nfc gpio %d (%d)\n",
            					__func__,pn544_dev->ven_gpio, rc);
            			 return -EIO;
            	          }
     
     
     		gpio_set_value_cansleep(pn544_dev->ven_gpio, 0);
            	mdelay(10);
            	gpio_set_value_cansleep(pn544_dev->ven_gpio, 1);
            	mdelay(50);
            	gpio_set_value_cansleep(pn544_dev->ven_gpio, 0);
            	mdelay(10);
             
      // }
	return 0;
}


static const struct file_operations pn544_dev_fops = {
   .owner            = THIS_MODULE,
   .llseek           = no_llseek,
   .read             = pn544_dev_read,
   .write            = pn544_dev_write,
   .open             = pn544_dev_open,
   .release          = pn544_dev_release,
  .unlocked_ioctl             = pn544_dev_ioctl,
/*   .poll             = pn544_dev_poll, */
};

/* ------------------------------------------------------------------------- */

static struct class *pn544_dev_class;

static int pn544_probe(
   struct i2c_client *client, const struct i2c_device_id *id)
{
   int 		ret;
   u32 	temp_val;
   struct device_node *of_node = NULL;
   
   printk("pn544_probe(): start\n");	//zte
   
   if (pn544_dev != NULL) {
      printk("pn544_probe: multiple devices NOT supported\n");
      ret = -ENODEV;
      goto err_single_device;
   }

   if (!i2c_check_functionality(client->adapter, I2C_FUNC_I2C)) {
      printk("pn544_probe: need I2C_FUNC_I2C\n");
      ret = -ENODEV;
      goto err_check_functionality_failed;
   }

   pn544_dev = kzalloc(sizeof(*pn544_dev), GFP_KERNEL);
   if (pn544_dev == NULL) {
      printk("pn544_probe: out of memory\n");
      ret = -ENOMEM;
      goto err_alloc_data_failed;
   }
   
   pn544_dev->client = client;
   
   if (client->dev.of_node) {
   	
   	of_node = client->dev.of_node;

	ret = of_get_named_gpio(of_node, "nfc,irq_gpio", 0);
	if (ret>0) {
		pn544_dev->irq_gpio=ret;
	} else{
		printk("pn544_probe: of_property_read(irq_gpio) fail:%d\n",ret);
		goto err_device_create_failed;
	}

	ret = of_get_named_gpio(of_node, "nfc,firm_gpio", 0);
	if (ret>0) {
		pn544_dev->firm_gpio=ret;
	} else{
		printk("pn544_probe: of_property_read(firm_gpio) fail:%d\n",ret);
		goto err_device_create_failed;
	}

	ret = of_get_named_gpio(of_node, "nfc,ven_gpio", 0);	//8974 mpp7
	if (ret>0) {
		pn544_dev->ven_gpio=ret;
	} else{
		printk("pn544_probe: of_property_read(ven_gpio) fail:%d\n",ret);
		goto err_device_create_failed;
	}
	
	ret = of_property_read_u32(of_node,"nfc,int_active_low", &temp_val);
	if (!ret) {
		pn544_dev->int_active_low=temp_val;
	} else{
		printk("pn544_probe: of_property_read(int_active_low) fail:%d\n",ret);
		goto err_device_create_failed;
	}
	
	pn544_dev->clock_gpio=0;
	pn544_dev->dcdc_gpio=0;

	printk( "pn544_probe: gpio config data,irq=%d,download=%d,ven=%d,int_active_low=%d\n",
		pn544_dev->irq_gpio,pn544_dev->firm_gpio,pn544_dev->ven_gpio,pn544_dev->int_active_low);
	// etc: 8974 => irq=59, download=5; ven=470(android4.2=>726); x_low=0
   }
   else
   {
   	printk("pn544_probe: no gpio config data\n");
      	ret = -ENODEV;
      	goto err_device_create_failed;
   }

   /* init semaphore and queues */
   sema_init(&pn544_dev->sem, 1);
   init_waitqueue_head(&pn544_dev->read_queue);

   /* register this device with the driver core */
   pn544_dev->dev = device_create(pn544_dev_class, &client->dev,
                 MKDEV(pn544_major, pn544_minor), NULL, PN544_DRIVER_NAME);
   if (IS_ERR(pn544_dev->dev)) {
      printk("pn544_probe: device_create() failed\n");
      ret = PTR_ERR(pn544_dev->dev);
      goto err_device_create_file_failed;
   }

   ret =nxp_pn544_reset();
   printk("cuilinwei pn544 reset\n");
   if (ret < 0) {
      	printk(  "can't reset device\n");
       	goto err_device_create_file_failed;
   }

   /* set irq/polling mode */
   if (client->irq && !pn544_disable_irq) {
	
    if(pn544_dev->int_active_low==1)
    {
          ret = request_irq(client->irq, pn544_dev_irq_handler, IRQF_TRIGGER_FALLING, client->name, pn544_dev);
    }
    else
    {
         ret = request_irq(client->irq, pn544_dev_irq_handler, IRQF_TRIGGER_RISING, client->name, pn544_dev);
    }
	
    if (ret == 0) {
         pn544_dev->use_irq = 1;
      }
    else {
         dev_err(&client->dev, "request_irq failed\n");
      }
   }
   
   if (!pn544_dev->use_irq) {
      hrtimer_init(&pn544_dev->timer, CLOCK_MONOTONIC, HRTIMER_MODE_REL);
      pn544_dev->timer.function = pn544_dev_timer_handler;
      hrtimer_start(&pn544_dev->timer, ktime_set(0, pn544_poll_value), HRTIMER_MODE_REL);
   }

   //printk("pn544_probe: Start in %s mode,IRQ=%d,GPIO=%d\n", pn544_dev->use_irq ? "interrupt" : "polling",client->irq,INT_TO_MSM_GPIO(client->irq));
   // interrupt; IRQ=328, GPIO=-692
   printk("pn544_probe: Start in %s mode,IRQ=%d,GPIO=%d\n", pn544_dev->use_irq ? "interrupt" : "polling", client->irq, client->irq);	//zte-ccb-20130315
   // interrupt; IRQ=328, GPIO=328 
  
   return 0;

err_device_create_file_failed:
   device_destroy(pn544_dev_class, MKDEV(pn544_major, pn544_minor));
err_device_create_failed:
   kfree(pn544_dev);
   pn544_dev = NULL;
err_alloc_data_failed:
err_check_functionality_failed:
err_single_device:
   return ret;
}

static int pn544_remove(struct i2c_client *client)
{
   printk("pn544_remove()\n");

   if (pn544_dev->use_irq) {
      free_irq(client->irq, pn544_dev);
   }
   else {
      hrtimer_cancel(&pn544_dev->timer);
   }
   device_destroy(pn544_dev_class, MKDEV(pn544_major, pn544_minor));
   kfree(pn544_dev);
   pn544_dev = NULL;
   return 0;
}

/*
static void pn544_shutdown(struct i2c_client *client)
{
       	gpio_set_value_cansleep(pn544_dev->ven_gpio, 0);
       	gpio_set_value(pn544_dev->firm_gpio, 0);
		
	msleep(20);	//mdelay(20); abnormal kernel_reset will call .shutdown.  the msleep make it reset repeatedly !
	gpio_set_value_cansleep(pn544_dev->ven_gpio, 1);
	msleep(60);	//
	gpio_set_value_cansleep(pn544_dev->ven_gpio, 0);
	msleep(20);	//
	
       	printk("pn544_shutdown()\n");	
}*/


static const struct i2c_device_id pn544_id[] = {
   	{ PN544_DRIVER_NAME, 0 },
   	{ }
};


static struct of_device_id nfc_match_table[] = {
	{.compatible = "nxp,pn544",},
	{ },
};

static struct i2c_driver pn544_driver = {
   	.id_table   = pn544_id,
   	.probe      = pn544_probe,
   	.remove      = pn544_remove,
   	//.shutdown	= pn544_shutdown,
   	.driver = {
		.name   = PN544_DRIVER_NAME,
		.of_match_table = nfc_match_table,
   	},
};

/* ------------------------------------------------------------------------- */

/*
 * module load/unload record keeping
 */
static int __init pn544_dev_init(void)
{
   int ret;

   printk( "pn544_dev_init()\n");

   ret = register_chrdev(pn544_major, PN544_DRIVER_NAME, &pn544_dev_fops);
 
   if (ret < 0) {
      printk(  "can't register device\n");
      goto err_register_chrdev_failed;
   }
   else if (ret > 0) {
      pn544_major = ret;
   }

  /*2. slf note 20110328--->create:  sys/class/nfc-dev */
   pn544_dev_class = class_create(THIS_MODULE, CLASSNAME);
   if (IS_ERR(pn544_dev_class)) {
      ret = PTR_ERR(pn544_dev_class);
      printk(  "can't create class\n");
      goto err_class_create_failed;
   }

   ret = i2c_add_driver(&pn544_driver);
   if (ret) {
      printk(  "can't register i2c driver\n");
      goto err_i2c_add_driver_failed;
   }

   printk("registered with major number %d\n", pn544_major);

   return 0;

err_i2c_add_driver_failed:
   class_destroy(pn544_dev_class);
err_class_create_failed:
   unregister_chrdev(pn544_major, PN544_DRIVER_NAME);
err_register_chrdev_failed:
   return ret;
}

static void __exit pn544_dev_exit(void)
{
   printk("Unloading /dev/nfc entry driver\n");
   i2c_del_driver(&pn544_driver);
   class_destroy(pn544_dev_class);
   unregister_chrdev(PN544_MAJOR, PN544_DRIVER_NAME);
}

module_init(pn544_dev_init);
module_exit(pn544_dev_exit);

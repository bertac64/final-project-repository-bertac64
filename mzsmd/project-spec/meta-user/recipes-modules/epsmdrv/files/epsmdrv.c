/*  epsmdrv.c - Simple kernel module.
 * Driver for Microzed shared memory access
 *
 *  Created on: 28 Oct 2024
 *      Author: bertac64
*/

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/gfp.h>
#include <linux/string.h>
#include <linux/kernel.h>
#include <linux/moduleparam.h>
#include <linux/cdev.h>
#include <linux/fs.h>
#include <linux/device.h>
#include <linux/interrupt.h>
#include <asm/uaccess.h>
#include <linux/version.h>
#include <linux/wait.h>
#include <linux/sched.h>
#include <linux/spinlock.h>
#include <linux/bitops.h>
#include <linux/workqueue.h>
#include <linux/platform_device.h>
#include <linux/of_platform.h>
#include <linux/of_address.h>
#include <linux/of_irq.h>
#include <asm/io.h>
#include <asm/cacheflush.h>

// ----------------------------------------------------------------------------------------
MODULE_LICENSE("GPL");
MODULE_AUTHOR ("bertac64");
MODULE_DESCRIPTION("epsmdrv - Shared Memory Driver");
MODULE_VERSION("1.0.0");

#define DRIVER_NAME "epsmdrv"
// ----------------------------------------------------------------------------------------
#define epsmd_ioctl_READ 0x100
#define epsmd_ioctl_WRITE 0x101

// ----------------------------------------------------------------------------------------

//! driver structure
typedef struct __drv_str_s {
    struct cdev *cdev;
    struct device *device;
    dev_t dev;
    
    unsigned long base;
    int irq;

    int openCnt;             //number of opened instances

    char devname[255];


/*    int tv_rx_done;
    int u_rx_cnt;
    int u_ak_cnt;
    
    wait_queue_head_t wait_lock_queue;
    wait_queue_head_t uwait_lock_queue;
    
    struct completion complete;*/
    spinlock_t lock;
} __drv_str;

static __drv_str sdrv;

static struct class *epsmd_class;


// ----------------------------------------------------------------------------------------
// IOCTLs
struct epsmd_regacc {
    unsigned int address;
    unsigned int data;
};


// ----------------------------------------------------------------------------------------
// Register access functions

static unsigned long read_reg ( __drv_str *dev, unsigned long offset )
{
unsigned long ret;

    ret = ioread32 ( (volatile void*)(dev->base + offset) );

    return ret;
}

static void write_reg ( __drv_str *dev, unsigned long offset, unsigned long value )
{
    iowrite32 ( value, (volatile void*)(dev->base + offset) );
}


/*static irqreturn_t epsmd_irq ( int irq, void *pdev )
{
	__drv_str *adev = (__drv_str *)pdev;
	unsigned int sts;

    return IRQ_HANDLED;
}*/


// ----------------------------------------------------------------------------------------
// Character Device file operations
/******************************************************************************************
 * implements the open system call
 ******************************************************************************************/
static int epsmd_open ( struct inode *inode, struct file *filp )
{
	int minor;
	__drv_str *adev;

    adev = &sdrv;
    minor = MINOR(inode->i_rdev);
    if ( minor != 0 ) {
        return -ENOENT;
    }
    adev->openCnt;

    filp->private_data = (void*)adev;

    return 0;
}


/******************************************************************************************
 * implements the close system call
 ******************************************************************************************/
static int epsmd_release ( struct inode *inode, struct file *filp )
{
	__drv_str *adev;

    adev = (__drv_str*)filp->private_data;

    if ( adev == NULL ) {
        return -ENOENT;
    }

    adev->openCnt--;
    return 0;
}


/******************************************************************************************
 * implements ioctl system call
 ******************************************************************************************/
static long epsmd_ioctl ( struct file *filp, unsigned int cmd, unsigned long arg )
{
	__drv_str *dev;
	int ret;
	void __user *uarg;
	//unsigned long flags;

    ret = 0;
    uarg = (void __user *)arg;

    dev = (__drv_str*)filp->private_data;
    if ( dev == NULL ) {
        return -ENOENT;
    }
    
    //spin_lock_irqsave(&dev->lock, flags);

    switch ( cmd ) {
        case epsmd_ioctl_READ: {
            struct epsmd_regacc r;
            if (copy_from_user((void*)&r,uarg,sizeof(unsigned int))) {
                ret = -EFAULT;
            } else {
                r.data = read_reg(dev,r.address);
                ret = 0;
                if (copy_to_user(uarg,(void*)&r,sizeof(struct epsmd_regacc))) {
                    ret = -EFAULT;
                }
            }
        }
            break;
        case epsmd_ioctl_WRITE: {
            struct epsmd_regacc r;
            if (copy_from_user((void*)&r,uarg,sizeof(struct epsmd_regacc))) {
                ret = -EFAULT;
            } else {
                write_reg(dev,r.address,r.data);
                ret = 0;
            }
        }
            break;
        default:
            // No valid IOCTL code
            ret = -EINVAL;
    }
    //spin_unlock_irqrestore(&dev->lock, flags);
    return ret;
}

// Character Device fops structure

struct file_operations epsmd_fops = {
    .owner      = THIS_MODULE,
    .open       = epsmd_open,
    .release    = epsmd_release,
    .unlocked_ioctl = epsmd_ioctl,
};
// ----------------------------------------------------------------------------------------

static int epsmdrv_probe(struct platform_device *pdev)
{
	int res;
	int irq = 0;
	struct resource *io_res;
	__drv_str *dev;

    dev = &sdrv;

   	io_res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (io_res == NULL) {
	    printk(KERN_ERR "epsmd: failed to get IORESOURCE_MEM\n");
		return -EINVAL;
	}
	//irq = irq_of_parse_and_map(pdev->dev.of_node, 0);
	irq = platform_get_irq(pdev, 0);
    if (irq <= 0) {
	    printk(KERN_ERR "epsmd: failed to get IRQ\n");
		return -EINVAL;
    }
    
    printk("epsmd: mappaing at 0x%08X irq=%d\n", io_res->start, irq );
    
   
    dev->base = (unsigned long)ioremap(io_res->start, resource_size(io_res));
    if ( dev->base == (unsigned long)NULL ) {
        printk("cannot map!\n");
        return -EINVAL;
    }
    dev->irq = irq;
	
    //Define char device
    res = alloc_chrdev_region ( &dev->dev,
            0, // minor number
            1,  // number of devices
            DRIVER_NAME );  // device name

    if ( res < 0 ) {
        printk( KERN_INFO "cannot get major device number\n" );
        return -ENODEV;
    }
    dev->cdev = cdev_alloc();
    dev->cdev->ops = &(epsmd_fops);
    dev->cdev->owner = THIS_MODULE;
    res = cdev_add(dev->cdev, dev->dev, 1);

    if ( res ) {
        unregister_chrdev_region ( dev->dev, 1 );
        return -ENODEV;
    }

    epsmd_class = class_create(THIS_MODULE, DRIVER_NAME);
    
    sprintf(dev->devname,"%s%d",DRIVER_NAME,0);
    dev->device = device_create(epsmd_class,NULL,MKDEV(MAJOR(dev->dev),0),dev,dev->devname);

    if ( dev->device == NULL ) {
        return -ENODEV;
    }
    
    dev->lock = __SPIN_LOCK_UNLOCKED(dev->lock);
    
    /* attach ISR for RX */
/*    res = request_irq( dev->irq, dcamz_irq, 0, DCAMZ_NAME, dev);
    if ( res ) {
        printk( KERN_INFO " cannot get IRQ" );

        return -ENXIO;
    }*/
    
    return 0;
}

static int epsmdrv_remove(struct platform_device *pdev)
{
	free_irq(sdrv.irq,&sdrv);
    iounmap((void*)sdrv.base);
    
    device_destroy(epsmd_class, MKDEV(MAJOR(sdrv.dev),0));
    class_destroy(epsmd_class);

	return 0;
}

static struct of_device_id epsmd_platform_match[] = {
	{ .compatible = "epsmdrv", },
	{},
};


static struct platform_driver epsmdrv_driver = {
	.probe		= epsmdrv_probe,
	.remove		= epsmdrv_remove,
	.driver = {
		.name = DRIVER_NAME,
		.owner = THIS_MODULE,
		.of_match_table	= epsmd_platform_match,
	},
};

static int __init epsmdrv_init(void)
{
	int status;
	
	printk("Start module epsmdrv.\n");
	status = platform_driver_register(&epsmdrv_driver);

	return status;
}


static void __exit epsmdrv_exit(void)
{
	platform_driver_unregister(&epsmdrv_driver);
	printk(KERN_ALERT "Goodbye module epsmdrv.\n");
}

module_init(epsmdrv_init);
module_exit(epsmdrv_exit);

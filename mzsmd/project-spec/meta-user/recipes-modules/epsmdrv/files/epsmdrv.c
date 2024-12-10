/*  epsmdrv.c - Simple kernel module.
 * Driver for Microzed shared memory access
 *
 *  Created on: 28 Oct 2024
 *      Author: bertac64
*/

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/gfp.h>
#include <linux/types.h>
#include <linux/string.h>
#include <linux/moduleparam.h>
#include <linux/cdev.h>
#include <linux/fs.h>
#include <linux/device.h>
#include <linux/kdev_t.h>
#include <linux/io.h>
#include <linux/interrupt.h>
#include <linux/uaccess.h>
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
#include <linux/cacheflush.h>

// ----------------------------------------------------------------------------------------
MODULE_LICENSE("Dual BSD/GPL");
MODULE_AUTHOR ("bertac64");
MODULE_DESCRIPTION("epsmdrv - Shared Memory Driver");
MODULE_VERSION("1.0.0");

/* Define debugging for use during our driver bringup */
#undef PDEBUG
#define PDEBUG(fmt, args...) printk(KERN_INFO fmt, ## args)

#define CUSTOM_IP_BASEADDR 0x43c00000
#define SRAM_IP_HIGHADDR 0x3FFFFFFF		// 1GB
#define SRAM_IP_BASEADDR 0x3FC00000		// 1M word at 32 bits = 4MB
#define SRAM_TR_BASEADDR 0x3FFF0000		// 16k word at 32 bit = 64KB

#define DRIVER_NAME "epsmdrv"				// module name
#define BUF_LEN 80						// max buffer length
#define SUCCESS 0						// success return value

int epsmd_minor = 0;
int epsmd_major = 0;

//#define PAGE_SIZE ((size_t)getpagesize())
//#define PAGE_MASK ((uint64_t)(long)~(PAGE_SIZE - 1))

// ----------------------------------------------------------------------------------------
#define EPSMD_IOCTL_READ 0x100
#define EPSMD_IOCTL_WRITE 0x101

// ----------------------------------------------------------------------------------------

#define MAX_EPCORE  1

// Driver structure
typedef struct __drv_str_s {
    struct cdev *cdev;
    struct device *device;
    dev_t numdev;
    unsigned long id;
    unsigned long base;
    int irq;

    int openCnt;             //number of opened instances

    char devname[255];

    spinlock_t lock;
} __drv_str;

static __drv_str sdrv;

static struct class *epsmdrv_class;


// ----------------------------------------------------------------------------------------
// IOCTLs
struct epsmdrv_regacc {
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
* implements the open system call ******************************************************************************************/
static int epsmdrv_open ( struct inode *inode, struct file *filp )
{
	int minor;
	__drv_str *adev;

	PDEBUG("Opening epsmdrv...\n");
    adev = &sdrv;
    minor = MINOR(inode->i_rdev);
    if ( minor != 0 ) {
        PDEBUG("Error opening driver: minor!=0\n");
        return -ENOENT;
    }
    adev->openCnt++;

    filp->private_data = (void*)adev;

    return 0;
}


/******************************************************************************************
 * implements the close system call
 ******************************************************************************************/
static int epsmdrv_release ( struct inode *inode, struct file *filp )
{
	__drv_str *adev;

    adev = (__drv_str*)filp->private_data;

    if ( adev == NULL ) {
        PDEBUG("Error releasing driver: pointer not NULL\n");
        return -ENOENT;
    }

    adev->openCnt--;
    PDEBUG("epsmdrv closed!\n");
    return 0;
}


/******************************************************************************************
 * implements ioctl system call
 ******************************************************************************************/
static long epsmdrv_ioctl ( struct file *filp, unsigned int cmd, unsigned long arg )
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
        case EPSMD_IOCTL_READ: {
            PDEBUG("ioctl read\n");
            struct epsmdrv_regacc r;
            if (copy_from_user((void*)&r,uarg,sizeof(unsigned int))) {
                ret = -EFAULT;
            } else {
                r.data = read_reg(dev,r.address);
                ret = 0;
                if (copy_to_user(uarg,(void*)&r,sizeof(struct epsmdrv_regacc))) {
                    ret = -EFAULT;
                }
            }
        }
            break;
        case EPSMD_IOCTL_WRITE: {
            PDEBUG("ioctl write\n");
            struct epsmdrv_regacc r;
            if (copy_from_user((void*)&r,uarg,sizeof(struct epsmdrv_regacc))) {
                ret = -EFAULT;
            } else {
                write_reg(dev,r.address,r.data);
                ret = 0;
            }
        }
            break;
        default:
            // No valid IOCTL code
            PDEBUG("No valid IOCTL code\n");
            ret = -EINVAL;
    }
    //spin_unlock_irqrestore(&dev->lock, flags);
    return ret;
}

// Character Device fops structure

struct file_operations epsmdrv_fops = {
    .owner      = THIS_MODULE,
    .open       = epsmdrv_open,
    .release    = epsmdrv_release,
	.unlocked_ioctl = epsmdrv_ioctl,
};
// ----------------------------------------------------------------------------------------

static int epsmdrv_probe(struct platform_device *pdev)
{
	int res;
//	int irq = 0;
	struct resource *io_res;	/*IO mem resources */
	__drv_str *dev;

    dev = &sdrv;
	PDEBUG("Device tree probing...\n");
   	io_res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (io_res == NULL) {
	    printk(KERN_ERR "epsmdrv: failed to get IORESOURCE_MEM\n");
		return -EINVAL;
	}
/*	irq = platform_get_irq(pdev, 0);
    if (irq <= 0) {
	    printk(KERN_ERR "epsmdrv: failed to get IRQ\n");
		return -EINVAL;
    }
    
    PDEBUG("epsmdrv: mapping at 0x%08X irq=%d\n", io_res->start, irq );*/
    PDEBUG("epsmdrv: mapping at 0x%08X\n", io_res->start );
       
    dev->base = (unsigned long)ioremap(io_res->start, resource_size(io_res));
    if ( dev->base == (unsigned long)NULL ) {
        PDEBUG("cannot map!\n");
        return -EINVAL;
    }
//    dev->irq = irq;
	
    //Define char device
    res = alloc_chrdev_region ( &dev->numdev,
            0, // minor number
            1,  // number of devices
            DRIVER_NAME );  // device name
    if ( res < 0 ) {
        PDEBUG("cannot get major device number\n" );
        return -ENODEV;
    }
    dev->cdev = cdev_alloc();
    dev->cdev->ops = &(epsmdrv_fops);
    dev->cdev->owner = THIS_MODULE;
    res = cdev_add(dev->cdev, dev->numdev, 1);

    if ( res ) {
        unregister_chrdev_region ( dev->numdev, 1 );
        return -ENODEV;
    }

    epsmdrv_class = class_create(THIS_MODULE, DRIVER_NAME);
    
    sprintf(dev->devname,"%s%d",DRIVER_NAME,0);
    dev->device = device_create(epsmdrv_class,NULL,MKDEV(MAJOR(dev->numdev),0),dev,dev->devname);
    if ( dev->device == NULL ) {
        PDEBUG("Error: cannot create device!\n" );
        return -ENODEV;
    }
    
    dev->lock = __SPIN_LOCK_UNLOCKED(dev->lock);
    PDEBUG("epsmdrv: device created.\n");
    return 0;
}

static int epsmdrv_remove(struct platform_device *pdev)
{
//	free_irq(sdrv.irq,&sdrv);
    iounmap((void*)sdrv.base);
    
    device_destroy(epsmdrv_class, MKDEV(MAJOR(sdrv.numdev),0));
    class_destroy(epsmdrv_class);
    
	return 0;
}

static struct of_device_id epsmdrv_platform_match[] = {
	{ .compatible = "xlnx,Axi4Lite-Slave-1.0", },
	{ /* end of list */},
};
MODULE_DEVICE_TABLE(of,epsmdrv_platform_match);

static struct platform_driver epsmdrv_driver = {
	.probe		= epsmdrv_probe,
	.remove		= epsmdrv_remove,
	.driver = {
		.name 			= DRIVER_NAME,
		.owner 			= THIS_MODULE,
		.of_match_table	= epsmdrv_platform_match,
	},
};

module_platform_driver(epsmdrv_driver);

/*static int __init epsmdrv_init(void)
{
	int status;
	
	status = platform_driver_register(&epsmdrv_driver);
	if (status == 0){
		PDEBUG("epsmdrv: module registered!\n");
	} else{
		PDEBUG("epsmdrv: module registration failed (%d).\n", status);
	}
	return status;
}


static void __exit epsmdrv_exit(void)
{ 
	platform_driver_unregister(&epsmdrv_driver);
	printk(KERN_ALERT "Goodbye module epsmdrv.\n");
}

module_init(epsmdrv_init);
module_exit(epsmdrv_exit);*/

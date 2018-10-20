#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/uio_driver.h>
#include <linux/slab.h>
#include <linux/platform_device.h>
#include <linux/types.h>
#include <linux/kthread.h>
#include <linux/delay.h>

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Mandeep Sandhu");
MODULE_DESCRIPTION("UIO fake hotplug driver");

static struct uio_info* info;
static struct platform_device *pdev;
static struct task_struct *ts;
static struct timer_list fake_intr_timer;

static int msec_unreg_delay = 3000; // 3 secs
static int msec_uio_intr_delay = 1000; // 1 sec

void unregister_uio(void)
{
    pr_info("Delting fake interrupt timer\n");
    del_timer_sync(&fake_intr_timer);
    if (info) {
        pr_info("Unregistering uio\n");
        uio_unregister_device(info);
        kfree((void *)info->mem[0].addr);
        kfree(info);
        info = 0;
    }
}

int kthread_fn(void *data)
{
    pr_info("Sleeping for %d secs\n", (msec_unreg_delay)/1000);
    msleep(msec_unreg_delay);

    if (kthread_should_stop())
        return 0;

    unregister_uio();
    pr_info("Exiting thread\n");
    ts = 0;
    return 0;
}

void fake_intr_timer_cb(unsigned long data)
{
    if (!ts)
        return;

    pr_info("Firing fake interrupt\n");
    uio_event_notify(info);
    // Setup timer to fire again
    mod_timer(&fake_intr_timer,
               jiffies + msecs_to_jiffies(msec_uio_intr_delay));
}

static int uio_fake_hotplug_open(struct uio_info *info, struct inode *inode)
{
    pr_info("%s called\n", __FUNCTION__);
    return 0;
}

static int uio_fake_hotplug_release(struct uio_info *info, struct inode *inode)
{
    pr_info("%s called\n", __FUNCTION__);
    return 0;
}

static int __init uio_fake_hotplug_init(void)
{

    pdev = platform_device_register_simple("uio_fake_hotplug_platform_device",
                                            0, NULL, 0);
    if (IS_ERR(pdev)) {
        pr_err("Failed to register platform device.\n");
        return -EINVAL;
    }

    info = kzalloc(sizeof(struct uio_info), GFP_KERNEL);
    
    if (!info)
        return -ENOMEM;

    info->name = "uio_fake_hotplug_driver";
    info->version = "0.1";
    info->mem[0].addr = (phys_addr_t) kzalloc(PAGE_SIZE, GFP_ATOMIC);
    if (!info->mem[0].addr)
        goto uiomem;
    info->mem[0].memtype = UIO_MEM_LOGICAL;
    info->mem[0].size = PAGE_SIZE;
    info->irq = UIO_IRQ_CUSTOM;
    info->handler = 0;
    info->open = uio_fake_hotplug_open;
    info->release = uio_fake_hotplug_release;
	
    if(uio_register_device(&pdev->dev, info)) {
        pr_err("Unable to register UIO device!\n");
        goto devmem;
    }

    ts = kthread_run(kthread_fn, NULL, "uio_unreg_kthread");

    timer_setup(&fake_intr_timer, (void*)fake_intr_timer_cb, 0 );
    if (mod_timer(&fake_intr_timer, (jiffies +
                                     msecs_to_jiffies(msec_uio_intr_delay)))) {
        pr_err("Error setting up fake interrupt timer");
        goto devmem;
    }

    pr_info("Fake uio hotplug driver loaded\n");
    return 0;

devmem:
    kfree((void *)info->mem[0].addr);
uiomem:
    kfree(info);
    
    return -ENODEV;
}

static void __exit uio_fake_hotplug_cleanup(void)
{
    pr_info("Cleaning up module.\n");

    if (ts) {
        del_timer_sync(&fake_intr_timer);
        kthread_stop(ts);
        unregister_uio();
    }

    if (pdev)
        platform_device_unregister(pdev);
}

module_init(uio_fake_hotplug_init);
module_exit(uio_fake_hotplug_cleanup);

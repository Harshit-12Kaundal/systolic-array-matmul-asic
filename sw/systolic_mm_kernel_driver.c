/*
 * systolic_mm_kernel_driver.c
 *
 * Linux kernel character device driver for the systolic-array matrix
 * multiplier accelerator. Exposes /dev/systolic_mm to user space via
 * standard open()/ioctl() calls. Binds to the platform device generated
 * from the "harshit,systolic-mm-0.1" device-tree node (see the QEMU
 * device model in systolic_mm_qemu.c for the matching hardware side).
 *
 * Interrupt-driven: launch_matmul() starts the operation and sleeps on a
 * wait queue; the ISR wakes it when the hardware/model raises IRQ.
 */

#include <linux/module.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/uaccess.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/platform_device.h>
#include <linux/interrupt.h>
#include <linux/wait.h>
#include <linux/io.h>
#include <linux/slab.h>

#include "systolic_mm_regs.h"
#include "systolic_mm_ioctl.h"

#define DRIVER_NAME "systolic_mm"

struct systolic_mm_dev {
    struct cdev cdev;
    void __iomem *base;
    int irq;
    wait_queue_head_t wq;
    bool op_done;
    struct device *dev;
};

static dev_t systolic_mm_devno;
static struct class *systolic_mm_class;
static struct systolic_mm_dev *g_smm; /* single-instance device, simple case */

static irqreturn_t systolic_mm_isr(int irq, void *dev_id)
{
    struct systolic_mm_dev *smm = dev_id;

    /* Reading STATUS clears done_sticky/irq on real HW and on the model */
    ioread32(smm->base + SYSMM_STATUS_ADDR);

    smm->op_done = true;
    wake_up_interruptible(&smm->wq);
    return IRQ_HANDLED;
}

static void systolic_mm_write_matrix(struct systolic_mm_dev *smm,
                                      unsigned int base_addr,
                                      const uint8_t *bytes, int num_words)
{
    int w, b;
    for (w = 0; w < num_words; w++) {
        uint32_t word = 0;
        for (b = 0; b < 4; b++)
            word |= ((uint32_t)bytes[w * 4 + b]) << (8 * b);
        iowrite32(word, smm->base + base_addr + 4 * w);
    }
}

static void systolic_mm_read_result(struct systolic_mm_dev *smm,
                                     int32_t *out)
{
    int w;
    for (w = 0; w < SYSMM_NUM_C_WORDS; w++)
        out[w] = (int32_t)ioread32(smm->base + SYSMM_C_BASE + 4 * w);
}

static long systolic_mm_ioctl(struct file *filp, unsigned int cmd, unsigned long arg)
{
    struct systolic_mm_dev *smm = filp->private_data;
    struct systolic_mm_matmul_args uargs;
    int32_t c_words[SYSMM_NUM_C_WORDS];
    int ret;

    switch (cmd) {
    case SYSTOLIC_MM_IOC_LAUNCH:
        if (copy_from_user(&uargs, (void __user *)arg, sizeof(uargs)))
            return -EFAULT;

        smm->op_done = false;

        systolic_mm_write_matrix(smm, SYSMM_A_BASE, uargs.a_bytes, SYSMM_NUM_A_WORDS);
        systolic_mm_write_matrix(smm, SYSMM_B_BASE, uargs.b_bytes, SYSMM_NUM_B_WORDS);

        iowrite32(SYSMM_CTRL_START_BIT, smm->base + SYSMM_CTRL_ADDR);

        /* Interrupt-driven wait, NOT a polling loop -- this is the point */
        ret = wait_event_interruptible_timeout(smm->wq, smm->op_done, HZ);
        if (ret == 0)
            return -ETIMEDOUT;
        if (ret < 0)
            return ret; /* interrupted by a signal */

        systolic_mm_read_result(smm, c_words);
        if (copy_to_user(uargs.c_words_out, c_words, sizeof(c_words)))
            return -EFAULT;

        return 0;

    default:
        return -ENOTTY;
    }
}

static int systolic_mm_open(struct inode *inode, struct file *filp)
{
    filp->private_data = g_smm;
    return 0;
}

static const struct file_operations systolic_mm_fops = {
    .owner          = THIS_MODULE,
    .open           = systolic_mm_open,
    .unlocked_ioctl = systolic_mm_ioctl,
};

static int systolic_mm_probe(struct platform_device *pdev)
{
    struct systolic_mm_dev *smm;
    struct resource *res;
    int ret;

    smm = devm_kzalloc(&pdev->dev, sizeof(*smm), GFP_KERNEL);
    if (!smm)
        return -ENOMEM;

    res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
    smm->base = devm_ioremap_resource(&pdev->dev, res);
    if (IS_ERR(smm->base))
        return PTR_ERR(smm->base);

    smm->irq = platform_get_irq(pdev, 0);
    if (smm->irq < 0)
        return smm->irq;

    init_waitqueue_head(&smm->wq);
    smm->dev = &pdev->dev;

    ret = devm_request_irq(&pdev->dev, smm->irq, systolic_mm_isr, 0,
                            DRIVER_NAME, smm);
    if (ret)
        return ret;

    cdev_init(&smm->cdev, &systolic_mm_fops);
    ret = cdev_add(&smm->cdev, systolic_mm_devno, 1);
    if (ret)
        return ret;

    device_create(systolic_mm_class, NULL, systolic_mm_devno, NULL, DRIVER_NAME);

    g_smm = smm;
    platform_set_drvdata(pdev, smm);

    dev_info(&pdev->dev, "systolic_mm: probed, irq=%d, base=%p\n", smm->irq, smm->base);
    return 0;
}

static void systolic_mm_remove(struct platform_device *pdev)
{
    struct systolic_mm_dev *smm = platform_get_drvdata(pdev);
    device_destroy(systolic_mm_class, systolic_mm_devno);
    cdev_del(&smm->cdev);
}

static const struct of_device_id systolic_mm_of_match[] = {
    { .compatible = "harshit,systolic-mm-0.1" },
    {}
};
MODULE_DEVICE_TABLE(of, systolic_mm_of_match);

static struct platform_driver systolic_mm_driver = {
    .driver = {
        .name = DRIVER_NAME,
        .of_match_table = systolic_mm_of_match,
    },
    .probe  = systolic_mm_probe,
    .remove = systolic_mm_remove,
};

static int __init systolic_mm_init(void)
{
    int ret = alloc_chrdev_region(&systolic_mm_devno, 0, 1, DRIVER_NAME);
    if (ret)
        return ret;

    systolic_mm_class = class_create(DRIVER_NAME);
    if (IS_ERR(systolic_mm_class)) {
        unregister_chrdev_region(systolic_mm_devno, 1);
        return PTR_ERR(systolic_mm_class);
    }

    return platform_driver_register(&systolic_mm_driver);
}

static void __exit systolic_mm_exit(void)
{
    platform_driver_unregister(&systolic_mm_driver);
    class_destroy(systolic_mm_class);
    unregister_chrdev_region(systolic_mm_devno, 1);
}

module_init(systolic_mm_init);
module_exit(systolic_mm_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Harshit");
MODULE_DESCRIPTION("Driver for the systolic array INT8 matmul accelerator");

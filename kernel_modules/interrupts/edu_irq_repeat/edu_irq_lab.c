// SPDX-License-Identifier: GPL-2.0
#include <linux/device.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/pci.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/sysfs.h>

#define DRIVER_NAME     "edu_irq_lab"
#define EDU_IRQ_STATUS  0x24
#define EDU_IRQ_RAISE   0x60
#define EDU_IRQ_ACK     0x64

struct edu_device {
    void __iomem *regs;
    struct mutex trigger_lock;
    int irq;
};

static irqreturn_t edu_interrupt(int irq, void *data)
{
    struct edu_device *edu = data;
    u32 pending = readl(edu->regs + EDU_IRQ_STATUS);

    if (!pending)
        return IRQ_NONE;

    writel(pending, edu->regs + EDU_IRQ_ACK);
    pr_info(DRIVER_NAME ": handled IRQ %d, status=%#x\n", irq, pending);
    return IRQ_HANDLED;
}

static ssize_t raise_irq_store(struct device *dev,
                               struct device_attribute *attr,
                               const char *buf, size_t count)
{
    struct edu_device *edu = dev_get_drvdata(dev);
    ssize_t result = count;

    if (!sysfs_streq(buf, "1"))
        return -EINVAL;

    mutex_lock(&edu->trigger_lock);
    if (readl(edu->regs + EDU_IRQ_STATUS)) {
        result = -EBUSY;
    } else {
        writel(1, edu->regs + EDU_IRQ_RAISE);
        readl(edu->regs + EDU_IRQ_STATUS); /* Flush the posted write. */
    }
    mutex_unlock(&edu->trigger_lock);
    return result;
}
static DEVICE_ATTR_WO(raise_irq);

static struct attribute *edu_attrs[] = {
    &dev_attr_raise_irq.attr,
    NULL,
};
ATTRIBUTE_GROUPS(edu);

static int edu_probe(struct pci_dev *pdev, const struct pci_device_id *id)
{
    struct edu_device *edu;
    int ret;

    edu = devm_kzalloc(&pdev->dev, sizeof(*edu), GFP_KERNEL);
    if (!edu)
        return -ENOMEM;

    mutex_init(&edu->trigger_lock);
    ret = pci_enable_device_mem(pdev);
    if (ret)
        return ret;

    pci_set_master(pdev);
    ret = pci_request_region(pdev, 0, DRIVER_NAME);
    if (ret)
        goto disable_device;

    edu->regs = pci_iomap(pdev, 0, 0);
    if (!edu->regs) {
        ret = -ENOMEM;
        goto release_region;
    }

    ret = pci_alloc_irq_vectors(pdev, 1, 1, PCI_IRQ_MSI);
    if (ret < 0)
        goto unmap;

    edu->irq = pci_irq_vector(pdev, 0);
    ret = request_irq(edu->irq, edu_interrupt, 0, DRIVER_NAME, edu);
    if (ret)
        goto free_vectors;

    pci_set_drvdata(pdev, edu);
    return 0;

free_vectors:
    pci_free_irq_vectors(pdev);
unmap:
    pci_iounmap(pdev, edu->regs);
release_region:
    pci_release_region(pdev, 0);
disable_device:
    pci_clear_master(pdev);
    pci_disable_device(pdev);
    return ret;
}

static void edu_remove(struct pci_dev *pdev)
{
    struct edu_device *edu = pci_get_drvdata(pdev);

    free_irq(edu->irq, edu);
    pci_free_irq_vectors(pdev);
    pci_iounmap(pdev, edu->regs);
    pci_release_region(pdev, 0);
    pci_clear_master(pdev);
    pci_disable_device(pdev);
}

static const struct pci_device_id edu_ids[] = {
    { PCI_DEVICE(0x1234, 0x11e8) },
    { }
};
MODULE_DEVICE_TABLE(pci, edu_ids);

static struct pci_driver edu_driver = {
    .name       = DRIVER_NAME,
    .id_table   = edu_ids,
    .probe      = edu_probe,
    .remove     = edu_remove,
    .dev_groups = edu_groups,
};
module_pci_driver(edu_driver);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Raise QEMU EDU interrupts from sysfs");

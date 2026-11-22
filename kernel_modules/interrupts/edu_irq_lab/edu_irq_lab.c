#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/pci.h>

#define DRIVER_NAME     "edu_irq_lab"
#define EDU_IRQ_STATUS  0x24
#define EDU_IRQ_RAISE   0x60
#define EDU_IRQ_ACK     0x64

static irqreturn_t edu_interrupt(int irq, void *dev_id)
{
    void __iomem *regs = dev_id;
    u32 pending = readl(regs + EDU_IRQ_STATUS);

    if (pending == 0)
        return IRQ_NONE;

    writel(pending, regs + EDU_IRQ_ACK);
    pr_info(DRIVER_NAME ": handled IRQ %d, status=%#x\n", irq, pending);

    return IRQ_HANDLED;
}

static int edu_probe(struct pci_dev *pdev, const struct pci_device_id *id)
{
    void __iomem *regs;
    int irq;
    int ret;

    ret = pci_enable_device_mem(pdev);
    if (ret)
        return ret;

    pci_set_master(pdev);

    ret = pci_request_region(pdev, 0, DRIVER_NAME);
    if (ret)
        goto disable_device;

    regs = pci_iomap(pdev, 0, 0);
    if (!regs) {
        ret = -ENOMEM;
        goto release_region;
    }

    ret = pci_alloc_irq_vectors(pdev, 1, 1, PCI_IRQ_MSI);
    if (ret < 0) {
        dev_err(&pdev->dev, "Could not allocate MSI vector: %d\n", ret);
        goto unmap_regs;
    }

    irq = pci_irq_vector(pdev, 0);
    ret = request_irq(irq, edu_interrupt, 0, DRIVER_NAME, regs);
    if (ret) {
        dev_err(&pdev->dev, "Could not register IRQ %d: %d\n", irq, ret);
        goto free_vectors;
    }

    pci_set_drvdata(pdev, regs);

    /* The handler is registered. Ask EDU to raise one interrupt. */
    writel(1, regs + EDU_IRQ_RAISE);
    return 0;

free_vectors:
    pci_free_irq_vectors(pdev);
unmap_regs:
    pci_iounmap(pdev, regs);
release_region:
    pci_release_region(pdev, 0);
disable_device:
    pci_disable_device(pdev);
    return ret;
}

static void edu_remove(struct pci_dev *pdev)
{
    void __iomem *regs = pci_get_drvdata(pdev);

    free_irq(pci_irq_vector(pdev, 0), regs);
    pci_free_irq_vectors(pdev);
    pci_iounmap(pdev, regs);
    pci_release_region(pdev, 0);
    pci_disable_device(pdev);
}

static const struct pci_device_id edu_ids[] = {
    { PCI_DEVICE(0x1234, 0x11e8) },
    { }
};
MODULE_DEVICE_TABLE(pci, edu_ids);

static struct pci_driver edu_driver = {
    .name     = DRIVER_NAME,
    .id_table = edu_ids,
    .probe    = edu_probe,
    .remove   = edu_remove,
};

module_pci_driver(edu_driver);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("One-interrupt lab for the QEMU EDU PCI device");

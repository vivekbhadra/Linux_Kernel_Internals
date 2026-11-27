#include <linux/device.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/sysfs.h>

#define DRIVER_NAME "edu_irq_lab"
#define EDU_IRQ_STATUS 0x24
#define EDU_IRQ_RAISE 0x60
#define EDU_IRQ_ACK 0x64

/**
 * edu_interrupt - Interrupt handler for the EDU device
 * @irq: IRQ number
 * @dev_id: Device identifier
 *
 * This function is called when an interrupt is received from the EDU device.
 *
 * Return: IRQ_HANDLED if the interrupt was handled, IRQ_NONE if it was
 * spurious.
 */
static irqreturn_t edu_interrupt(int irq, void *dev_id)
{
	void __iomem *regs = dev_id;
	u32 pending = readl(regs + EDU_IRQ_STATUS);

	if (!pending)
		return IRQ_NONE;

	pr_info(DRIVER_NAME
		": IRQ %d received; pending status=%#x, acknowledging it\n",
		irq, pending);
	// Acknowledge the interrupt by writing back the pending status
	writel(pending, regs + EDU_IRQ_ACK);
	pr_info(DRIVER_NAME ": IRQ %d handled\n", irq);

	return IRQ_HANDLED;
}

/* A write of "1" asks the device to raise an interrupt. */
static ssize_t raise_irq_store(struct device *dev,
			       struct device_attribute *attr, const char *buf,
			       size_t count)
{
	void __iomem *regs = pci_get_drvdata(to_pci_dev(dev));

	if (!sysfs_streq(buf, "1"))
		return -EINVAL;

	dev_info(dev, "Writing to the device's interrupt-raise register\n");
	writel(1, regs + EDU_IRQ_RAISE);
	return count;
}
static DEVICE_ATTR_WO(raise_irq);

/**
 * edu_probe - Probe function for the EDU device
 * @pdev: PCI device structure
 * @id: PCI device ID structure
 *
 * This function is called when a matching PCI device is found.
 *
 * Return: 0 on success, negative error code on failure.
 */
static int edu_probe(struct pci_dev *pdev, const struct pci_device_id *id)
{
	void __iomem *regs;
	int irq;
	int ret;

	dev_info(&pdev->dev, "Matching EDU device found; starting setup\n");

	ret = pci_enable_device_mem(pdev);
	if (ret)
		return ret;

	dev_info(&pdev->dev, "Enabled PCI memory access\n");
	pci_set_master(pdev);

	ret = pci_request_region(pdev, 0, DRIVER_NAME);
	if (ret)
		goto disable_device;

	regs = pci_iomap(pdev, 0, 0);
	if (!regs) {
		ret = -ENOMEM;
		goto release_region;
	}
	dev_info(&pdev->dev, "Mapped device registers from PCI BAR 0\n");

	ret = pci_alloc_irq_vectors(pdev, 1, 1, PCI_IRQ_MSI);
	if (ret < 0) {
		dev_err(&pdev->dev, "Could not allocate MSI vector: %d\n", ret);
		goto unmap_regs;
	}

	irq = pci_irq_vector(pdev, 0);
	dev_info(&pdev->dev, "Allocated MSI interrupt vector (IRQ %d)\n", irq);
	ret = request_irq(irq, edu_interrupt, 0, DRIVER_NAME, regs);
	if (ret) {
		dev_err(&pdev->dev, "Could not register IRQ %d: %d\n", irq,
			ret);
		goto free_vectors;
	}
	dev_info(&pdev->dev, "Registered IRQ handler\n");

	pci_set_drvdata(pdev, regs);
	ret = device_create_file(&pdev->dev, &dev_attr_raise_irq);
	if (ret)
		goto free_handler;
	dev_info(
		&pdev->dev,
		"Created raise_irq sysfs control; write 1 to trigger an interrupt\n");

	/* Raise one interrupt when the driver binds. */
	dev_info(&pdev->dev, "Raising a startup interrupt\n");
	writel(1, regs + EDU_IRQ_RAISE);
	return 0;

free_handler:
	free_irq(irq, regs);
	pci_set_drvdata(pdev, NULL);
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

	dev_info(&pdev->dev,
		 "Removing driver; releasing interrupt and PCI resources\n");
	device_remove_file(&pdev->dev, &dev_attr_raise_irq);
	free_irq(pci_irq_vector(pdev, 0), regs);
	pci_set_drvdata(pdev, NULL);
	pci_free_irq_vectors(pdev);
	pci_iounmap(pdev, regs);
	pci_release_region(pdev, 0);
	pci_disable_device(pdev);
	dev_info(&pdev->dev, "Driver removed\n");
}

static const struct pci_device_id edu_ids[] = { { PCI_DEVICE(0x1234, 0x11e8) },
						{} };
MODULE_DEVICE_TABLE(pci, edu_ids);

static struct pci_driver edu_driver = {
	.name = DRIVER_NAME,
	.id_table = edu_ids,
	.probe = edu_probe,
	.remove = edu_remove,
};
module_pci_driver(edu_driver);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("QEMU EDU interrupt lab with startup and sysfs triggers");

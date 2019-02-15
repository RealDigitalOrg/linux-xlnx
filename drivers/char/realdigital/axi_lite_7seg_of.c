/*
 * 7-Segment display AXI Lite driver
 *
 * 2018 (c) Real Digital LLC
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation version 2.
 *
 * This program is distributed "as is" WITHOUT ANY WARRANTY of any
 * kind, whether express or implied; without even the implied warranty
 * of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/module.h>
#include <linux/device.h>
#include <linux/slab.h>
#include <linux/platform_device.h>
#include <linux/of.h>
#include <linux/of_irq.h>
#include <linux/of_address.h>
#include <linux/of_device.h>
#include <linux/of_platform.h>
#include <linux/uio_driver.h>

#define DRIVER_NAME		"axi_lite_7seg_of"

struct axi_lite_7seg_info {
	struct resource res;
	struct uio_info info;
};

static irqreturn_t axi_lite_7seg_handler(int irq, struct uio_info *dev_info)
{
	/*
	 * Do nothing. The interrupt is edge triggered, so there is no
	 * need to turn it off.
	*/

	return IRQ_HANDLED;
}

static int axi_lite_7seg_of_probe(struct platform_device *op)
{
	struct device *dev = &op->dev;
	struct axi_lite_7seg_info *myinfo;
	struct uio_info *info;

	int rc;
	int irq;

	/* Allocate the driver data region */
	myinfo = kzalloc(sizeof(*myinfo), GFP_KERNEL);
	if (!myinfo) {
		dev_err(dev, "Could not allocate local record\n");
		return -ENOMEM;
	}

	rc = of_address_to_resource(dev->of_node, 0, &myinfo->res);
	if (rc) {
		dev_err(dev, "Failed to obtain device tree info\n");
		goto noresource;
	}

	if  (!request_mem_region(myinfo->res.start,
				 resource_size(&myinfo->res),
				 DRIVER_NAME)) {
		dev_err(dev, "request of_memory_region failed\n");
		rc = -EBUSY;
		goto noresource;
	}

	irq = irq_of_parse_and_map(dev->of_node, 0);

	info = &myinfo->info;

	info->name = DRIVER_NAME;
	info->version = "1.1";
	info->mem[0].addr = myinfo->res.start;
	info->mem[0].size = resource_size(&myinfo->res);
	info->mem[0].memtype = UIO_MEM_PHYS;
	info->handler = axi_lite_7seg_handler;

	if ((irq == NO_IRQ) || !irq)
		info->irq = UIO_IRQ_NONE;
	else
		info->irq = irq;

	if (uio_register_device(dev, info))
		goto failed_register;

	dev_set_drvdata(dev, myinfo);

	return 0; /* Success */

failed_register:
	release_mem_region(myinfo->res.start,
			   resource_size(&myinfo->res));

noresource:
	kfree(myinfo);
	return rc;
}

static int axi_lite_7seg_of_remove(struct platform_device *op)
{
	struct device *dev = &op->dev;
	struct axi_lite_7seg_info *myinfo = dev_get_drvdata(dev);

	uio_unregister_device(&myinfo->info);
	release_mem_region(myinfo->res.start,
			   resource_size(&myinfo->res));

	kfree(myinfo);

	return 0;
}

/* Match table for of_platform binding */
static struct of_device_id axi_lite_7seg_of_match[] = {
	{ .compatible = "xlnx,display-7seg-cntr-1.0", },
	{ .compatible = "realdigital,axi-lite-7seg-of-1.00.a", },
	{},
};
MODULE_DEVICE_TABLE(of, axi_lite_7seg_of_match);

static struct platform_driver axi_lite_7seg_of_driver = {
	.probe = axi_lite_7seg_of_probe,
	.remove = axi_lite_7seg_of_remove,
	.driver = {
		.name = DRIVER_NAME,
		.owner = THIS_MODULE,
		.of_match_table = axi_lite_7seg_of_match,
	},
};

module_platform_driver(axi_lite_7seg_of_driver);

MODULE_AUTHOR("Rick Hoover, Real Digital LLC");
MODULE_DESCRIPTION("AXI Lite 7-segment display driver");
MODULE_LICENSE("GPL");
MODULE_VERSION("1.1");

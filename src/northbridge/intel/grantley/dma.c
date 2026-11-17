/* SPDX-License-Identifier: GPL-2.0-only */

#include <arch/mmio.h>
#include <console/console.h>
#include <device/device.h>
#include <device/pci.h>
#include <device/pci_ids.h>
#include <device/pci_ops.h>
#include <device/resource.h>
#include <types.h>

#define DMA_DCAOFFSET		0x14	/* word: offset of the DCA block in the channel window */
#define DMA_DCA_VERSION		0x00
#define DMA_DCA_VERSION_MAJOR	0xf0
#define DMA_DCA_VERSION_1	0x10
#define DMA_DCA_TAG_MAP_LOW	0x10
#define DMA_DCA_TAG_MAP_HIGH	0x14
#define DMA_DCA_TAG_MAP_SELECT	0x40	/* take this APIC ID bit, not its inverse */
#define DMA_DCA_TAG_MAP_TAGS	5	/* tags 5 to 7 are hardwired to zero */

static void dma_program_dca_tag_map(struct device *dev)
{
	struct resource *res;
	unsigned int tag;
	u16 dca_offset;
	u8 version;
	u64 map = 0;
	void *bar;

	res = probe_resource(dev, PCI_BASE_ADDRESS_0);
	if (!res || !res->base || res->size < DMA_DCA_TAG_MAP_HIGH + sizeof(u32))
		return;

	bar = (void *)(uintptr_t)res->base;
	dca_offset = read16(bar + DMA_DCAOFFSET);
	if (!dca_offset || dca_offset > res->size - (DMA_DCA_TAG_MAP_HIGH + sizeof(u32))) {
		printk(BIOS_WARNING, "%s: invalid DCA block offset %04x\n", dev_path(dev), dca_offset);
		return;
	}

	/* The DCA block version is independent of the IOAT engine version. */
	version = read8(bar + dca_offset + DMA_DCA_VERSION);
	if ((version & DMA_DCA_VERSION_MAJOR) != DMA_DCA_VERSION_1) {
		printk(BIOS_WARNING, "%s: unsupported DCA version %02x\n", dev_path(dev), version);
		return;
	}

	for (tag = 0; tag < DMA_DCA_TAG_MAP_TAGS; tag++)
		map |= (u64)(DMA_DCA_TAG_MAP_SELECT | tag) << (8 * tag);

	write32(bar + dca_offset + DMA_DCA_TAG_MAP_LOW, (u32)map);
	write32(bar + dca_offset + DMA_DCA_TAG_MAP_HIGH, (u32)(map >> 32));

	if (read32(bar + dca_offset + DMA_DCA_TAG_MAP_LOW) != (u32)map ||
	    read32(bar + dca_offset + DMA_DCA_TAG_MAP_HIGH) != (u32)(map >> 32))
		printk(BIOS_WARNING, "%s: DCA tag map write did not stick\n", dev_path(dev));
}

/*
 * Runs from dev_initialize(), i.e. after the channel window has been assigned
 * and its memory decode enabled; the .enable hook is not used because it also
 * runs while the bus is still being probed.
 */
static void dma_init(struct device *dev)
{
	if (!dev->enabled)
		return;

	dma_program_dca_tag_map(dev);
}

static struct device_operations dma_ops = {
	.read_resources		= pci_dev_read_resources,
	.set_resources		= pci_dev_set_resources,
	.enable_resources	= pci_dev_enable_resources,
	.init			= dma_init,
	.ops_pci		= &pci_dev_ops_pci,
};

static const unsigned short pci_device_ids[] = {
	0x2f20, /* Xeon E5 v3 DMA channel 0 */
	0x2f21, /* Xeon E5 v3 DMA channel 1 */
	0x2f22, /* Xeon E5 v3 DMA channel 2 */
	0x2f23, /* Xeon E5 v3 DMA channel 3 */
	0x2f24, /* Xeon E5 v3 DMA channel 4 */
	0x2f25, /* Xeon E5 v3 DMA channel 5 */
	0x2f26, /* Xeon E5 v3 DMA channel 6 */
	0x2f27, /* Xeon E5 v3 DMA channel 7 */
	0
};

static const struct pci_driver grantley_dma __pci_driver = {
	.ops		= &dma_ops,
	.vendor		= PCI_VID_INTEL,
	.devices	= pci_device_ids,
};

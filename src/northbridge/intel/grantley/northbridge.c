/* SPDX-License-Identifier: GPL-2.0-only */

#include <assert.h>
#include <bootstate.h>
#include <commonlib/helpers.h>
#include <console/console.h>
#include <device/device.h>
#include <device/mmio.h>
#include <device/pci.h>
#include <device/pci_ids.h>
#include <device/pci_ops.h>
#include <timer.h>
#include <cpu/cpu.h>
#include <cpu/intel/grantley/grantley.h>
#include <cpu/intel/grantley/grantley_routing.h>
#include <acpi/acpi.h>
#include <acpi/acpigen_pci.h>
#include <arch/ioapic.h>
#include <cpu/intel/microcode.h>
#include <cpu/x86/msr.h>
#include <cpu/x86/smm.h>
#include <types.h>
#include "chip.h"
#include "platform.h"
#include "memmap.h"
#include <southbridge/intel/lynxpoint/pch.h>

#define GRANTLEY_MMIOL_END		0xfc000000ULL
/* Stock firmware places the 8 KiB VT-d BAR at the top of MMIOL. */
#define GRANTLEY_VTD_BASE		0xfbffc000
#define IIO_VTBAR			0x180
#define  IIO_VTBAR_EN			(1 << 0)
#define VTD_ECAP			0x10
#define  VTD_ECAP_C			(1 << 0)
#define IIO_IOAPIC_ABAR			0x40
#define  IIO_IOAPIC_ABAR_EN		(1 << 15)
/* Stock firmware places the IIO IOAPIC right above the PCH one. */
#define IIO_IOAPIC_ABAR_STOCK		(IIO_IOAPIC_ABAR_EN | 0x010)
#define IIO_IOAPIC_GSI_BASE		24
#define IIO_IOAPIC_ID			2
#define IOAPIC_INDEX			0x00
#define IOAPIC_DATA			0x10
#define IOAPIC_ID_REG			0x00
#define IOAPIC_ID_MASK			(0xf << 24)

/* Top of the low MMIO window, 1 MiB granularity. */
#define IIO_MMIO_TOP			0xa4
#define IIO_MMIO_TOP_APIC		0xfef00000

#define GRANTLEY_MMIOH_BASE		0x380000000000ULL
#define GRANTLEY_MMIOH_END		0x384000000000ULL

/*
 * Hook called by coreboot microcode update subsystem in ramstage.
 * If the Haswell-EP turbo unlock is requested, skip updating microcode so
 * the CPU retains the unpatched state required for all-core turbo boost.
 * Other models have no unlock sequence here, so their microcode is updated.
 */
int soc_skip_ucode_update(u32 current_patch_id, u32 new_patch_id)
{
	struct grantley_cpu_params params;
	mainboard_cpu_init_params(&params);

	if (params.enable_turbo_unlock) {
		uint32_t sig = cpuid_eax(1);
		if (grantley_cpu_model(sig) == GRANTLEY_HASWELL_EP) {
			printk(BIOS_INFO, "Grantley: Haswell-EP Turbo Unlock enabled; skipping microcode update\n");
			return 1;
		}
		printk(BIOS_WARNING,
		       "Grantley: Turbo Unlock is implemented for Haswell-EP only\n");
	}
	return 0;
}

static void grantley_domain_read_resources(struct device *dev)
{
	const struct grantley_memory_map *map = grantley_get_memory_map();
	int idx = 10;

	/* The legacy I/O range below 0x1000 is decoded but not a PCI window. */
	struct resource *res = new_resource(dev, 3);
	res->base = 0;
	res->size = 0x1000;
	res->limit = 0xfff;
	res->flags = IORESOURCE_IO | IORESOURCE_SUBTRACTIVE | IORESOURCE_ASSIGNED;

	/* The windows the uncore initializer programs into the IIO for socket 0. */
	domain_io_window_from_to(dev, 0, 0x1000, 0x10000);
	domain_mem_window_from_to(dev, 1, GRANTLEY_ECAM_END, GRANTLEY_MMIOL_END);
	domain_mem_window_from_to(dev, 2, GRANTLEY_MMIOH_BASE, GRANTLEY_MMIOH_END);
	/* Report ECAM as reserved; the MMIOL window starts right above it. */
	mmio_from_to(dev, idx++, GRANTLEY_ECAM_BASE, GRANTLEY_ECAM_END);
	mmio_from_to(dev, idx++, 0xa0000, 0xc0000);
	if (pci_s_read_config32(PCI_DEV(0, 5, 0), IIO_VTBAR) & IIO_VTBAR_EN)
		mmio_from_to(dev, idx++, GRANTLEY_VTD_BASE, GRANTLEY_MMIOL_END);

	for (unsigned int i = 0; i < map->count; i++) {
		const struct grantley_memory_range *range = &map->ranges[i];
		uint64_t end = range->base + range->size;
		uint64_t base = range->base;
		bool reserved = range->type == GRANTLEY_MEMORY_RESERVED_RAM;

		/* Do not turn the VGA aperture or option-ROM area into allocatable RAM. */
		if (base < 0xa0000) {
			if (reserved)
				reserved_ram_from_to(dev, idx++, base, MIN(end, 0xa0000));
			else
				ram_from_to(dev, idx++, base, MIN(end, 0xa0000));
		}
		if (base < MiB && end > 0xc0000)
			reserved_ram_from_to(dev, idx++, MAX(base, 0xc0000), MIN(end, MiB));
		base = MAX(base, MiB);
		if (base >= end)
			continue;
		/* Reserved DRAM, including SMRAM, remains WB for x86_setup_mtrrs(). */
		if (reserved)
			reserved_ram_from_to(dev, idx++, base, end);
		else
			ram_from_to(dev, idx++, base, end);
	}
}

_Static_assert(CONFIG_ECAM_MMCONF_BASE_ADDRESS == GRANTLEY_ECAM_BASE,
	       "ECAM base must match the bootblock route");

static void grantley_domain_scan_bus(struct device *dev)
{
	/* Bus 0xff carries the socket 0 uncore devices, as on stock firmware. */
	dev->downstream->max_subordinate = GRANTLEY_WARM_UNCORE_BUS - 1;
	pci_host_bridge_scan_bus(dev);
}

static uintptr_t iio_ioapic_addr(void)
{
	const u16 abar = pci_s_read_config16(PCI_DEV(0, 5, 4), IIO_IOAPIC_ABAR);

	if (!(abar & IIO_IOAPIC_ABAR_EN))
		return 0;
	return IO_APIC_ADDR | ((abar & 0xfff) << 8);
}

static struct device_operations iio_ioapic_ops;

static unsigned long grantley_fill_dmar(unsigned long current)
{
	const unsigned long tmp = current;
	struct device *ioapic = NULL;

	current += acpi_create_dmar_drhd_4k(current, DRHD_INCLUDE_PCI_ALL, 0,
					    GRANTLEY_VTD_BASE);
	current += acpi_create_dmar_ds_ioapic_from_hw(current, IO_APIC_ADDR,
						      PCH_IOAPIC_PCI_BUS,
						      PCH_IOAPIC_PCI_SLOT, 0);
	while ((ioapic = dev_find_path(ioapic, DEVICE_PATH_IOAPIC))) {
		const struct device *pci = ioapic->upstream ? ioapic->upstream->dev : NULL;

		if (!pci || !is_pci(pci) || pci->ops != &iio_ioapic_ops)
			continue;
		/* This DRHD covers only socket zero's PCI segment and domain. */
		if (!pci->upstream || pci->upstream->segment_group ||
		    pci->upstream->dev->path.type != DEVICE_PATH_DOMAIN ||
		    dev_get_domain_id(pci) != 0)
			continue;
		/* Use the ID announced in the MADT; the OS matches the tables by it. */
		current += acpi_create_dmar_ds_ioapic(current, ioapic->path.ioapic.ioapic_id,
						      pci->upstream->secondary,
						      PCI_SLOT(pci->path.pci.devfn),
						      PCI_FUNC(pci->path.pci.devfn));
	}
	/* The PCH has one HPET block; all of its timers share one requester ID. */
	current += acpi_create_dmar_ds_msi_hpet(current, 0, PCH_HPET_PCI_BUS,
						PCH_HPET_PCI_SLOT, 0);
	acpi_dmar_drhd_fixup(tmp, current);

	return current;
}

/*
 * The VT-d unit reports no coherent access to its paging structures and
 * interrupt remapping table until firmware sets ECAP.C, so the OS must
 * flush every update to them out of the CPU caches. Stock firmware sets
 * the bit; do the same.
 */
static void grantley_vtd_set_coherency(void *unused)
{
	const uintptr_t ecap = GRANTLEY_VTD_BASE + VTD_ECAP;
	u32 value;

	if (!(pci_s_read_config32(PCI_DEV(0, 5, 0), IIO_VTBAR) & IIO_VTBAR_EN))
		return;

	value = read32p(ecap);
	if (value == UINT32_MAX) {
		printk(BIOS_ERR, "Grantley: VT-d ECAP does not decode\n");
		return;
	}

	write32p(ecap, value | VTD_ECAP_C);
	printk(BIOS_DEBUG, "Grantley: VT-d ECAP %08x -> %08x\n", value, read32p(ecap));
}

BOOT_STATE_INIT_ENTRY(BS_DEV_INIT, BS_ON_EXIT, grantley_vtd_set_coherency, NULL);

void soc_fill_fadt(acpi_fadt_t *fadt)
{
	fadt->flags &= ~ACPI_FADT_APIC_CLUSTER;
	fadt->flags |= ACPI_FADT_APIC_PHYSICAL;
}

static unsigned long grantley_write_acpi_tables(const struct device *dev,
						unsigned long current,
						struct acpi_rsdp *rsdp)
{
	if (!(pci_s_read_config32(PCI_DEV(0, 5, 0), IIO_VTBAR) & IIO_VTBAR_EN))
		return current;

	acpi_dmar_t *const dmar = (acpi_dmar_t *)current;
	printk(BIOS_DEBUG, "ACPI:    * DMAR\n");
	acpi_create_dmar(dmar, DMAR_INTR_REMAP, grantley_fill_dmar);
	current += dmar->header.length;
	current = acpi_align_current(current);
	acpi_add_table(rsdp, dmar);

	return current;
}

static const char *grantley_domain_acpi_name(const struct device *dev)
{
	if (dev->path.type == DEVICE_PATH_DOMAIN)
		return "PCI0";

	return NULL;
}

struct device_operations grantley_pci_domain_ops = {
	.read_resources   = grantley_domain_read_resources,
	.set_resources    = pci_domain_set_resources,
	.scan_bus         = grantley_domain_scan_bus,
	.acpi_name        = grantley_domain_acpi_name,
	.acpi_fill_ssdt   = pci_domain_fill_ssdt,
	.write_acpi_tables = grantley_write_acpi_tables,
	.ops_pci          = &pci_dev_ops_pci,
};

static void iio_ioapic_read_resources(struct device *dev)
{
	pci_dev_read_resources(dev);

	/* The uncore initializer may leave the IOAPIC window disabled. */
	if (!iio_ioapic_addr())
		pci_write_config16(dev, IIO_IOAPIC_ABAR, IIO_IOAPIC_ABAR_STOCK);

	mmio_range(dev, IIO_IOAPIC_ABAR, iio_ioapic_addr(), 0x100);
}

/* Both IOAPICs reset to ID 0; give this one a unique ID for the MADT and DMAR. */
static void iio_ioapic_set_id(uintptr_t ioapic_base)
{
	u32 reg;

	write32p(ioapic_base + IOAPIC_INDEX, IOAPIC_ID_REG);
	reg = read32p(ioapic_base + IOAPIC_DATA);
	write32p(ioapic_base + IOAPIC_DATA, (reg & ~IOAPIC_ID_MASK) | IIO_IOAPIC_ID << 24);

	if (get_ioapic_id(ioapic_base) != IIO_IOAPIC_ID)
		printk(BIOS_ERR, "IIO: IOAPIC ID %u did not stick, read %u\n",
		       IIO_IOAPIC_ID, get_ioapic_id(ioapic_base));
}

static void iio_ioapic_init(struct device *dev)
{
	const uintptr_t ioapic_base = iio_ioapic_addr();
	struct device_path path = { .type = DEVICE_PATH_IOAPIC };
	struct bus *bus;
	u32 mmio_top;

	pci_dev_init(dev);

	if (!ioapic_base)
		return;

	/*
	 * The low MMIO window of the IIO ends below the APIC range, so nothing
	 * decodes the address this IOAPIC sits at and every access reads back as
	 * all ones. Stock firmware lifts the top past the APIC range; do the
	 * same by programming TOMMIOL_OB[31:20]. Bits 19:0 are not writable.
	 */
	mmio_top = pci_s_read_config32(PCI_DEV(0, 5, 0), IIO_MMIO_TOP);
	pci_s_write_config32(PCI_DEV(0, 5, 0), IIO_MMIO_TOP, IIO_MMIO_TOP_APIC);
	printk(BIOS_DEBUG, "IIO: low MMIO top %08x -> %08x\n", mmio_top,
	       pci_s_read_config32(PCI_DEV(0, 5, 0), IIO_MMIO_TOP));

	iio_ioapic_set_id(ioapic_base);

	bus = alloc_bus(dev);
	assert(bus);

	/*
	 * Leave the redirection table alone: the IIO configures this IOAPIC
	 * itself, and writing the table here stops the whole window from
	 * decoding, so the clearing performed by ioapic_setup() fails while
	 * the ID register still reads back. Program only the ID above and
	 * announce the IOAPIC, so that it ends up in the MADT and DMAR scope.
	 */
	path.ioapic.ioapic_id = get_ioapic_id(ioapic_base);
	path.ioapic.addr = ioapic_base;
	path.ioapic.gsi_base = IIO_IOAPIC_GSI_BASE;
	assert(alloc_dev(bus, &path));
}

static struct device_operations iio_ioapic_ops = {
	.read_resources   = iio_ioapic_read_resources,
	.set_resources    = pci_dev_set_resources,
	.enable_resources = pci_dev_enable_resources,
	.init             = iio_ioapic_init,
	.ops_pci          = &pci_dev_ops_pci,
};

static const unsigned short iio_ioapic_ids[] = {
	0x2f2c, /* Haswell-EP */
	0x6f2c, /* Broadwell-EP */
	0
};

static const struct pci_driver iio_ioapic_driver __pci_driver = {
	.ops     = &iio_ioapic_ops,
	.vendor  = PCI_VID_INTEL,
	.devices = iio_ioapic_ids,
};

#define DMI_LNKCAP	0x9c
#define DMI_LNKCON	0x1b0
#define DMI_LNKSTS	0x1b2
#define DMI_LNKCON2	0x1c0

static void dmi_print_link_status(int loglevel)
{
	const u16 lnksts = pci_s_read_config16(PCI_DEV(0, 0, 0), DMI_LNKSTS);
	printk(loglevel, "DMI: Running at Gen%u x%u\n", lnksts & 0xf, lnksts >> 4 & 0x1f);
}

static void dmi_retrain(void)
{
	const pci_devfn_t dmi = PCI_DEV(0, 0, 0);

	pci_s_write_config16(dmi, DMI_LNKCON,
			     pci_s_read_config16(dmi, DMI_LNKCON) | PCI_EXP_LNKCTL_RL);
	if (!wait_ms(100, !(pci_s_read_config16(dmi, DMI_LNKSTS) & PCI_EXP_LNKSTA_LT)))
		printk(BIOS_ERR, "DMI: Link training timed out\n");
}

/* The uncore initializer leaves DMI at Gen1; train it to Gen2 as on Haswell. */
static void grantley_dmi_gen2(void)
{
	const pci_devfn_t dmi = PCI_DEV(0, 0, 0);

	pch_dmi_setup_physical_layer();
	if ((RCBA8(DLCTL2) & 0xf) != 2)
		return;

	/* Maximum Link Speed is write-once */
	pci_s_write_config32(dmi, DMI_LNKCAP,
			     (pci_s_read_config32(dmi, DMI_LNKCAP) & ~PCI_EXP_LNKCAP_MLS) | 2);
	pci_s_write_config16(dmi, DMI_LNKCON2,
			     (pci_s_read_config16(dmi, DMI_LNKCON2) & ~0xf) | 2);

	printk(BIOS_DEBUG, "Retraining DMI at Gen2 speeds...\n");
	dmi_print_link_status(BIOS_DEBUG);

	dmi_retrain();
	dmi_print_link_status(BIOS_DEBUG);

	/* Retrain link again for DMI Gen2 speeds */
	dmi_retrain();
	dmi_print_link_status(BIOS_INFO);
}

static void grantley_init(void *chip_info)
{
	const struct northbridge_intel_grantley_config *cfg = chip_info;

	grantley_dmi_gen2();

	/* The uncore initializer leaves VT-d off; IIO late init would enable it. */
	if (cfg->vtd_support)
		pci_s_write_config32(PCI_DEV(0, 5, 0), IIO_VTBAR,
				     GRANTLEY_VTD_BASE | IIO_VTBAR_EN);
}

struct chip_operations northbridge_intel_grantley_ops = {
	.name = "Intel Haswell-EP / Broadwell-EP Northbridge",
	.init = grantley_init,
};

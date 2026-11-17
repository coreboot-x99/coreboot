/* SPDX-License-Identifier: GPL-2.0-or-later */

#include <console/console.h>
#include <device/pci_def.h>
#include <device/pci_ops.h>
#include <southbridge/intel/common/lpc_def.h>
#include <southbridge/intel/lynxpoint/hsio/hsio.h>
#include <southbridge/intel/lynxpoint/pch.h>
#include <types.h>

static inline bool is_9_series_pch_h(void)
{
	const uint16_t devid = pci_read_config16(PCH_LPC_DEV, PCI_DEVICE_ID);
	return (devid & 0xfff0) == 0x8cc0;
}

static void early_sata_init(const uint8_t pch_revision)
{
	const bool is_mobile = get_pch_platform_type() != PCH_TYPE_DESKTOP;

	const uint8_t lane_owner = pci_read_config8(PCH_PCIE_DEV(0), 0x410);
	printk(BIOS_DEBUG, "HSIO lane owner: 0x%02x\n", lane_owner);

	/* BWG Step 2 */
	pci_update_config32(PCH_SATA_DEV, SATA_SCLKG, ~0x1ff, 0x183);

	/* BWG Step 3: Set OOB Retry Mode */
	pci_or_config16(PCH_SATA_DEV, SATA_PCS, 1 << 15);

	/* BWG Step 4: Program the SATA mPHY tables */
	if (pch_is_lp()) {
		if (pch_revision >= LPT_LP_STEP_B0 && pch_revision <= LPT_LP_STEP_B2) {
			program_hsio_sata_lpt_lp_bx(is_mobile);
		} else {
			printk(BIOS_ERR, "Unsupported PCH-LP stepping 0x%02x\n", pch_revision);
		}
	} else {
		/** FIXME: Figure out HSIO settings for 9 series PCH-H **/
		if (pch_revision >= LPT_H_STEP_C0 || is_9_series_pch_h()) {
			program_hsio_sata_lpt_h_cx(is_mobile);
		} else {
			printk(BIOS_ERR, "Unsupported PCH-H stepping 0x%02x\n", pch_revision);
		}
	}

	/** FIXME: Program SATA RxEq tables **/

	/* BWG Step 5 */
	/** FIXME: Only for desktop and mobile (skip this on workstation and server) **/
	pci_or_config32(PCH_SATA_DEV, 0x98, BIT(22));

	/* BWG Step 6 */
	pci_or_config32(PCH_SATA_DEV, 0x98, BIT(19));

	/* BWG Step 7 */
	pci_update_config32(PCH_SATA_DEV, 0x98, ~(0x3f << 7), 0x04 << 7);

	/* BWG Step 8 */
	pci_or_config32(PCH_SATA_DEV, 0x98, BIT(20));

	/* BWG Step 9 */
	pci_update_config32(PCH_SATA_DEV, 0x98, ~(3 << 5), 1 << 5);

	/* BWG Step 10 */
	pci_or_config32(PCH_SATA_DEV, 0x98, BIT(18));

	/* Enable SATA ports */
	uint8_t sata_pcs = 0;
	if (CONFIG(INTEL_LYNXPOINT_LP)) {
		for (uint8_t i = 0; i < 4; i++) {
			if ((lane_owner & BIT(7 - i)) == 0) {
				sata_pcs |= BIT(i);
			}
		}
	} else {
		sata_pcs |= 0x0f;
		for (uint8_t i = 4; i < 6; i++) {
			if ((lane_owner & BIT(i)) == 0) {
				sata_pcs |= BIT(i);
			}
		}
	}
	printk(BIOS_DEBUG, "SATA port enables: 0x%02x\n", sata_pcs);
	pci_or_config8(PCH_SATA_DEV, SATA_PCS, sata_pcs);
}

void early_pch_init_native(bool s3resume)
{
	const uint8_t pch_revision = pci_read_config8(PCH_LPC_DEV, PCI_REVISION_ID);

	RCBA16(DISPBDF) = 0x0010;
	RCBA32_OR(FD2, PCH_ENABLE_DBDF);

	/** FIXME: Check GEN_PMCON_3 and handle RTC failure? **/

	RCBA32(PRSTS) = BIT(4);

	early_sata_init(pch_revision);

	pci_or_config8(PCH_LPC_DEV, 0xa6, 1 << 1);
	pci_and_config8(PCH_LPC_DEV, BIOS_CNTL, ~(BIOS_CNTL_SMM_BWP | BIOS_CNTL_BLE));

	/** TODO: Send GET HSIO VER and update ChipsetInit table? Is it needed? **/

	/** FIXME: GbE handling? **/

	pci_update_config32(PCH_LPC_DEV, 0xac, ~(1 << 20), 0);

	for (uint8_t i = 0; i < 8; i++)
		pci_update_config32(PCH_PCIE_DEV(i), 0x338, ~(1 << 26), 0);

	pci_update_config8(PCH_PCIE_DEV(0), 0xf4, ~(3 << 5), 1 << 7);

	pci_update_config8(PCH_EHCI1_DEV, 0x88, ~(1 << 2), 0);
	if (!pch_is_lp())
		pci_update_config8(PCH_EHCI2_DEV, 0x88, ~(1 << 2), 0);

	/** FIXME: Disable SATA2 device? **/

	if (pch_is_lp()) {
		if (pch_revision >= LPT_LP_STEP_B0 && pch_revision <= LPT_LP_STEP_B2) {
			program_hsio_xhci_lpt_lp_bx();
			program_hsio_igbe_lpt_lp_bx();
		} else {
			printk(BIOS_ERR, "Unsupported PCH-LP stepping 0x%02x\n", pch_revision);
		}
	} else {
		/** FIXME: Figure out HSIO settings for 9 series PCH-H **/
		if (pch_revision >= LPT_H_STEP_C0 || is_9_series_pch_h()) {
			program_hsio_xhci_lpt_h_cx();
			program_hsio_igbe_lpt_h_cx();
		} else {
			printk(BIOS_ERR, "Unsupported PCH-H stepping 0x%02x\n", pch_revision);
		}
	}

	early_thermal_init();
	early_usb_init();
}

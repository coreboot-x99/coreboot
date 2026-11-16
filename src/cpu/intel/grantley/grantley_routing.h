/* SPDX-License-Identifier: GPL-2.0-only */

#ifndef CPU_INTEL_GRANTLEY_GRANTLEY_ROUTING_H
#define CPU_INTEL_GRANTLEY_GRANTLEY_ROUTING_H

#define HASWELL_EP_FAMILY	0x306f0
#define BROADWELL_EP_FAMILY	0x406f0
#define BROADWELL_DE_FAMILY	0x50660

/* Shared by stackless assembly and the Uncore runner. */
#define GRANTLEY_ECAM_BASE		0x80000000
#define GRANTLEY_ECAM_LIMIT		0x8c000000
/* The limit register stores bits 31:26 of the inclusive decoded end. */
#define GRANTLEY_ECAM_END		((GRANTLEY_ECAM_LIMIT | 0x3ffffffULL) + 1)

#define MSR_XAPIC_BASE			0x1b
#define LOCAL_APIC_BASE			0xfee00000

#define VCODE_API_DELAY_COUNT		0x1000

/* Cold four-socket map: socket zero occupies buses 0x00 through 0x3f. */
#define GRANTLEY_COLD_UNCORE_BUS		0x3f
#define GRANTLEY_COLD_CPUBUSNO			0x80003f00
#define GRANTLEY_COLD_IIO_BUS_RULE		0x13f00
#define GRANTLEY_COLD_MMCFG_TARGET_LIST		0x006d2240

/* Warm single-socket map: all 256 buses route to socket zero. */
#define GRANTLEY_WARM_UNCORE_BUS		0xff
#define GRANTLEY_WARM_CPUBUSNO			0x8000ff00
#define GRANTLEY_WARM_IIO_BUS_RULE		0x1ff00
#define GRANTLEY_WARM_MMCFG_TARGET_LIST		0

#define GRANTLEY_CF8(bus, dev, fn, reg) \
	((1 << 31) | ((bus) << 16) | ((dev) << 11) | ((fn) << 8) | (reg))

#define CSR_LEGACY_MMIO_RULE3_CBO			GRANTLEY_CF8(1, 15, 5, 0x58)
#define CSR_LEGACY_MMIO_RULE7_CBO			GRANTLEY_CF8(1, 15, 5, 0x78)
#define CSR_LEGACY_IO_TARGET_LIST_CBO			GRANTLEY_CF8(1, 15, 5, 0xe0)
#define CSR_LEGACY_MMIO_TARGET_LIST_CBO			GRANTLEY_CF8(1, 15, 5, 0xe8)
#define CSR_LEGACY_MMIO_TARGET_LIST_HI_CBO		GRANTLEY_CF8(1, 15, 5, 0xf8)
#define CSR_LEGACY_MMCFG_RULE_CBO			GRANTLEY_CF8(1, 15, 5, 0xc0)
#define CSR_LEGACY_MMCFG_RULE_HI_CBO			GRANTLEY_CF8(1, 15, 5, 0xc4)
#define CSR_LEGACY_MMCFG_TARGET_LIST_CBO		GRANTLEY_CF8(1, 15, 5, 0xe4)
#define CSR_LEGACY_SAD_TARGET_CBO			GRANTLEY_CF8(1, 15, 5, 0xf0)
#define CSR_LEGACY_SAD_CONTROL_CBO			GRANTLEY_CF8(1, 15, 5, 0xf4)
#define CSR_LEGACY_LOCKCONTROL				GRANTLEY_CF8(1, 16, 5, 0x50)
#define CSR_LEGACY_CPUBUSNO_UBOX			GRANTLEY_CF8(1, 16, 7, 0xd0)
#define CSR_LEGACY_MMCFG_RULE_UBOX			GRANTLEY_CF8(1, 16, 7, 0xdc)

/* Sticky Scratchpad Registers */
#define CSR_LEGACY_BIOS_SCRATCHPAD(n)			GRANTLEY_CF8(1, 16, 7, 0x40 + (n) * 4)
#define SSR_LEGACY_BUS_CONFIG_INFO_CSR			CSR_LEGACY_BIOS_SCRATCHPAD(3)
#define SSR_LEGACY_BOOT_CONFIG_INFO_CSR			CSR_LEGACY_BIOS_SCRATCHPAD(7)

/* Non-sticky Scratchpad Registers */
#define CSR_LEGACY_BIOS_NONSTICKY_SCRATCHPAD(n)		GRANTLEY_CF8(1, 16, 7, 0x60 + (n) * 4)
#define SR_LEGACY_BOOT_HALTED_CSR			CSR_LEGACY_BIOS_NONSTICKY_SCRATCHPAD(0)
#define SR_LEGACY_BOOT_GO_CSR				CSR_LEGACY_BIOS_NONSTICKY_SCRATCHPAD(1)
#define SR_LEGACY_PBSP_CHECKIN_CSR			CSR_LEGACY_BIOS_NONSTICKY_SCRATCHPAD(2)

/*
 * Defines for MMCONF accesses
 * NOTE: Bus parameter is unused because it needs to be set correctly at runtime
 */
#define GRANTLEY_MMCFG(bus, dev, fn, reg) \
	(((0) << 20) | ((dev) << 15) | ((fn) << 12) | (reg))

#define CSR_EMULATION_FLAG_UBOX				GRANTLEY_MMCFG(1, 16, 5, 0xb0)

#define CSR_BIOS_NONSTICKY_SCRATCHPAD(n)		GRANTLEY_MMCFG(1, 16, 7, 0x60 + (n) * 4)

#define CSR_CAPID4_HSX_BDX				GRANTLEY_MMCFG(1, 30, 3, 0x94)

#define CSR_VCU_MAILBOX_INTERFACE			GRANTLEY_MMCFG(1, 31, 0, 0x40)
#define CSR_VCU_MAILBOX_DATA				GRANTLEY_MMCFG(1, 31, 0, 0x44)

#define GRANTLEY_UBOX	0x87000	/* 16.7 */
#define GRANTLEY_PCU	0xf2000	/* 30.2 */
#define GRANTLEY_VCU	0xf8000	/* 31.0 */
#define GRANTLEY_CFG_NODE 0x85040	/* 16.5.0x40 */

#ifndef __ASSEMBLER__
#include <stdint.h>

#if ENV_TEST
uint8_t grantley_uncore_bus(void);
#else
#include <cpu/x86/msr.h>

static inline uint8_t grantley_uncore_bus(void)
{
	const msr_t buses = rdmsr(0x300);

	return buses.lo & (1U << 31) ? (buses.lo >> 8) & 0xff : 0;
}
#endif
#endif

#endif /* CPU_INTEL_GRANTLEY_GRANTLEY_ROUTING_H */

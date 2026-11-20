/* SPDX-License-Identifier: GPL-2.0-only */

/* Stackless BSP startup; MM0/MM1/MM2 retain BIST and the entry timestamp. */
#ifndef GRANTLEY_STARTUP_MACROS
#define GRANTLEY_STARTUP_MACROS

#include <cpu/intel/grantley/grantley_routing.h>

/* Legacy access: selector immediate, data EAX; EBX/ECX/ESI/EDI unchanged. */
.macro startup_pci_read selector, bus=$0
	movl	$\selector, %eax
	orl	\bus, %eax
	movw	$0xcf8, %dx
	outl	%eax, %dx
	movw	$0xcfc, %dx
	inl	%dx, %eax
.endm

/* Verify all writable bits requested by mask, with expected data in EBX. */
.macro startup_pci_write selector, mask=0xffffffff, bus=$0
	movl	$\selector, %eax
	orl	\bus, %eax
	movw	$0xcf8, %dx
	outl	%eax, %dx
	movl	%ebx, %eax
	movw	$0xcfc, %dx
	outl	%eax, %dx
	inl	%dx, %eax
	xorl	%ebx, %eax
	testl	$\mask, %eax
	jnz	.Lstartup_failure
.endm

.macro startup_uncore_base
	movl	$0x300, %ecx
	rdmsr
	andl	$0xff00, %eax
	shll	$12, %eax
	orl	$GRANTLEY_ECAM_BASE, %eax
	movl	%eax, %ebp
.endm

.macro startup_mmio_verify address, expected, mask
	movl	\address, %eax
	andl	$\mask, %eax
	cmpl	$\expected, %eax
	jne	.Lstartup_failure
.endm

.macro early_uncore_init
	/* Enable FXSAVE and unmasked SIMD exception support (CR4.OSFXSR |
	 * OSXMMEXCPT).  The stock SEC enables these before any IO or C code;
	 * the Uncore PEIM uses XMM registers and would fault without them. */
	movl	%cr4, %eax
	orl	$((1 << 9) | (1 << 10)), %eax
	movl	%eax, %cr4

	/* Program the early SAD I/O and MMIO target lists via CF8/CFC. */
	/* Set LOCKCONTROL.LockDisable (1:16.5:0x50 bit 0). */
	movl	$CSR_LEGACY_LOCKCONTROL, %eax
	movw	$0xcf8, %dx
	outl	%eax, %dx
	movw	$0xcfc, %dx
	inl	%dx, %eax
	orl	$1, %eax
	outl	%eax, %dx

	/* 2. Route IO Target List to Legacy Node (1:15.5:0xe0 = IOPORT_TARGET_LIST).
	 * Only offsets 0x00-0xff are reachable through CF8/CFC; the 0x7df0/0x7de0/
	 * 0x7de8 values below are devfn 0x7d (dev 15/fn 5) plus the register
	 * offset in the low byte, matching the CSR_LEGACY_*_CBO register naming. */
	movl	$CSR_LEGACY_SAD_TARGET_CBO, %eax	/* 1:15.5:0xf0 SAD_TARGET_CBO */
	movw	$0xcf8, %dx
	outl	%eax, %dx
	movw	$0xcfc, %dx
	inl	%dx, %eax
	andl	$0x38, %eax		/* keep bits 5:3 */
	shrl	$3, %eax		/* node id */
	movd	%eax, %xmm1
	movl	%eax, %ebx
	movb	$3, %cl
	movb	$7, %ch
1:
	shll	%cl, %eax
	orl	%eax, %ebx
	decb	%ch
	jnz	1b

	movl	$CSR_LEGACY_IO_TARGET_LIST_CBO, %eax	/* 1:15.5:0xe0 IOPORT_TARGET_LIST_CBO */
	movw	$0xcf8, %dx
	outl	%eax, %dx
	movw	$0xcfc, %dx
	movl	%ebx, %eax
	outl	%eax, %dx

	movl	$CSR_LEGACY_MMIO_TARGET_LIST_CBO, %eax	/* 1:15.5:0xe8 MMIO_TARGET_LIST_CBO */
	movw	$0xcf8, %dx
	outl	%eax, %dx
	movw	$0xcfc, %dx
	movl	%ebx, %eax
	outl	%eax, %dx

	/* Store CPU family/stepping in xmm1[27:8] */
	movl	$1, %eax
	cpuid
	andl	$0xfffff, %eax
	shll	$8, %eax
	movd	%xmm1, %ebx
	orl	%eax, %ebx
	movd	%ebx, %xmm1

	/* Decode 0xfc000000..0xffffffff, including CAR, as MMIO
	 * (1:15.5:0x58/0x5c = MMIO_RULE_3 base/limit). */
	movl	$(CSR_LEGACY_MMIO_RULE3_CBO + 4), %eax
	movw	$0xcf8, %dx
	outl	%eax, %dx
	movw	$0xcfc, %dx
	xorl	%eax, %eax
	outl	%eax, %dx

	movl	$CSR_LEGACY_MMIO_RULE3_CBO, %eax
	movw	$0xcf8, %dx
	outl	%eax, %dx
	movw	$0xcfc, %dx
	movl	$0xff00007f, %eax
	outl	%eax, %dx
.endm
#endif
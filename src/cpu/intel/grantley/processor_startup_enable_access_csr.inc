/* SPDX-License-Identifier: GPL-2.0-only */

.macro enable_access_csr
	/* Reject a different CPU family or node layout before using its CSRs. */
	movl	$1, %eax
	cpuid
	movl	%eax, %esi
	andl	$0xffff0, %eax
	cmpl	$HASWELL_EP_FAMILY, %eax
	je	.Lstartup_family_ok
	cmpl	$BROADWELL_EP_FAMILY, %eax
	jne	.Lstartup_failure
.Lstartup_family_ok:
	startup_pci_read CSR_LEGACY_SAD_CONTROL_CBO
	testl	$0xb, %eax
	jnz	.Lstartup_failure
	startup_pci_read CSR_LEGACY_SAD_TARGET_CBO
	testl	$0x38, %eax
	jnz	.Lstartup_failure

	/* CPUBUSNO changes the bus used by subsequent CF8 transactions.
	 * Its readback must select the NEW bus, not the obsolete reset alias.
	 */
	startup_pci_read GRANTLEY_CF8(1, 16, 7, 0x00)
	cmpw	$0x8086, %ax
	jne	.Lstartup_failure
	/* Sticky state selects only the supported cold or single-socket warm map. */
	startup_pci_read SSR_LEGACY_BOOT_CONFIG_INFO_CSR
	/* Hotplug reset flow entered? */
	testl	$0x40, %eax
	jnz	.Lstartup_failure
	/* Cold reset flow complete? */
	testl	$0x20, %eax
	jnz	.Lstartup_warm
	/* Cold reset flow started? */
	testl	$0x10, %eax
	jnz	.Lstartup_failure
	movl	$GRANTLEY_COLD_CPUBUSNO, %ebx
	jmp	.Lstartup_scheme_ready
.Lstartup_warm:
	startup_pci_read CSR_LEGACY_BIOS_SCRATCHPAD(2)
	cmpb	$1, %al
	jne	.Lstartup_failure
	startup_pci_read SSR_LEGACY_BUS_CONFIG_INFO_CSR
	movl	%eax, %ecx
	andl	$7, %eax
	cmpl	$7, %eax
	jne	.Lstartup_failure
	shll	$2, %ecx
	andl	$0xfc000000, %ecx
	cmpl	$GRANTLEY_ECAM_BASE, %ecx
	jne	.Lstartup_failure
	movl	$GRANTLEY_WARM_CPUBUSNO, %ebx
.Lstartup_scheme_ready:
	movl	%ebx, %edi
	andl	$0xff00, %edi
	shll	$8, %edi
	movl	%edi, %ebp
	shll	$4, %ebp
	orl	$GRANTLEY_ECAM_BASE, %ebp
	movl	$CSR_LEGACY_CPUBUSNO_UBOX, %eax
	movw	$0xcf8, %dx
	outl	%eax, %dx
	movl	%ebx, %eax
	movw	$0xcfc, %dx
	outl	%eax, %dx
	startup_pci_read GRANTLEY_CF8(0, 16, 7, 0xd0), %edi
	cmpl	%ebx, %eax
	jne	.Lstartup_failure
	startup_pci_read GRANTLEY_CF8(0, 16, 7, 0x00), %edi
	cmpw	$0x8086, %ax
	jne	.Lstartup_failure
	startup_pci_read GRANTLEY_CF8(0, 15, 5, 0x00), %edi
	cmpw	$0x8086, %ax
	jne	.Lstartup_failure
	startup_pci_read GRANTLEY_CF8(0, 5, 0, 0x00)
	cmpw	$0x8086, %ax
	jne	.Lstartup_failure

	/* Rule length 00 encodes 256 buses in both schemes. */
	xorl	%ebx, %ebx
	cmpl	$(GRANTLEY_COLD_UNCORE_BUS << 16), %edi
	jne	.Lstartup_targets_ready
	movl	$GRANTLEY_COLD_MMCFG_TARGET_LIST, %ebx
.Lstartup_targets_ready:
	startup_pci_write GRANTLEY_CF8(0, 15, 5, 0xe4), 0xffffff, %edi
	movl	$(GRANTLEY_ECAM_BASE | 1), %ebx
	startup_pci_write GRANTLEY_CF8(0, 15, 5, 0xc0), 0xfc000007, %edi
	startup_pci_read GRANTLEY_CF8(0, 16, 7, 0xdc), %edi
	andl	$~7, %eax
	orl	$1, %eax
	movl	%eax, %ebx
	startup_pci_write GRANTLEY_CF8(0, 16, 7, 0xdc), 7, %edi

	/* A0 has 32-bit base/limit at 84/88. Later silicon has 64-bit
	 * fields at 90/98: clear BOTH high words before enabling CSR use.
	 * Limit stores address bits 31:26, so 8c covers through 8fffffff.
	 */
	cmpl	$HASWELL_EP_FAMILY, %esi
	je	.Lstartup_iio_a0
	xorl	%ebx, %ebx
	startup_pci_write GRANTLEY_CF8(0, 5, 0, 0x94)
	startup_pci_write GRANTLEY_CF8(0, 5, 0, 0x9c)
	movl	$GRANTLEY_ECAM_BASE, %ebx
	startup_pci_write GRANTLEY_CF8(0, 5, 0, 0x90), 0xfc000000
	movl	$GRANTLEY_ECAM_LIMIT, %ebx
	startup_pci_write GRANTLEY_CF8(0, 5, 0, 0x98), 0xfc000000
	jmp	.Lstartup_iio_ready
.Lstartup_iio_a0:
	movl	$GRANTLEY_ECAM_BASE, %ebx
	startup_pci_write GRANTLEY_CF8(0, 5, 0, 0x84), 0xfc000000
	movl	$GRANTLEY_ECAM_LIMIT, %ebx
	startup_pci_write GRANTLEY_CF8(0, 5, 0, 0x88), 0xfc000000
.Lstartup_iio_ready:
	movl	GRANTLEY_CFG_NODE(%ebp), %eax
	andl	$~7, %eax
	movl	%eax, GRANTLEY_CFG_NODE(%ebp)
	startup_mmio_verify GRANTLEY_CFG_NODE(%ebp), 0, 7

	/* MY_NID=0 and AM_I_MASTER=1. MASTER_NID retains its cold-reset
	 * node-zero value; do not alter unrelated package C-state controls.
	 */
	movl	(GRANTLEY_PCU + 0x6c)(%ebp), %eax
	andl	$~0x700, %eax
	orl	$4, %eax
	movl	%eax, (GRANTLEY_PCU + 0x6c)(%ebp)
	startup_mmio_verify (GRANTLEY_PCU + 0x6c)(%ebp), 4, 0x704

	startup_mmio_verify GRANTLEY_PCU(%ebp), 0x8086, 0xffff
	movl	(GRANTLEY_PCU + 0x40)(%ebp), %eax
	andl	$0xffff0000, %eax
	movl	%edi, %ebx
	shrl	$8, %ebx
	orl	$0x80000000, %ebx
	orl	%ebx, %eax
	movl	%eax, (GRANTLEY_PCU + 0x40)(%ebp)
	movl	(GRANTLEY_PCU + 0x40)(%ebp), %eax
	xorl	%ebx, %eax
	testl	$0x8000ffff, %eax
	jnz	.Lstartup_failure
	andl	$0xff00, %ebx
	orl	$0x10000, %ebx
	movl	0x80028108, %eax
	andl	$0xfffe0000, %eax
	orl	%ebx, %eax
	movl	%eax, 0x80028108
	movl	0x80028108, %eax
	xorl	%ebx, %eax
	testl	$0x1ffff, %eax
	jnz	.Lstartup_failure

	jmp	.Lstartup_route_ready
.Lstartup_failure:
	/* A routing prerequisite failed: never attempt cache/Uncore access. */
	post_code(0xcc)
	cli
.Lstartup_failed_halt:
	hlt
	jmp	.Lstartup_failed_halt
.Lstartup_route_ready:
.endm
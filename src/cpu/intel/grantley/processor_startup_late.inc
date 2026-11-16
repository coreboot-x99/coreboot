/* SPDX-License-Identifier: GPL-2.0-only */

.macro processor_workaround
	/* The PROCHOT workaround is specific to Haswell-EP A0. Never infer
	 * engineering-sample register compatibility from family ordering.
	 */
	movl	$1, %eax
	cpuid
	cmpl	$HASWELL_EP_FAMILY, %eax
	jne	.Ldone_hsx_wa
	movl	$0x1fc, %ecx
	rdmsr
	orl	$((1 << 21) | 1), %eax
	wrmsr
.Ldone_hsx_wa:
.endm

#define TXT_PUBLIC_BASE		0xfed30000
#define TPM_STATUS_REG_ADDR	0xfed40000
#define MSR_LT_UNLOCK_MEMORY	0x2e6

.macro txt_init
	/* TXT memory unlock is permitted only with SMX and this package's
	 * TXT status bit asserted. An absent MMIO register is not permission.
	 * The TPM status read must precede the memory-unlock MSR write.
	 */
	/* Check LTSX support */
	movl	$1, %eax
	cpuid
	testl	$(1 << 6), %ecx
	jz	.Ltxt_init_exit
	movl	(TXT_PUBLIC_BASE + 0x10), %eax
	cmpl	$0xffffffff, %eax
	je	.Ltxt_init_exit
	testl	$1, %eax
	jz	.Ltxt_init_exit
	movl	TPM_STATUS_REG_ADDR, %eax
	movl	$MSR_LT_UNLOCK_MEMORY, %ecx
	xorl	%eax, %eax
	xorl	%edx, %edx
	wrmsr
.Ltxt_init_exit:
.endm

#define MSR_OVER_CLOCK_PLL_RATIO 0x61e

.macro setup_processor_checkin_data
	/* Publish the package BSP ABI consumed by Uncore: check-in bit 0,
	 * OC PLL ratio [3:2], initial APIC ID [23:8], stepping [30:27].
	 * All other scratch fields survive. No OC-capability means MSR 61e
	 * is not readable, not merely that its result should be ignored.
	 */
	movl	$1, %eax
	cpuid
	andl	$15, %eax
	shll	$27, %eax
	movl	%eax, %esi
	shrl	$24, %ebx
	shll	$8, %ebx
	movl	%ebx, %edi
	xorl	%eax, %eax
	cpuid
	cmpl	$0xb, %eax
	jb	.Lstartup_apic_id_ready
	movl	$0xb, %eax
	xorl	%ecx, %ecx
	cpuid
	testl	%ebx, %ebx
	jz	.Lstartup_apic_id_ready
	movzwl	%dx, %edi
	shll	$8, %edi
.Lstartup_apic_id_ready:
	orl	%edi, %esi
	/* Get QPI PLL ratio */
	xorl	%edi, %edi
	movl	CSR_CAPID4_HSX_BDX(%ebp), %eax
	cmpl	$0xffffffff, %eax
	je	.Lstartup_post_failure
	testl	$(1 << 28), %eax
	jz	.Lqpi_pll_oc_disabled
	movl	$0x61e, %ecx
	rdmsr
	andl	$3, %eax
	shll	$2, %eax
	movl	%eax, %edi
.Lqpi_pll_oc_disabled:
	movl	CSR_BIOS_NONSTICKY_SCRATCHPAD(2)(%ebp), %eax
	andl	$0x870000f3, %eax
	orl	%esi, %eax
	orl	%edi, %eax
	orl	$1, %eax
	movl	%eax, CSR_BIOS_NONSTICKY_SCRATCHPAD(2)(%ebp)
	cmpl	CSR_BIOS_NONSTICKY_SCRATCHPAD(2)(%ebp), %eax
	jne	.Lstartup_post_failure
	jmp	.Lstartup_post_ready
.Lstartup_post_failure:
	post_code(0xcc)
	cli
.Lstartup_post_halt:
	hlt
	jmp	.Lstartup_post_halt
.Lstartup_post_ready:
.endm

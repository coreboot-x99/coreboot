/* SPDX-License-Identifier: GPL-2.0-only */

.macro mid_uncore_init
	movl	$0x1a0, %ecx
	rdmsr
	/* If limit CPU ID is enabled (because of soft reset), disable it */
	btrl	$22, %eax
	jnc	.Lstartup_cpuid_unlimited
	wrmsr
.Lstartup_cpuid_unlimited:
	/* Set local APIC base address */
	movl	$MSR_XAPIC_BASE, %ecx
	rdmsr
	andl	$0x00000fff, %eax
	orl	$LOCAL_APIC_BASE, %eax
	andl	$~0xf, %edx
	wrmsr
.endm

.macro startup_vcu_wait
	movl	$VCODE_API_DELAY_COUNT, %ecx
.Lvcu_poll\@:
	movl	CSR_VCU_MAILBOX_INTERFACE(%ebp), %eax
	cmpl	$0xffffffff, %eax
	je	.Lvcu_failure
	testl	$0x80000000, %eax
	jz	.Lvcu_idle\@
	pause
	decl	%ecx
	jnz	.Lvcu_poll\@
	jmp	.Lvcu_failure
.Lvcu_idle\@:
.endm

.macro pre_nem_uncore_init
	/* Cold boot has no request for 16 KiB ECC DCU mode or enabling the
	 * debug interface. Do not access MSR 31 solely because PLATFORM_INFO
	 * advertises its capability. Sticky policy replay and the PCU A4
	 * core/ring command guarded by completed cold-reset state are not replayed
	 * here; warm entry changes only the bus routing.
	 */
	/* VCU real-hardware flag is the low byte only. Simulation has no
	 * mailbox contract. Each wait, including the initial idle wait, is
	 * bounded independently. A VCU failure is diagnostic, as in the stock
	 * cold-boot path; it must not introduce a new unconditional boot stop.
	 */
	startup_uncore_base
	cmpb	$0, CSR_EMULATION_FLAG_UBOX(%ebp)
	jne	.Lstartup_ready
	startup_vcu_wait
	/* VCU API: open sequence (IOT_LLC_SETUP) */
	movl	$0x1000a, CSR_VCU_MAILBOX_DATA(%ebp)
	movl	$0x80000002, CSR_VCU_MAILBOX_INTERFACE(%ebp)
	startup_vcu_wait
	/* VCU API: set parameter (index = 0), required even if don't care */
	movl	$0, CSR_VCU_MAILBOX_DATA(%ebp)
	movl	$0x80000006, CSR_VCU_MAILBOX_INTERFACE(%ebp)
	startup_vcu_wait
	/* VCU API: IOT_LLC_SETUP (index = 1) */
	movl	$0x8001100f, CSR_VCU_MAILBOX_INTERFACE(%ebp)
	startup_vcu_wait
	/* VCU API: close sequence (IOT_LLC_SETUP) */
	movl	$0x1000a, CSR_VCU_MAILBOX_DATA(%ebp)
	movl	$0x80000003, CSR_VCU_MAILBOX_INTERFACE(%ebp)
	startup_vcu_wait
	jmp	.Lstartup_ready

.Lvcu_failure:
	post_code(0xcb)
.Lstartup_ready:
.endm

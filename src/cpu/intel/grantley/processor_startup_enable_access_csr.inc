/* SPDX-License-Identifier: GPL-2.0-only */

.macro READ_PCI_DWORD_LEGACY
	movw	$0xcf8, %dx
	outl	%eax, %dx
	xchgl	%ebx, %eax
	addw	$4, %dx
	inl	%dx, %eax
	xchgl	%ebx, %eax
.endm

.macro WRITE_PCI_DWORD_LEGACY
	movw	$0xcf8, %dx
	outl	%eax, %dx
	xchgl	%ebx, %eax
	addw	$4, %dx
	outl	%eax, %dx
	xchgl	%ebx, %eax
.endm

.macro GET_LOCAL_SOCKET_ID
	/* Read own socket ID from CSR */
	movl	$CSR_LEGACY_SAD_CONTROL_CBO, %eax
	READ_PCI_DWORD_LEGACY
	/* Node ID encoding: SASS */
	movb	%bl, %bh
	andb	$3, %bl
	andb	$8, %bh
	shrb	$1, %bh
	/* Local socket ID SSS in bl */
	orb	%bh, %bl
.endm

.macro enable_access_csr
	/* Read sticky scratch pad register for MMCFG/bus number config info */
	mov $SSR_LEGACY_BOOT_CONFIG_INFO_CSR, %eax
	READ_PCI_DWORD_LEGACY
	/* Cold reset flow complete? */
	testl	$(1 << 5), %ebx
	jz	.Lcold_reset_flow_incomplete
	/* Hotplug reset flow entered? */
	testl	$(1 << 6), %ebx
	jnz	.Luse_default_cfg_scheme
	/* Clear cold reset flow entered flag */
	andb	$(~(1 << 4)), %bl
	WRITE_PCI_DWORD_LEGACY
	jmp	.Lcalculate_cfg_scheme

.Lcold_reset_flow_incomplete:
	/* Cold reset flow entered? */
	testl	$(1 << 4), %ebx
	jz	.Luse_default_cfg_scheme

	/*
	 * Warm reset occurred before completion of MRC on the cold boot path
	 * In this case, SBSP triggers power-good reset, other socket BSPs wait
	 */

	GET_LOCAL_SOCKET_ID
	movd	%xmm1, %eax
	cmpb	%al, %bl
	jne	.Lwait_for_reset

	/* Legacy BSP comes here, triggers a power-good reset */
	movb	$0xe, %al
	movw	$0xcf9, %dx
	outb	%al, %dx

.Lwait_for_reset:
	jmp	.Lwait_for_reset

.Luse_default_cfg_scheme:
	/* Set cold reset flow entered flag */
	orl	$0x10, %ebx
	WRITE_PCI_DWORD_LEGACY

	/* TODO: 8-socket systems use different values */
	movl	$DEFAULT_COLDBOOT_MMCFG_TARGET_LIST, %eax
	movd	%eax, %xmm2
	movl	$DEFAULT_COLDBOOT_MMCFG_RULE, %eax
	movd	%eax, %xmm3
	movl	$DEFAULT_COLDBOOT_IIO_BUS_NUMS, %eax
	movd	%eax, %xmm4
	movl	$DEFAULT_COLDBOOT_UNCORE_BUS_NUMS, %eax
	movd	%eax, %xmm5

	/* Check to see if legacy socket ID is 0 */
	movd	%xmm1, %eax
	movb	%al, %cl
	movb	%cl, %ch
	cmpb	$0, %cl
	je	.Lwrite_cfg_scheme

	/* TODO: swap values for non-zero legacy socket */
	jmp	.Lenable_access_csr_fatal_error

.Lcalculate_cfg_scheme:
	movl	$CSR_LEGACY_BIOS_SCRATCHPAD(2), %eax
	READ_PCI_DWORD_LEGACY
	andl	$0x000000ff, %ebx
	movd	%ebx, %xmm3
	movl	$SSR_LEGACY_BUS_CONFIG_INFO_CSR, %eax
	READ_PCI_DWORD_LEGACY
	shll	$8, %ebx
	movd	%xmm3, %eax
	orl	%ebx, %eax
	movd	%eax, %xmm3

	xorl	%ebp, %ebp
	xorl	%esi, %esi
	xorl	%edi, %edi
	xorl	%ecx, %ecx
	xorl	%edx, %edx

.Lcheck_node_present:
	movd	%xmm3, %eax
	movw	%dx, %cx
	btw	%cx, %ax
	jnc	.Lnext_node_id

	movl	%ecx, %ebx
	shrl	$16, %ebx
	orl	%ebx, %esi

	shlw	$1, %cx
	addw	%dx, %cx
	addw	$8, %cx
	shrl	%cl, %eax
	andb	$0x7, %al
	incb	%al
	movzx	%dx, %ebx
	rorl	$16, %edx
	movw	%dx, %cx

.Lnot_yet_done_for_nid:
	shll	%cl, %ebx
	orl	%ebx, %ebp
	movw	$3, %cx
	addw	%cx, %dx

	rorl	$16, %ecx
	addw	$32, %cx
	roll	$16, %ecx

	decb	%al
	jnz	.Lnot_yet_done_for_nid
	roll	$16, %edx

	movl	%ecx, %ebx
	shrl	$16, %ebx
	decl	%ebx

	/* Some commented out code exists here */

	orl	%ebx, %edi

.Lnext_node_id:
	rorl	$8, %esi
	rorl	$8, %edi
	cmpw	$3, %dx
	jg	.Lstore_socket4_7_info
	movd	%esi, %xmm4
	movd	%edi, %xmm5

	/* If socket = 3, clear registers before using for sockets 4-7 */
	jb	.Ldata_saved
	xorl	%esi, %esi
	xorl	%edi, %edi
	jmp	.Ldata_saved

.Lstore_socket4_7_info:
	movd	%esi, %xmm6
	movd	%edi, %xmm7

.Ldata_saved:
	incw	%dx
	cmpw	$MAX_CPU_SOCKETS, %dx
	jb	.Lcheck_node_present

	movd	%ebp, %xmm2

	/*
	 * xmm2 = calculated MMCFG_TARGET_LIST
	 * xmm3 = MMCFG base [31:26]
	 * xmm4/6 = calculated IIO bus # array for socket0:3/4:7
	 * xmm6/7 = calculated Uncore bus # array for socket0:3/4:7
	 * ecx[31:16] = total bus range for all sockets (64,128,256)
	 */

	movl	$SSR_LEGACY_BUS_CONFIG_INFO_CSR, %eax
	READ_PCI_DWORD_LEGACY
	shll	$2, %ebx
	movl	%ebx, %edi
	andl	$0xfc000000, %edi

	movl	%ecx, %edx
	shrl	$16, %edx
	cmpw	$128, %dx
	jbe	.Lbus_range_ok
	movw	$256, %dx
.Lbus_range_ok:
	shrw	$7, %dx
	notb	%dl
	andb	$3, %dl
	decb	%dl
	shlb	$1, %dl
	orb	$1, %dl
	movzx	%dl, %edx
	orl	%edx, %edi

	movd	%edi, %xmm3

.Lwrite_cfg_scheme:
	/* Ensure socket ID is not out of range */
	GET_LOCAL_SOCKET_ID
	cmpb	$7, %bl
	jg	.Lenable_access_csr_fatal_error

	/* Save socket ID in xmm2[31:24] */
	movzx	%bl, %edx
	movd	%xmm2, %eax
	roll	$8, %eax
	orb	%dl, %al
	rorl	$8, %eax
	movd	%eax, %xmm2
	shll	$16, %edx

	/* Initialise UBOX CPUBUSNO register */
	movb	%bl, %cl
	cmpb	$4, %cl
	jge	.Lis_socket_4_7

	shlb	$3, %cl
	movd	%xmm4, %ebx
	shrl	%cl, %ebx
	andl	$0x000000ff, %ebx
	movd	%xmm5, %eax
	jmp	.Lset_bus_num

.Lis_socket_4_7:
	subb	$4, %cl
	shlb	$3, %cl
	movd	%xmm6, %ebx
	shrl	%cl, %ebx
	andl	$0x000000ff, %ebx
	movd	%xmm7, %eax
	jmp	.Lset_bus_num

.Lset_bus_num:
	shrl	%cl, %eax
	shll	$8, %eax
	andl	$0x0000ff00, %eax
	orl	%eax, %ebx
	movl	%ebx, %ecx
	orl	$(1 << 31), %ebx
	movl	$CSR_LEGACY_CPUBUSNO_UBOX, %eax
	WRITE_PCI_DWORD_LEGACY

	/*
	 * From this point on, valid matching bus# must be used to access all CSRs
	 */

	/* Initialise MMCFG_Target_List */
	movl	$CSR_LEGACY_MMCFG_TARGET_LIST_CBO, %eax
	andl	$0xff00ffff, %eax
	movzx	%ch, %ebx
	shll	$16, %ebx
	orl	%ebx, %eax
	movd	%xmm2, %ebx
	andl	$0x00ffffff, %ebx
	WRITE_PCI_DWORD_LEGACY

	/* Initialise and enable MMCFG_Rule registers */
	/* Cbo CSR */
	movw	$(CSR_LEGACY_MMCFG_RULE_CBO & 0xffff), %ax
	movd	%xmm3, %ebx
	orl	$1, %ebx
	WRITE_PCI_DWORD_LEGACY
	andl	$0x7, %ebx
	movl	%ebx, %edi
	/* UBOX CSR */
	movw	$(CSR_LEGACY_MMCFG_RULE_UBOX & 0xffff), %ax
	READ_PCI_DWORD_LEGACY
	andl	$(~0x7), %ebx
	orl	%edi, %ebx
	WRITE_PCI_DWORD_LEGACY

/*
 * When all socket BSPs have done this, remote CSR access among sockets
 * is enabled. Valid bus number must be used to match the bus number of
 * the target socket. Access to bus0 will be routed to legacy socket no
 * matter which socket issues the access.
 */

	movd	%xmm3, %edi
	andl	$0xfffffff0, %edi

	/*
	 * edi = MMCFG_BASE_ADDR
	 * edx [18:16] = socket ID
	 * ecx [15:8] = uncore bus, [7:0] = IIO bus
	 */
	/* Init CPUNODEID CSR */
	movl	$(CSR_CPUNODEID_UBOX & 0x000fffff), %eax
	movzx	%ch, %ebx
	shll	$20, %ebx
	orl	%eax, %ebx
	addl	%edi, %ebx
	movl	%ds:(%ebx), %eax
	andl	$(~0x7), %eax
	rorl	$16, %edx
	orb	%dl, %al
	roll	$16, %edx
	movl	%eax, %ds:(%ebx)

	/*
	 * Init CSR_GLOBAL_PKG_C_S_CONTROL_PCU CSR in PCU
	 * Note: for a system without Node Controller, the legacy socket is always
	 * treated as Master, matching the default value (0) of MASTER_NID field.
	 */
	movd	%xmm1, %eax
	movb	%al, %dh
	movl	$(CSR_GLOBAL_PKG_C_S_CONTROL_PCU & 0x000fffff), %eax
	andl	$0xfff00000, %ebx
	orl	%eax, %ebx
	movl	%ds:(%ebx), %eax
	andl	$(~0x7 << 8), %eax
	rorl	$8, %edx
	orb	%dh, %ah
	roll	$8, %edx
	cmpb	%dh, %ah
	jnz	.Lpcu_not_master
	orl	$(1 << 2), %eax
.Lpcu_not_master:
	movl	%eax, %ds:(%ebx)

	/* Init CPU_BUS_NUMBER CSR in PCU2 */
	movl	$(CSR_CPU_BUS_NUMBER_PCU2 & 0x000fffff), %eax
	andl	$0xfff00000, %ebx
	orl	%eax, %ebx
	movl	%ds:(%ebx), %eax
	movw	%cx, %ax
	movl	%eax, %ds:(%ebx)

	/* Save config address for later */
	movl	%ebx, %esi

	/* Init CPUBUSNO CSR in IIO */
	movl	$(CSR_CPUBUSNO_IIO & 0x000fffff), %eax
	movzx	%cl, %ebx
	shll	$20, %ebx
	orl	%eax, %ebx
	addl	%edi, %ebx
	movl	%ds:(%ebx), %eax
	movw	%cx, %ax
	/* Set valid bit */
	orl	$(1 << 16), %eax
	movl	%eax, %ds:(%ebx)

	/* Init MMCFG CSR in IIO */
	movd	%xmm1, %eax
	shrl	$8, %eax
	andl	$0x000fffff, %eax
	/* Check for HSX-A0 stepping */
	cmpl	$(HASWELL_EP_FAMILY | 0), %eax
	movl	$(CSR_MMCFG_IIO_A0 & 0x000fffff), %eax
	movl	$CSR_MMCFG_IIO_BASE_LIMIT_GAP_A0, %edx
	jz	.Lmmcfg_hsx_a0
	movl	$(CSR_MMCFG_IIO & 0x000fffff), %eax
	movl	$CSR_MMCFG_IIO_BASE_LIMIT_GAP, %edx
.Lmmcfg_hsx_a0:
	andl	$0xfff00000, %ebx
	orl	%eax, %ebx
	movl	%edi, %ds:(%ebx)

	/* Clear higher 32-bit of base address if HSX-B0 or later */
	cmpl	$CSR_MMCFG_IIO_BASE_LIMIT_GAP_A0, %edx
	je	.Lmmcfg_hi_not_a0
	movl	%edi, %ds:0x4(%ebx)
.Lmmcfg_hi_not_a0:

	/* Calculate MMCFG limit = base + max bus * 1 MiB */
	movd	%xmm3, %ecx
	andl	$0x6, %ecx
	shrb	$1, %cl
	movl	$(1 << 2), %eax
	shrl	%cl, %eax
	shll	$20, %eax

	addl	%edi, %eax
	subl	$1, %eax
	andl	$0xfc000000, %eax
	addl	%edx, %ebx
	movl	%eax, %ds:(%ebx)

	movl	%ebx, %edi

	/* Check CPU stepping and perform stepping-specific tasks */

	/* Set the new "Valid" bit in CPU_BUS_NUMBER register of PCU */
	movl	%ds:(%esi), %eax
	orl	$(1 << 31), %eax
	movl	%eax, %ds:(%esi)

	/*
	 * CPL3 change: send SET_CORE_RING_RATIO (0xA4) B2P (BIOS to P-code)
	 * command in cold boot path for C0 stepping onwards for HSX-EP, and
	 * A1 stepping onwards for BDX-EP. Unsupported on BDX-DE so skip it.
	 */
	movl	$1, %eax
	cpuid
	andl	$0x000fffff, %eax
	/* Note: this differs slightly from RC */
	cmpl	$HASWELL_EP_FAMILY, %eax
	je	.Lcpl3_hsx_ep
	cmpl	$BROADWELL_DE_FAMILY, %eax
	je	.Lend_cpl3
	cmpl	$BROADWELL_EP_FAMILY, %eax
	jne	.Lend_cpl3
	cmpl	$CPUID_BDX_EP_A0, %eax
	ja	.Lstart_cpl3
	/* If not BDX-EP B0 or above, check for A1 stepping */
	movl	$CSR_CAPID1_HSX_BDX, %eax
	andl	$0x000fffff, %eax
	movl	%esi, %ebx
	andl	$0xfff00000, %ebx
	orl	%ebx, %eax
	testl	$(1 << 20), %ds:(%eax)
	jz	.Lend_cpl3
	jmp	.Lstart_cpl3

.Lcpl3_hsx_ep:
	cmpl	$CPUID_HSX_EP_C0, %eax
	jb	.Lend_cpl3

.Lstart_cpl3:
	movl	$CSR_BIOS_STICKY_SCRATCHPAD(7), %eax
	andl	$0x000fffff, %eax
	movl	%esi, %ebx
	andl	$0xfff00000, %ebx
	orl	%ebx, %eax
	testl	$(1 << 5), %ds:(%eax)
	jz	.Lend_cpl3

	/* Construct SET_CORE_RING_RATIO command */
	movl	$0xce, %ecx
	rdmsr
	movl	%eax, %ebx
	andl	$0x0000ff00, %ebx
	movl	$0x620, %ecx
	rdmsr
	andl	$0x000000ff, %eax
	shll	$16, %eax
	movl	$((1 << 31) | 0xa4), %ecx
	orl	%eax, %ecx
	orl	%ebx, %ecx
	movl	$CSR_BIOS_MAILBOX_INTERFACE_PCU, %eax
	andl	$0x000fffff, %eax
	movl	%esi, %ebx
	andl	$0xfff00000, %ebx
	orl	%ebx, %eax
	/* Poll for RUN_BUSY bit to be clear */
.Lpcu_loop1:
	testl	$(1 << 31), %ds:(%eax)
	jnz	.Lpcu_loop1
	movl	%ds:(%eax), %ecx
	/* Poll for RUN_BUSY bit to be clear */
.Lpcu_loop2:
	testl	$(1 << 31), %ds:(%eax)
	jnz	.Lpcu_loop2

.Lend_cpl3:
	jmp	.Lcsr_access_enabled
.Lenable_access_csr_fatal_error:
	post_code(0xcc)
	cli
.Lcsr_access_deadloop:
	hlt
	jmp	.Lcsr_access_deadloop
.Lcsr_access_enabled:
.endm

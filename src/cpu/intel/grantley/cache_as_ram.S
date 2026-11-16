/* SPDX-License-Identifier: GPL-2.0-only */

#include <cpu/intel/post_codes.h>
#include <cpu/x86/mtrr.h>
#include <cpu/x86/cache.h>
#include <cpu/x86/post_code.h>

#define NoEvictMod_MSR 0x2e0

.section .init
.global bootblock_pre_c_entry

#include <cpu/intel/car/cache_as_ram_symbols.inc>

#include "early_startup.inc"

.code32
_cache_as_ram_setup:

bootblock_pre_c_entry:
	movl	$cache_as_ram, %esp /* return address */
	jmp	check_mtrr /* Check if CPU properly reset */

cache_as_ram:
	early_uncore_init

	/* Entry16/32 POSTs stay suppressed until legacy routing is installed.
	 * Restore assembly POSTs here, including the CAR/VCU failure paths.
	 */
#undef post_code
#if CONFIG(POST_IO)
#define post_code(value) movb $value, %al; outb %al, $CONFIG_POST_IO_PORT
#else
#define post_code(value)
#endif
	post_code(POSTCODE_BOOTBLOCK_CAR)

#include "processor_startup_enable_access_csr.inc"
	enable_access_csr

#if CONFIG(MICROCODE_UPDATE_PRE_RAM)
	/* Apply the matching update before enabling caches or entering NEM. */
	/* The assembly microcode loader returns through the address in ESP. */
	movl	$end_microcode_update, %esp
	jmp	update_bsp_microcode
end_microcode_update:
#endif

#include "processor_startup_mid.inc"
	mid_uncore_init
	pre_nem_uncore_init

#include <cpu/intel/car/non-evict/mtrr_setup.inc>

	/* Enable cache (CR0.CD = 0, CR0.NW = 0). */
	movl	%cr0, %eax
	andl	$(~(CR0_CacheDisable | CR0_NoWriteThrough)), %eax
	invd
	movl	%eax, %cr0

	/* Enable No-Eviction Mode - Setup State (MSR 0x2e0 bit 0) */
	movl	$NoEvictMod_MSR, %ecx
	rdmsr
	orl	$1, %eax
	andl	$~2, %eax
	wrmsr

	/* Address to touch cache line. */
	movl	$_car_mtrr_start, %edi

	/* Number of loops (64-byte cache lines). */
	movl	$_car_mtrr_size, %ecx
	shr	$6, %ecx
	xorl	%ebx, %ebx
	movb	$64, %bl

1:
	movl	%eax, (%edi)
	sfence
	addl	%ebx, %edi
	loop	1b

	/* Enable No-Eviction Mode - Run State (MSR 0x2e0 bit 1) */
	movl	$NoEvictMod_MSR, %ecx
	rdmsr
	orl	$2, %eax
	wrmsr

	post_code(POSTCODE_SOC_ENABLE_CACHE)

	/* Verify all CAR before using it for the stack or C data. */
	cld
	movl	$_car_mtrr_start, %edi
	movl	$_car_mtrr_size, %ecx
	shrl	$2, %ecx
	movl	$0x5a5a5a5a, %eax
	rep	stosl

	movl	$_car_mtrr_start, %edi
	movl	$_car_mtrr_size, %ecx
	shrl	$2, %ecx
1:
	cmpl	%eax, (%edi)
	jne	.Lcar_failure
	addl	$4, %edi
	loop	1b
	jmp	.Lcar_verified

.Lcar_failure:
	post_code(POSTCODE_RAM_FAILURE)
	hlt
	jmp	.Lcar_failure

.Lcar_verified:
	/* Now that CAR is enabled and verified clear it. */
	movl	$_car_mtrr_start, %edi
	movl	$_car_mtrr_size, %ecx
	cld
	xorl	%eax, %eax
	rep	stosb

	/* Set up the C stack, including padding for the three arguments. */
#include "processor_startup_late.inc"
	startup_uncore_base
	processor_workaround
	txt_init
	setup_processor_checkin_data

#include <cpu/intel/car/non-evict/c_entry.inc>

.Lhlt:
	post_code(POSTCODE_DEAD_CODE)
	hlt
	jmp	.Lhlt

fixed_mtrr_list:
	.word	MTRR_FIX_64K_00000
	.word	MTRR_FIX_16K_80000
	.word	MTRR_FIX_16K_A0000
	.word	MTRR_FIX_4K_C0000
	.word	MTRR_FIX_4K_C8000
	.word	MTRR_FIX_4K_D0000
	.word	MTRR_FIX_4K_D8000
	.word	MTRR_FIX_4K_E0000
	.word	MTRR_FIX_4K_E8000
	.word	MTRR_FIX_4K_F0000
	.word	MTRR_FIX_4K_F8000
fixed_mtrr_list_end:

_cache_as_ram_setup_end:

/*
 * Copyright (c) 2016 Jean-Paul Etienne <fractalclone@gmail.com>
 * Copyright (c) 2020 BayLibre, SAS
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/kernel.h>
#include <kernel_internal.h>
#include <zephyr/arch/riscv/csr.h>
#include <stdio.h>
#include <pmp.h>

#ifdef CONFIG_USERSPACE
/*
 * Per-thread (TLS) variable indicating whether execution is in user mode.
 */
Z_THREAD_LOCAL uint8_t is_user_mode;
#endif

void arch_new_thread(struct k_thread *thread, k_thread_stack_t *stack,
		     char *stack_ptr, k_thread_entry_t entry,
		     void *p1, void *p2, void *p3)
{
	extern void z_riscv_thread_start(void);
	struct arch_esf *stack_init;

#ifdef CONFIG_RISCV_SOC_CONTEXT_SAVE
	const struct soc_esf soc_esf_init = {SOC_ESF_INIT};
#endif

	/* Initial stack frame for thread */
	stack_init = (struct arch_esf *)Z_STACK_PTR_ALIGN(
				Z_STACK_PTR_TO_FRAME(struct arch_esf, stack_ptr)
				);

	/* Setup the initial stack frame */
	stack_init->a0 = (unsigned long)entry;
	stack_init->a1 = (unsigned long)p1;
	stack_init->a2 = (unsigned long)p2;
	stack_init->a3 = (unsigned long)p3;

	/*
	 * Following the RISC-V architecture,
	 * the MSTATUS register (used to globally enable/disable interrupt),
	 * as well as the MEPC register (used to by the core to save the
	 * value of the program counter at which an interrupt/exception occurs)
	 * need to be saved on the stack, upon an interrupt/exception
	 * and restored prior to returning from the interrupt/exception.
	 * This shall allow to handle nested interrupts.
	 *
	 * Given that thread startup happens through the exception exit
	 * path, initially set:
	 * 1) MSTATUS to MSTATUS_DEF_RESTORE in the thread stack to enable
	 *    interrupts when the newly created thread will be scheduled;
	 * 2) MEPC to the address of the z_thread_entry in the thread
	 *    stack.
	 * Hence, when going out of an interrupt/exception/context-switch,
	 * after scheduling the newly created thread:
	 * 1) interrupts will be enabled, as the MSTATUS register will be
	 *    restored following the MSTATUS value set within the thread stack;
	 * 2) the core will jump to z_thread_entry, as the program
	 *    counter will be restored following the MEPC value set within the
	 *    thread stack.
	 */
	stack_init->mstatus = MSTATUS_DEF_RESTORE;

#if defined(CONFIG_FPU_SHARING)
	/* thread birth happens through the exception return path */
	thread->arch.exception_depth = 1;
#elif defined(CONFIG_FPU)
	/* Unshared FP mode: enable FPU of each thread. */
	stack_init->mstatus |= MSTATUS_FS_INIT;
#endif

#if defined(CONFIG_USERSPACE)
	/* Clear user thread context */
	z_riscv_pmp_usermode_init(thread);
	thread->arch.priv_stack_start = 0;
#endif /* CONFIG_USERSPACE */

	/* Assign thread entry point and mstatus.MPRV mode. */
	if (IS_ENABLED(CONFIG_USERSPACE)
	    && (thread->base.user_options & K_USER)) {
		/* User thread */
		stack_init->mepc = (unsigned long)k_thread_user_mode_enter;

	} else {
		/* Supervisor thread */
		stack_init->mepc = (unsigned long)z_thread_entry;

#if defined(CONFIG_PMP_STACK_GUARD)
		/* Enable PMP in mstatus.MPRV mode for RISC-V machine mode
		 * if thread is supervisor thread.
		 */
		stack_init->mstatus |= MSTATUS_MPRV;
#endif /* CONFIG_PMP_STACK_GUARD */
	}

#if defined(CONFIG_PMP_STACK_GUARD)
	/* Setup PMP regions of PMP stack guard of thread. */
	z_riscv_pmp_stackguard_prepare(thread);
#endif /* CONFIG_PMP_STACK_GUARD */

#ifdef CONFIG_RISCV_SOC_CONTEXT_SAVE
	stack_init->soc_context = soc_esf_init;
#endif

#ifdef CONFIG_RISCV_SOC_HAS_ISR_STACKING
	SOC_ISR_STACKING_ESR_INIT;
#endif

#ifdef CONFIG_CLIC_SUPPORT_INTERRUPT_LEVEL
	/* Clear the previous interrupt level. */
	stack_init->mcause = 0;
#endif

	thread->callee_saved.sp = (unsigned long)stack_init;

	/* where to go when returning from z_riscv_switch() */
	thread->callee_saved.ra = (unsigned long)z_riscv_thread_start;

#ifdef CONFIG_RISCV_XPAC_RET

	static unsigned long xpacctx_counter = 1;
	thread->callee_saved.xpacctx = xpacctx_counter++; //Assign unique xpacctx value for each thread.

	/*
	 * Compute valid PAC for new thread's entry point.
	 *
	 * Two PACs are needed:
	 * 1. pr0: for z_riscv_switch's ret to z_riscv_thread_start
	 *    - Message = {RA, SP} = {z_riscv_thread_start, stack_init}
	 *
	 * 2. pr1: for mret from ISR epilogue to z_thread_entry
	 *    - Message = {MEPC, SP} = {z_thread_entry, stack_init + sizeof(arch_esf)}
	 *
	 * IMPORTANT: We must save/restore this function's own pr0 because
	 * pac.sign will overwrite it. Without this, arch_new_thread's ret
	 * would fail PAC verification.
	 *
	 * Hardware message format: pac.sign produces {rs2, rs1}
	 * So pac.sign pr, sp_val, ra_val produces {ra_val, sp_val}
	 */
	stack_init->s0 = 0;
	thread->callee_saved.s0 = 0;
	thread->callee_saved.s1 = 0;
	{
		/* Values for pr0 (z_riscv_switch ret) */
		/* Bind to temp registers to avoid using s0/s1 which we clobber manually */
		register unsigned long ra_val __asm__("t0") = thread->callee_saved.ra;
		register unsigned long sp_val __asm__("t1") = thread->callee_saved.sp;

		/* Values for pr1 (mret) */
		register unsigned long mepc_val __asm__("t2") = stack_init->mepc;
		register unsigned long sp_mret __asm__("t5") = (unsigned long)stack_init + sizeof(struct arch_esf);

		/* xpacctx value for tweak - CPU XORs s0/s1 with this CSR value */
		register unsigned long xpacctx_val __asm__("t6") = thread->callee_saved.xpacctx;

		unsigned long long saved_pr0;

		/* Save current function's pr0, compute new thread's pr0 & pr1, restore */
		__asm__ volatile(
			/* Save s0/s1 to t3/t4 */
			"mv t3, s0\n\t"
			"mv t4, s1\n\t"
			
			/* Save this function's pr0 */
			"pac.store pr0, 0(%5)\n\t"

			/* Swap mpacctx: a0 = old mpacctx, mpacctx = xpacctx_new */
			"csrrw a0, 0xBC5, %7\n\t"

			/* Set s0/s1 to 0 to match verify-time values */
			"mv s0, zero\n\t"
			"mv s1, zero\n\t"
			
			/* Compute pr0 for z_riscv_switch ret: Message = {ra, sp}
			 * Command: pac.sign pr0, rs1(low), rs2(high) -> {rs2, rs1}
			 * We want {ra, sp} -> rs2=ra, rs1=sp
			 * So: pac.sign pr0, sp, ra
			 */
			"pac.sign pr0, %1, %0\n\t"
			/* Compute pr1 for mret: Message = {mepc, sp_mret} */
			"pac.sign pr1, %3, %4\n\t"

			/*
			 * PIPELINE OPTIMIZATION: Restore s0/s1 between pac.sign and pac.store.
			 * pac.store immediately after pac.sign causes a 1-cycle pipeline stall
			 * due to data hazard (pac.sign result not yet available).
			 * Inserting these mv instructions hides the latency.
			 */
			"mv s0, t3\n\t"
			"mv s1, t4\n\t"

			/* Restore mpacctx CSR to original value (saved in a0) */
			"csrw 0xBC5, a0\n\t"

			/* Store pr0 to thread->callee_saved.pr0 */
			"pac.store pr0, 0(%2)\n\t"
			/* Store pr1 to stack_init->pr1 */
			"pac.store pr1, 0(%6)\n\t"
			
			/* Restore this function's pr0 */
			"pac.load pr0, 0(%5)\n\t"
			:
			: "r"(ra_val), "r"(sp_val),
			  "r"(&thread->callee_saved.pr0),
			  "r"(sp_mret), "r"(mepc_val),
			  "r"(&saved_pr0),
			  "r"(&stack_init->pr1),
			  "r"(xpacctx_val)
			: "t3", "t4", "a0", "memory"
		);
	}
#endif

	/* our switch handle is the thread pointer itself */
	thread->switch_handle = thread;
}

#ifdef CONFIG_USERSPACE

/*
 * User space entry function
 *
 * This function is the entry point to user mode from privileged execution.
 * The conversion is one way, and threads which transition to user mode do
 * not transition back later, unless they are doing system calls.
 */
FUNC_NORETURN void arch_user_mode_enter(k_thread_entry_t user_entry,
					void *p1, void *p2, void *p3)
{
	unsigned long top_of_user_stack, top_of_priv_stack;
	unsigned long status;

	/* Set up privileged stack */
#ifdef CONFIG_GEN_PRIV_STACKS
	_current->arch.priv_stack_start =
			(unsigned long)z_priv_stack_find(_current->stack_obj);
	/* remove the stack guard from the main stack */
	_current->stack_info.start -= K_THREAD_STACK_RESERVED;
	_current->stack_info.size += K_THREAD_STACK_RESERVED;
#else
	_current->arch.priv_stack_start = (unsigned long)_current->stack_obj;
#endif /* CONFIG_GEN_PRIV_STACKS */
	top_of_priv_stack = Z_STACK_PTR_ALIGN(_current->arch.priv_stack_start +
					      K_KERNEL_STACK_RESERVED +
					      CONFIG_PRIVILEGED_STACK_SIZE);

#ifdef CONFIG_INIT_STACKS
	/* Initialize the privileged stack */
	(void)memset((void *)_current->arch.priv_stack_start, 0xaa,
		     Z_STACK_PTR_ALIGN(K_KERNEL_STACK_RESERVED + CONFIG_PRIVILEGED_STACK_SIZE));
#endif /* CONFIG_INIT_STACKS */

	top_of_user_stack = Z_STACK_PTR_ALIGN(
				_current->stack_info.start +
				_current->stack_info.size -
				_current->stack_info.delta);

	status = csr_read(mstatus);

	/* Set next CPU status to user mode */
	status = INSERT_FIELD(status, MSTATUS_MPP, PRV_U);
	/* Enable IRQs for user mode */
	status = INSERT_FIELD(status, MSTATUS_MPIE, 1);
	/* Disable IRQs for m-mode until the mode switch */
	status = INSERT_FIELD(status, MSTATUS_MIE, 0);

	csr_write(mstatus, status);
	csr_write(mepc, z_thread_entry);

#ifdef CONFIG_PMP_STACK_GUARD
	/* reconfigure as the kernel mode stack will be different */
	z_riscv_pmp_stackguard_prepare(_current);
#endif

	/* Set up Physical Memory Protection */
	z_riscv_pmp_usermode_prepare(_current);
	z_riscv_pmp_usermode_enable(_current);

	/* preserve stack pointer for next exception entry */
	arch_curr_cpu()->arch.user_exc_sp = top_of_priv_stack;

	is_user_mode = true;

	register void *a0 __asm__("a0") = user_entry;
	register void *a1 __asm__("a1") = p1;
	register void *a2 __asm__("a2") = p2;
	register void *a3 __asm__("a3") = p3;

	__asm__ volatile (
	"mv sp, %4; mret"
	:
	: "r" (a0), "r" (a1), "r" (a2), "r" (a3), "r" (top_of_user_stack)
	: "memory");

	CODE_UNREACHABLE;
}

int arch_thread_priv_stack_space_get(const struct k_thread *thread, size_t *stack_size,
				     size_t *unused_ptr)
{
	if ((thread->base.user_options & K_USER) != K_USER) {
		return -EINVAL;
	}

	*stack_size = Z_STACK_PTR_ALIGN(K_KERNEL_STACK_RESERVED + CONFIG_PRIVILEGED_STACK_SIZE);

	return z_stack_space_get((void *)thread->arch.priv_stack_start, *stack_size, unused_ptr);
}

#endif /* CONFIG_USERSPACE */

#ifndef CONFIG_MULTITHREADING

K_KERNEL_STACK_ARRAY_DECLARE(z_interrupt_stacks, CONFIG_MP_MAX_NUM_CPUS, CONFIG_ISR_STACK_SIZE);
K_THREAD_STACK_DECLARE(z_main_stack, CONFIG_MAIN_STACK_SIZE);

FUNC_NORETURN void z_riscv_switch_to_main_no_multithreading(k_thread_entry_t main_entry,
							    void *p1, void *p2, void *p3)
{
	void *main_stack;

	ARG_UNUSED(p1);
	ARG_UNUSED(p2);
	ARG_UNUSED(p3);

	_kernel.cpus[0].id = 0;
	_kernel.cpus[0].irq_stack = (K_KERNEL_STACK_BUFFER(z_interrupt_stacks[0]) +
				     K_KERNEL_STACK_SIZEOF(z_interrupt_stacks[0]));

	main_stack = (K_THREAD_STACK_BUFFER(z_main_stack) +
		      K_THREAD_STACK_SIZEOF(z_main_stack));

	irq_unlock(MSTATUS_IEN);

	__asm__ volatile (
	"mv sp, %0; jalr ra, %1, 0"
	:
	: "r" (main_stack), "r" (main_entry)
	: "memory");

	/* infinite loop */
	irq_lock();
	while (true) {
	}

	CODE_UNREACHABLE; /* LCOV_EXCL_LINE */
}
#endif /* !CONFIG_MULTITHREADING */

int arch_coprocessors_disable(struct k_thread *thread)
{
#ifdef CONFIG_FPU_SHARING
	return arch_float_disable(thread);
#else
	return -ENOTSUP;
#endif
}

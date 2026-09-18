/*
 * systolic_mm_regs.h
 *
 * Shared register map for the systolic array matrix-multiplier accelerator.
 * This MUST stay in sync with rtl/systolic_mm_wb.v's address decode logic —
 * it is the single contract that the real hardware, the QEMU device model,
 * and the Linux kernel driver all agree on.
 *
 * Default config: N=4, DW=8 (INT8 operands), AW=32 (INT32 accumulator)
 */
#ifndef SYSTOLIC_MM_REGS_H
#define SYSTOLIC_MM_REGS_H

#define SYSMM_N   4
#define SYSMM_DW  8
#define SYSMM_AW  32

#define SYSMM_NUM_A_WORDS  4   /* (N*N*DW + 31) / 32 */
#define SYSMM_NUM_B_WORDS  4
#define SYSMM_NUM_C_WORDS  16  /* (N*N*AW + 31) / 32 */

#define SYSMM_A_BASE       0x00
#define SYSMM_B_BASE       0x10
#define SYSMM_CTRL_ADDR    0x20
#define SYSMM_STATUS_ADDR  0x24
#define SYSMM_C_BASE       0x28

#define SYSMM_REG_SPAN     0x68  /* total addressable byte span, for MMIO mapping */

#define SYSMM_CTRL_START_BIT   0x1
#define SYSMM_STATUS_DONE_BIT  0x1

#endif /* SYSTOLIC_MM_REGS_H */

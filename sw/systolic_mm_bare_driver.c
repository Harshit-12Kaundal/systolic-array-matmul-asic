/*
 * systolic_mm_bare_driver.c
 *
 * Stage 2: pure C driver for the systolic array matmul accelerator.
 * This file has ZERO dependency on Verilator, cocotb, or any simulator --
 * it only calls mmio_read32()/mmio_write32(), which are declared here but
 * DEFINED elsewhere (by sim_main.cpp in simulation, or by a real memory-map
 * in an embedded target). This is exactly the separation a real embedded
 * driver keeps: the driver logic is portable, the register access is not.
 */

#include <stdint.h>
#include "systolic_mm_regs.h"
#include "mmio.h"

#ifdef __cplusplus
extern "C" {
#endif

void matmul_write_matrix(uint32_t base_addr, const uint8_t *bytes, int num_words)
{
    int w, b;
    for (w = 0; w < num_words; w++) {
        uint32_t word = 0;
        for (b = 0; b < 4; b++)
            word |= ((uint32_t)bytes[w * 4 + b]) << (8 * b);
        mmio_write32(base_addr + 4u * (uint32_t)w, word);
    }
}

void matmul_read_result(int32_t *out_words, int num_words)
{
    int w;
    for (w = 0; w < num_words; w++)
        out_words[w] = (int32_t)mmio_read32(SYSMM_C_BASE + 4u * (uint32_t)w);
}

void matmul_launch(const uint8_t *a_bytes, const uint8_t *b_bytes)
{
    matmul_write_matrix(SYSMM_A_BASE, a_bytes, SYSMM_NUM_A_WORDS);
    matmul_write_matrix(SYSMM_B_BASE, b_bytes, SYSMM_NUM_B_WORDS);
    mmio_write32(SYSMM_CTRL_ADDR, SYSMM_CTRL_START_BIT);
}

/* Polling variant (Stage 2 baseline -- interrupts come later). Returns 1 on
 * success, 0 on timeout after max_polls status reads. */
int matmul_poll_done(int max_polls)
{
    int i;
    for (i = 0; i < max_polls; i++) {
        uint32_t status = mmio_read32(SYSMM_STATUS_ADDR);
        if (status & SYSMM_STATUS_DONE_BIT)
            return 1;
    }
    return 0;
}

#ifdef __cplusplus
} /* extern "C" */
#endif

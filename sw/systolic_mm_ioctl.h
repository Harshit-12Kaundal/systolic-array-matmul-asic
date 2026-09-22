#ifndef SYSTOLIC_MM_IOCTL_H
#define SYSTOLIC_MM_IOCTL_H

#include <linux/ioctl.h>

#ifdef __KERNEL__
#include <linux/types.h>
#else
#include <stdint.h>
#endif

#include "systolic_mm_regs.h"

struct systolic_mm_matmul_args {
    uint8_t  a_bytes[SYSMM_N * SYSMM_N]; /* row-major A */
    uint8_t  b_bytes[SYSMM_N * SYSMM_N]; /* row-major B */
    int32_t *c_words_out;                /* user pointer, N*N int32 results */
};

#define SYSTOLIC_MM_IOC_MAGIC 's'
#define SYSTOLIC_MM_IOC_LAUNCH _IOWR(SYSTOLIC_MM_IOC_MAGIC, 1, struct systolic_mm_matmul_args)

#endif /* SYSTOLIC_MM_IOCTL_H */

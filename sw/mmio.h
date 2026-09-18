#ifndef MMIO_H
#define MMIO_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Declared here, DEFINED by whatever is actually attached: sim_main.cpp
 * (Verilator) in simulation, or a real memory-mapped pointer on an
 * embedded target. The driver in systolic_mm_bare_driver.c never needs to
 * change between the two. */
uint32_t mmio_read32(uint32_t addr);
void     mmio_write32(uint32_t addr, uint32_t val);

#ifdef __cplusplus
}
#endif

#endif /* MMIO_H */

/*
 * systolic_mm_qemu.c
 *
 * QEMU device model for the systolic-array matrix-multiplier accelerator.
 * This is a FUNCTIONAL emulation of systolic_mm_wb.v's register interface —
 * it performs the actual INT8xINT8->INT32 matmul in C, matching the RTL's
 * documented behavior exactly, so a real Linux kernel driver can be
 * developed and tested at normal speed (RTL simulation is far too slow to
 * boot a full Linux kernel against). This mirrors how real chip companies
 * develop drivers before silicon exists.
 *
 * Intended to be dropped into QEMU's hw/misc/ tree as part of a custom
 * QEMU build (e.g. a "virt" RISC-V machine with this device memory-mapped
 * in). Follows the same skeleton as QEMU's well-known "edu" example device.
 */

#include "qemu/osdep.h"
#include "hw/sysbus.h"
#include "hw/irq.h"
#include "qemu/log.h"
#include "qom/object.h"

#include "systolic_mm_regs.h"

#define TYPE_SYSTOLIC_MM "systolic_mm"
OBJECT_DECLARE_SIMPLE_TYPE(SystolicMMState, SYSTOLIC_MM)

struct SystolicMMState {
    SysBusDevice parent_obj;
    MemoryRegion mmio;
    qemu_irq irq;

    uint8_t  a_bytes[SYSMM_N * SYSMM_N];
    uint8_t  b_bytes[SYSMM_N * SYSMM_N];
    int32_t  c_words[SYSMM_N * SYSMM_N];

    bool     done;
    bool     irq_enabled; /* not exposed as a register yet; reserved */
};

static void systolic_mm_do_matmul(SystolicMMState *s)
{
    int i, j, k;
    for (i = 0; i < SYSMM_N; i++) {
        for (j = 0; j < SYSMM_N; j++) {
            int32_t acc = 0;
            for (k = 0; k < SYSMM_N; k++) {
                int8_t a = (int8_t)s->a_bytes[i * SYSMM_N + k];
                int8_t b = (int8_t)s->b_bytes[k * SYSMM_N + j];
                acc += (int32_t)a * (int32_t)b;
            }
            s->c_words[i * SYSMM_N + j] = acc;
        }
    }
    s->done = true;
    qemu_set_irq(s->irq, 1); /* mirrors done_sticky -> irq in the RTL wrapper */
}

static uint64_t systolic_mm_read(void *opaque, hwaddr addr, unsigned size)
{
    SystolicMMState *s = opaque;

    if (addr >= SYSMM_A_BASE && addr < SYSMM_A_BASE + 4 * SYSMM_NUM_A_WORDS) {
        int word_idx = (addr - SYSMM_A_BASE) / 4;
        uint32_t w = 0;
        int b;
        for (b = 0; b < 4; b++)
            w |= ((uint32_t)s->a_bytes[word_idx * 4 + b]) << (8 * b);
        return w;
    }
    if (addr >= SYSMM_B_BASE && addr < SYSMM_B_BASE + 4 * SYSMM_NUM_B_WORDS) {
        int word_idx = (addr - SYSMM_B_BASE) / 4;
        uint32_t w = 0;
        int b;
        for (b = 0; b < 4; b++)
            w |= ((uint32_t)s->b_bytes[word_idx * 4 + b]) << (8 * b);
        return w;
    }
    if (addr == SYSMM_STATUS_ADDR) {
        uint32_t status = s->done ? SYSMM_STATUS_DONE_BIT : 0;
        s->done = false;               /* clear-on-read, same as the RTL */
        qemu_set_irq(s->irq, 0);
        return status;
    }
    if (addr >= SYSMM_C_BASE && addr < SYSMM_C_BASE + 4 * SYSMM_NUM_C_WORDS) {
        int word_idx = (addr - SYSMM_C_BASE) / 4;
        return (uint32_t)s->c_words[word_idx];
    }

    qemu_log_mask(LOG_GUEST_ERROR, "systolic_mm: bad read at 0x%" HWADDR_PRIx "\n", addr);
    return 0;
}

static void systolic_mm_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    SystolicMMState *s = opaque;

    if (addr >= SYSMM_A_BASE && addr < SYSMM_A_BASE + 4 * SYSMM_NUM_A_WORDS) {
        int word_idx = (addr - SYSMM_A_BASE) / 4;
        int b;
        for (b = 0; b < 4; b++)
            s->a_bytes[word_idx * 4 + b] = (val >> (8 * b)) & 0xFF;
        return;
    }
    if (addr >= SYSMM_B_BASE && addr < SYSMM_B_BASE + 4 * SYSMM_NUM_B_WORDS) {
        int word_idx = (addr - SYSMM_B_BASE) / 4;
        int b;
        for (b = 0; b < 4; b++)
            s->b_bytes[word_idx * 4 + b] = (val >> (8 * b)) & 0xFF;
        return;
    }
    if (addr == SYSMM_CTRL_ADDR) {
        if (val & SYSMM_CTRL_START_BIT) {
            s->done = false;
            systolic_mm_do_matmul(s); /* instant in the model; real HW takes ~3N cycles */
        }
        return;
    }

    qemu_log_mask(LOG_GUEST_ERROR, "systolic_mm: bad write at 0x%" HWADDR_PRIx "\n", addr);
}

static const MemoryRegionOps systolic_mm_ops = {
    .read = systolic_mm_read,
    .write = systolic_mm_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
    .valid = { .min_access_size = 4, .max_access_size = 4 },
};

static void systolic_mm_realize(DeviceState *dev, Error **errp)
{
    SystolicMMState *s = SYSTOLIC_MM(dev);
    SysBusDevice *sbd = SYS_BUS_DEVICE(dev);

    memory_region_init_io(&s->mmio, OBJECT(s), &systolic_mm_ops, s,
                           TYPE_SYSTOLIC_MM, SYSMM_REG_SPAN);
    sysbus_init_mmio(sbd, &s->mmio);
    sysbus_init_irq(sbd, &s->irq);
}

static void systolic_mm_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    dc->realize = systolic_mm_realize;
}

static const TypeInfo systolic_mm_info = {
    .name          = TYPE_SYSTOLIC_MM,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(SystolicMMState),
    .class_init    = systolic_mm_class_init,
};

static void systolic_mm_register_types(void)
{
    type_register_static(&systolic_mm_info);
}

type_init(systolic_mm_register_types)

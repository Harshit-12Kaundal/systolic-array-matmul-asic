/*
 * sim_main.cpp
 *
 * Verilator simulation harness. Implements mmio_read32()/mmio_write32()
 * (declared in mmio.h) by actually clocking the systolic_mm_wb Verilated
 * model and driving/sampling its real wb_* bus signals -- so the C driver
 * in systolic_mm_bare_driver.c is genuinely exercising the RTL's bus
 * protocol, not a shortcut.
 *
 * main() then calls the SAME driver functions a real embedded program
 * would call, with randomized test matrices checked against a C-computed
 * golden reference.
 */

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include "verilated.h"
#include "Vsystolic_mm_wb.h"

extern "C" {
#include "systolic_mm_regs.h"
#include "mmio.h"
void matmul_launch(const uint8_t *a_bytes, const uint8_t *b_bytes);
int  matmul_poll_done(int max_polls);
void matmul_read_result(int32_t *out_words, int num_words);
}

static Vsystolic_mm_wb *dut = nullptr;
static vluint64_t sim_time = 0;

static void tick(void)
{
    dut->clk = 0;
    dut->eval();
    sim_time++;
    dut->clk = 1;
    dut->eval();
    sim_time++;
}

/* One Wishbone-lite transaction = one clock tick, since the RTL is
 * zero-wait-state (wb_ack asserts the same cycle as wb_valid). These MUST
 * be extern "C" since systolic_mm_bare_driver.c (compiled as plain C)
 * calls them by unmangled name. */
extern "C" uint32_t mmio_read32(uint32_t addr)
{
    dut->wb_addr = addr;
    dut->wb_we = 0;
    dut->wb_valid = 1;
    tick();
    uint32_t val = dut->wb_rdata;
    dut->wb_valid = 0;
    return val;
}

extern "C" void mmio_write32(uint32_t addr, uint32_t val)
{
    dut->wb_addr = addr;
    dut->wb_wdata = val;
    dut->wb_we = 1;
    dut->wb_valid = 1;
    tick();
    dut->wb_valid = 0;
    dut->wb_we = 0;
}

static void reset_dut(void)
{
    dut->rst_n = 0;
    dut->wb_valid = 0;
    dut->wb_we = 0;
    dut->wb_addr = 0;
    dut->wb_wdata = 0;
    for (int i = 0; i < 3; i++) tick();
    dut->rst_n = 1;
    tick();
}

static void golden_matmul(const uint8_t *a, const uint8_t *b, int32_t *c)
{
    for (int i = 0; i < SYSMM_N; i++)
        for (int j = 0; j < SYSMM_N; j++) {
            int32_t acc = 0;
            for (int k = 0; k < SYSMM_N; k++) {
                int8_t av = (int8_t)a[i * SYSMM_N + k];
                int8_t bv = (int8_t)b[k * SYSMM_N + j];
                acc += (int32_t)av * (int32_t)bv;
            }
            c[i * SYSMM_N + j] = acc;
        }
}

static void print_matrix_i32(const char *label, const int32_t *m)
{
    printf("%s:\n", label);
    for (int i = 0; i < SYSMM_N; i++) {
        for (int j = 0; j < SYSMM_N; j++)
            printf("%8d ", m[i * SYSMM_N + j]);
        printf("\n");
    }
}

static int run_one_case(const uint8_t *a, const uint8_t *b, const char *label)
{
    int32_t result[SYSMM_N * SYSMM_N];
    int32_t expected[SYSMM_N * SYSMM_N];

    matmul_launch(a, b);
    if (!matmul_poll_done(1000)) {
        printf("[%s] TIMEOUT waiting for done\n", label);
        return 1;
    }
    matmul_read_result(result, SYSMM_NUM_C_WORDS);
    golden_matmul(a, b, expected);

    int mismatches = 0;
    for (int i = 0; i < SYSMM_N * SYSMM_N; i++)
        if (result[i] != expected[i])
            mismatches++;

    if (mismatches == 0) {
        printf("[%s] PASS\n", label);
        return 0;
    } else {
        printf("[%s] FAIL (%d mismatches)\n", label, mismatches);
        print_matrix_i32("  got", result);
        print_matrix_i32("  expected", expected);
        return 1;
    }
}

int main(int argc, char **argv)
{
    Verilated::commandArgs(argc, argv);
    dut = new Vsystolic_mm_wb;

    reset_dut();

    int failures = 0;

    /* Case 1: known values, identity B -> C should equal A */
    {
        uint8_t a[16] = {1,2,3,4, 5,6,(uint8_t)-7,8, 9,(uint8_t)-10,11,12, 13,14,15,(uint8_t)-16};
        uint8_t b[16] = {1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1};
        failures += run_one_case(a, b, "identity_case");
    }

    /* Case 2: overflow-stress corner (-128 * 127) */
    {
        uint8_t a[16], b[16];
        for (int i = 0; i < 16; i++) { a[i] = (uint8_t)(-128); b[i] = (uint8_t)127; }
        failures += run_one_case(a, b, "overflow_case");
    }

    /* Case 3-12: randomized trials, matching the cocotb driver test's coverage */
    srand(42);
    for (int t = 0; t < 10; t++) {
        uint8_t a[16], b[16];
        for (int i = 0; i < 16; i++) {
            a[i] = (uint8_t)(rand() % 256);
            b[i] = (uint8_t)(rand() % 256);
        }
        char label[32];
        snprintf(label, sizeof(label), "random_%d", t);
        failures += run_one_case(a, b, label);
    }

    /* Case 13: back-to-back ops, no reset in between */
    {
        uint8_t a1[16] = {1,2,3,4, 5,6,7,8, 9,10,11,12, 13,14,15,16};
        uint8_t b1[16] = {1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1};
        failures += run_one_case(a1, b1, "back_to_back_1");

        uint8_t a2[16], b2[16];
        for (int i = 0; i < 16; i++) { a2[i] = (uint8_t)(-5); b2[i] = (uint8_t)3; }
        failures += run_one_case(a2, b2, "back_to_back_2");
    }

    printf("\n==================================\n");
    if (failures == 0)
        printf("ALL TESTS PASSED (real C driver -> Verilator RTL model)\n");
    else
        printf("%d TEST(S) FAILED\n", failures);
    printf("==================================\n");

    dut->final();
    delete dut;
    return failures == 0 ? 0 : 1;
}

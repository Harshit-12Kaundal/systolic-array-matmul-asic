/*
 * test_systolic_mm.c
 *
 * User-space test program. Opens /dev/systolic_mm, launches a matmul via
 * ioctl (which the kernel driver executes via interrupt-driven MMIO), and
 * checks the result against the expected values.
 */

#include <stdio.h>
#include <stdint.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>

#include "systolic_mm_ioctl.h"

static void print_matrix_i8(const char *label, const uint8_t *m)
{
    int i, j;
    printf("%s:\n", label);
    for (i = 0; i < SYSMM_N; i++) {
        for (j = 0; j < SYSMM_N; j++)
            printf("%4d ", (int8_t)m[i * SYSMM_N + j]);
        printf("\n");
    }
}

static void print_matrix_i32(const char *label, const int32_t *m)
{
    int i, j;
    printf("%s:\n", label);
    for (i = 0; i < SYSMM_N; i++) {
        for (j = 0; j < SYSMM_N; j++)
            printf("%8d ", m[i * SYSMM_N + j]);
        printf("\n");
    }
}

int main(void)
{
    int fd;
    struct systolic_mm_matmul_args args;
    int32_t result[SYSMM_N * SYSMM_N];
    int32_t expected[SYSMM_N * SYSMM_N];
    int i, j, k, mismatches = 0;

    uint8_t a[16] = { 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16 };
    uint8_t b[16] = { 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1 };

    for (i = 0; i < 16; i++) {
        args.a_bytes[i] = a[i];
        args.b_bytes[i] = b[i];
    }
    args.c_words_out = result;

    fd = open("/dev/systolic_mm", O_RDWR);
    if (fd < 0) {
        perror("open /dev/systolic_mm");
        return 1;
    }

    if (ioctl(fd, SYSTOLIC_MM_IOC_LAUNCH, &args) < 0) {
        perror("ioctl SYSTOLIC_MM_IOC_LAUNCH");
        close(fd);
        return 1;
    }
    close(fd);

    for (i = 0; i < SYSMM_N; i++)
        for (j = 0; j < SYSMM_N; j++) {
            int32_t acc = 0;
            for (k = 0; k < SYSMM_N; k++)
                acc += (int8_t)a[i * SYSMM_N + k] * (int8_t)b[k * SYSMM_N + j];
            expected[i * SYSMM_N + j] = acc;
        }

    print_matrix_i8("A", a);
    print_matrix_i8("B", b);
    print_matrix_i32("C (from hardware/driver)", result);
    print_matrix_i32("C (expected)", expected);

    for (i = 0; i < SYSMM_N * SYSMM_N; i++)
        if (result[i] != expected[i])
            mismatches++;

    if (mismatches == 0) {
        printf("PASS: all %d elements match.\n", SYSMM_N * SYSMM_N);
        return 0;
    } else {
        printf("FAIL: %d mismatches.\n", mismatches);
        return 1;
    }
}

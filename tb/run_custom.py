import numpy as np
import cocotb
from cocotb.clock import Clock
from cocotb.triggers import RisingEdge, Timer

N = 4
DW = 8
AW = 32

# ---- EDIT THESE TWO MATRICES TO WHATEVER YOU WANT TO TEST ----
A = [[1, 2, 3, 4],
     [5, 6, -7, 8],
     [9, -10, 11, 12],
     [13, 14, 15, -16]]

B = [[1, 0, 2, 9],
     [0, 1, 0, 0],
     [0, 20, 1, 0],
     [4, 0, 9, 1]]
# ----------------------------------------------------------------


def pack_matrix(mat, width):
    val = 0
    for i in range(N):
        for j in range(N):
            v = int(mat[i][j]) & ((1 << width) - 1)
            val |= v << ((i * N + j) * width)
    return val


def unpack_matrix(val, width, signed=True):
    mat = np.zeros((N, N), dtype=np.int64)
    mask = (1 << width) - 1
    sign_bit = 1 << (width - 1)
    for i in range(N):
        for j in range(N):
            raw = (val >> ((i * N + j) * width)) & mask
            if signed and (raw & sign_bit):
                raw -= (1 << width)
            mat[i][j] = raw
    return mat


@cocotb.test()
async def run_my_matrices(dut):
    cocotb.start_soon(Clock(dut.clk, 10, units="ns").start())

    dut.rst_n.value = 0
    dut.start.value = 0
    dut.a_flat.value = 0
    dut.b_flat.value = 0
    for _ in range(3):
        await RisingEdge(dut.clk)
    dut.rst_n.value = 1
    await RisingEdge(dut.clk)

    a = np.array(A, dtype=np.int64)
    b = np.array(B, dtype=np.int64)

    dut.a_flat.value = pack_matrix(a, DW)
    dut.b_flat.value = pack_matrix(b, DW)
    dut.start.value = 1
    await RisingEdge(dut.clk)
    dut.start.value = 0

    for _ in range(100):
        await RisingEdge(dut.clk)
        if dut.done.value == 1:
            break

    await Timer(1, units="ns")
    c_val = dut.c_flat.value.integer
    c = unpack_matrix(c_val, AW, signed=True)

    print("\n===== A =====")
    print(a)
    print("===== B =====")
    print(b)
    print("===== C = A x B (hardware result) =====")
    print(c)
    print("===== C (numpy reference, for comparison) =====")
    print(a @ b)

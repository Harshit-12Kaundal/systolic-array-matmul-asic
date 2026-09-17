import random
import numpy as np
import cocotb
from cocotb.clock import Clock
from cocotb.triggers import RisingEdge, Timer

N = 4
DW = 8
AW = 32


def pack_matrix(mat, width):
    val = 0
    for i in range(N):
        for j in range(N):
            v = int(mat[i][j]) & ((1 << width) - 1)
            val |= v << ((i * N + j) * width)
    return val


def unpack_matrix(val, width, signed):
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


async def reset(dut):
    dut.rst_n.value = 0
    dut.start.value = 0
    dut.a_flat.value = 0
    dut.b_flat.value = 0
    for _ in range(3):
        await RisingEdge(dut.clk)
    dut.rst_n.value = 1
    await RisingEdge(dut.clk)


async def run_matmul(dut, a, b):
    dut.a_flat.value = pack_matrix(a, DW)
    dut.b_flat.value = pack_matrix(b, DW)
    dut.start.value = 1
    await RisingEdge(dut.clk)
    dut.start.value = 0

    for _ in range(20 * N + 20):
        await RisingEdge(dut.clk)
        if dut.done.value == 1:
            break
    else:
        raise TimeoutError("done was never asserted")

    await Timer(1, units="ns")
    c_val = dut.c_flat.value.integer
    return unpack_matrix(c_val, AW, signed=True)


@cocotb.test()
async def test_known_matrices(dut):
    cocotb.start_soon(Clock(dut.clk, 10, units="ns").start())
    await reset(dut)
    a = np.array([[1, 2, 3, 4], [5, 6, 7, 8], [9, 10, 11, 12], [13, 14, 15, 16]], dtype=np.int64)
    b = np.eye(4, dtype=np.int64)
    c = await run_matmul(dut, a, b)
    expected = (a @ b).astype(np.int64)
    assert np.array_equal(c, expected), f"got:\n{c}\nexpected:\n{expected}"


@cocotb.test()
async def test_random_matrices(dut):
    cocotb.start_soon(Clock(dut.clk, 10, units="ns").start())
    await reset(dut)
    random.seed(42)
    for trial in range(15):
        a = np.array([[random.randint(-128, 127) for _ in range(N)] for _ in range(N)], dtype=np.int64)
        b = np.array([[random.randint(-128, 127) for _ in range(N)] for _ in range(N)], dtype=np.int64)
        c = await run_matmul(dut, a, b)
        expected = (a @ b).astype(np.int64)
        assert np.array_equal(c, expected), f"trial {trial} mismatch\nA:\n{a}\nB:\n{b}\ngot:\n{c}\nexpected:\n{expected}"


@cocotb.test()
async def test_negative_extremes(dut):
    cocotb.start_soon(Clock(dut.clk, 10, units="ns").start())
    await reset(dut)
    a = np.full((N, N), -128, dtype=np.int64)
    b = np.full((N, N), 127, dtype=np.int64)
    c = await run_matmul(dut, a, b)
    expected = (a @ b).astype(np.int64)
    assert np.array_equal(c, expected), f"got:\n{c}\nexpected:\n{expected}"

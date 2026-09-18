import random
import numpy as np
import cocotb
from cocotb.clock import Clock
from cocotb.triggers import RisingEdge

N = 4
DW = 8
AW = 32

NUM_A_WORDS = (N * N * DW + 31) // 32
NUM_B_WORDS = (N * N * DW + 31) // 32
NUM_C_WORDS = (N * N * AW + 31) // 32

A_BASE = 0x00
B_BASE = A_BASE + 4 * NUM_A_WORDS
CTRL_ADDR = B_BASE + 4 * NUM_B_WORDS
STATUS_ADDR = CTRL_ADDR + 4
C_BASE = STATUS_ADDR + 4


async def wb_write(dut, addr, data):
    dut.wb_addr.value = addr
    dut.wb_wdata.value = data & 0xFFFFFFFF
    dut.wb_we.value = 1
    dut.wb_valid.value = 1
    await RisingEdge(dut.clk)
    dut.wb_valid.value = 0
    dut.wb_we.value = 0


async def wb_read(dut, addr):
    dut.wb_addr.value = addr
    dut.wb_we.value = 0
    dut.wb_valid.value = 1
    await RisingEdge(dut.clk)
    val = dut.wb_rdata.value.integer
    dut.wb_valid.value = 0
    return val


def pack_matrix_words(mat, width):
    total_bits = N * N * width
    num_words = (total_bits + 31) // 32
    big = 0
    for i in range(N):
        for j in range(N):
            v = int(mat[i][j]) & ((1 << width) - 1)
            big |= v << ((i * N + j) * width)
    words = []
    for w in range(num_words):
        words.append((big >> (w * 32)) & 0xFFFFFFFF)
    return words


def unpack_c_words(words, width, signed=True):
    big = 0
    for w, val in enumerate(words):
        big |= val << (w * 32)
    mat = np.zeros((N, N), dtype=np.int64)
    mask = (1 << width) - 1
    sign_bit = 1 << (width - 1)
    for i in range(N):
        for j in range(N):
            raw = (big >> ((i * N + j) * width)) & mask
            if signed and (raw & sign_bit):
                raw -= (1 << width)
            mat[i][j] = raw
    return mat


async def reset(dut):
    dut.rst_n.value = 0
    dut.wb_valid.value = 0
    dut.wb_we.value = 0
    dut.wb_addr.value = 0
    dut.wb_wdata.value = 0
    for _ in range(3):
        await RisingEdge(dut.clk)
    dut.rst_n.value = 1
    await RisingEdge(dut.clk)


async def driver_matmul(dut, a, b):
    a_words = pack_matrix_words(a, DW)
    b_words = pack_matrix_words(b, DW)

    for i, w in enumerate(a_words):
        await wb_write(dut, A_BASE + 4 * i, w)
    for i, w in enumerate(b_words):
        await wb_write(dut, B_BASE + 4 * i, w)

    await wb_write(dut, CTRL_ADDR, 0x1)

    for _ in range(200):
        status = await wb_read(dut, STATUS_ADDR)
        if status & 0x1:
            break
        await RisingEdge(dut.clk)
    else:
        raise TimeoutError("status.done never set")

    c_words = []
    for i in range(NUM_C_WORDS):
        c_words.append(await wb_read(dut, C_BASE + 4 * i))

    return unpack_c_words(c_words, AW, signed=True)


@cocotb.test()
async def test_bus_random_matmul(dut):
    cocotb.start_soon(Clock(dut.clk, 10, units="ns").start())
    await reset(dut)

    random.seed(7)
    for trial in range(10):
        a = np.array([[random.randint(-128, 127) for _ in range(N)] for _ in range(N)], dtype=np.int64)
        b = np.array([[random.randint(-128, 127) for _ in range(N)] for _ in range(N)], dtype=np.int64)

        c = await driver_matmul(dut, a, b)
        expected = (a @ b).astype(np.int64)
        assert np.array_equal(c, expected), f"trial {trial} mismatch\nA:\n{a}\nB:\n{b}\ngot:\n{c}\nexpected:\n{expected}"


@cocotb.test()
async def test_status_clears_on_read(dut):
    cocotb.start_soon(Clock(dut.clk, 10, units="ns").start())
    await reset(dut)

    a = np.eye(N, dtype=np.int64)
    b = np.eye(N, dtype=np.int64)
    await driver_matmul(dut, a, b)

    status = await wb_read(dut, STATUS_ADDR)
    assert status & 0x1 == 0, "done bit should be clear after being read once"


@cocotb.test()
async def test_back_to_back_ops(dut):
    cocotb.start_soon(Clock(dut.clk, 10, units="ns").start())
    await reset(dut)

    a1 = np.array([[1, 2, 3, 4], [5, 6, 7, 8], [9, 10, 11, 12], [13, 14, 15, 16]], dtype=np.int64)
    b1 = np.eye(N, dtype=np.int64)
    c1 = await driver_matmul(dut, a1, b1)
    assert np.array_equal(c1, a1 @ b1)

    a2 = np.full((N, N), -5, dtype=np.int64)
    b2 = np.full((N, N), 3, dtype=np.int64)
    c2 = await driver_matmul(dut, a2, b2)
    assert np.array_equal(c2, a2 @ b2)

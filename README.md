# Systolic Array Matrix Multiplier — ASIC Tapeout (SKY130 + IHP SG13G2)

## Overview
A parameterizable N×N (default 4×4) output-stationary systolic array performing
INT8×INT8 → INT32 matrix multiplication, designed in Verilog and taken through
full RTL-to-GDSII across two open-source 130nm PDKs (SkyWater SKY130 and IHP
SG13G2) using the LibreLane flow.

## Design
The array consists of 16 processing elements (PEs), each performing a signed
multiply-accumulate per cycle, wired in a classic diagonal-skew dataflow — the
same architectural pattern used in TPU/NPU-class AI accelerators. A control FSM
handles matrix load, skewed streaming, and result latch-out. The core exposes
wide flattened buses (128-bit a_flat/b_flat inputs, 512-bit c_flat output)
suited for on-chip interconnect integration rather than direct package I/O.

## Verification
Functional correctness was verified with a cocotb testbench (Icarus Verilog)
against a NumPy golden reference across randomized signed-INT8 matrices and
edge-case overflow conditions. A signedness bug was found and fixed during this
process — an unsized unsigned literal inside a ternary expression was silently
zero-extending negative operands before multiplication, per Verilog's
context-determined sizing rules.

## Physical Implementation

|                                | SKY130A                        | IHP SG13G2                  |
|--------------------------------|---------------------------------|------------------------------|
| DRC / LVS / Antenna            | Passed                          | Passed                       |
| Clock                          | 40ns (25MHz)                    | 40ns (25MHz)                 |
| Setup slack (worst-case)       | +10.66ns                        | +26.53ns                     |
| Hold violations                | 0                                | 0                             |
| Max Cap / Max Slew violations  | Isolated to ss_100C_1v60 corner | 0 (across 3 corners tested)  |
| PVT corners characterized      | 9                                | 3                             |

Setup and hold timing close cleanly on both PDKs. SKY130 shows residual
max-transition/max-capacitance margin violations confined entirely to the
ss_100C_1v60 (slow-process, 100°C, 1.60V) corner — the deliberately worst-case
PVT extreme — consistent with the drive-strength demands of 16 parallel MAC
units; this is a documented, reportable limitation rather than a design flaw,
with a clear path to closure (custom SDC constraints or drive-strength-optimized
cell resizing). IHP's characterized corner set is narrower (3 vs. 9), so the
comparison is directionally informative rather than exactly equivalent.

## Scope Note
Unlike a prior SPI-controller tapeout, this project does not implement a
physical I/O padframe: the design's wide parallel buses would require 770+
package pins, which is impractical for direct off-chip integration. This is a
deliberate architectural choice — a real deployment of this block would
connect via on-chip interconnect, not package-level pins — documented here as
a boundary of scope rather than an omission.

## Repository Structure
- src/        — Verilog RTL (pe.v, systolic_array.v, systolic_mm_top.v)
- tb/         — cocotb testbench and custom-matrix test script
- config.json — LibreLane flow configuration
- results/    — Final GDS and STA summary reports for SKY130 and IHP SG13G2

## Tools Used
Verilog, cocotb, Icarus Verilog, LibreLane, OpenROAD, Magic, KLayout

SIM ?= icarus
TOPLEVEL_LANG ?= verilog

VERILOG_SOURCES += $(PWD)/../src/pe.v
VERILOG_SOURCES += $(PWD)/../src/systolic_array.v
VERILOG_SOURCES += $(PWD)/../src/systolic_mm_top.v

TOPLEVEL = systolic_mm_top
MODULE   = run_custom

include $(shell cocotb-config --makefiles)/Makefile.sim

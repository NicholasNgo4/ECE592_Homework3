CC      = gcc
# -O2 is safe: every timed instruction lives in generated assembly, so the
# compiler cannot reorder or delete it.
CFLAGS  = -O2 -g -std=c11 -Wall -Wextra -Icommon -Iexp/rob_prf
BUILD   = build

# ROB/PRF sweep range: N = 0, STEP, ..., MAX filler instructions per half.
ROB_MAX  ?= 640
ROB_STEP ?= 4

all: $(BUILD)/rob_prf

$(BUILD)/rob_prf_kernels.S $(BUILD)/rob_prf_table.c: exp/rob_prf/gen_kernels.py Makefile
	python3 exp/rob_prf/gen_kernels.py --outdir $(BUILD) --max $(ROB_MAX) --step $(ROB_STEP)

$(BUILD)/rob_prf: exp/rob_prf/main.c common/hw3_common.c $(BUILD)/rob_prf_table.c $(BUILD)/rob_prf_kernels.S common/hw3_common.h exp/rob_prf/rob_prf.h
	$(CC) $(CFLAGS) -o $@ exp/rob_prf/main.c common/hw3_common.c $(BUILD)/rob_prf_table.c $(BUILD)/rob_prf_kernels.S

clean:
	rm -rf $(BUILD)

.PHONY: all clean

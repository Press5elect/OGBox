# OGBox tests (OGBox by Press5elect, GPL-3.0-or-later): build with nxdk.
#   export NXDK_DIR=/path/to/nxdk; PATH=$NXDK_DIR/bin:$PATH; make
XBE_TITLE = OGBox\ tests
GEN_XISO = ogbox-tests.iso
SRCS = $(CURDIR)/main.c $(CURDIR)/t_cpu.c $(CURDIR)/t_time.c $(CURDIR)/t_mem.c $(CURDIR)/t_disc.c $(CURDIR)/t_hdd.c
CFLAGS += -O2 -Wall
NXDK_DIR ?= $(CURDIR)/../../../../nxdk

include $(NXDK_DIR)/Makefile

$(OUTPUT_DIR)/testdata.bin: gen-testdata.py | $(OUTPUT_DIR)
	@echo "[ DATA     ] $@"
	$(VE)python3 -I gen-testdata.py $@

$(GEN_XISO): $(OUTPUT_DIR)/testdata.bin

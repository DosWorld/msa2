# POSIX / GNU make build for MSA2 in-process unit tests.
#
# Builds a single binary `TESTMSA2` that links every source file from
# ../src (including MSA.C, with main() renamed via -Dmain=) plus the
# test files in this directory. Run with:
#
#     make -f MAKEGNU.MAK
#     ./TESTMSA2                  # run all tests
#     ./TESTMSA2 misc             # run only tests whose name contains "misc"
#
# C89 only (-std=gnu89 keeps the K&R `inline` from the existing sources).
# All file names are 8.3-compliant per project rules.
#

CC      ?= gcc
CFLAGS  ?= -O2 -Wall -Wno-missing-braces -std=gnu89 -g
SRC_DIR  = ../src

# All non-main src files compile normally.
SRC_OTHER = $(SRC_DIR)/TABLES.C $(SRC_DIR)/ASSEMBLR.C \
            $(SRC_DIR)/MISC.C $(SRC_DIR)/LEX.C $(SRC_DIR)/EXPR.C \
            $(SRC_DIR)/RDF.C

TEST_C   = RUNNER.C T_MISC.C T_EXPR.C T_LEX.C T_RDF.C T_ASM.C
HDRS     = TEST.H $(SRC_DIR)/MSA2.H $(SRC_DIR)/LEX.H $(SRC_DIR)/EXPR.H \
           $(SRC_DIR)/RDF.H

TARGET   = TESTMSA2

all: $(TARGET)

MSA_NMAI.O: $(SRC_DIR)/MSA.C $(HDRS)
	$(CC) $(CFLAGS) -x c -Dmain=msa2_unused_main -c $(SRC_DIR)/MSA.C -o $@

$(TARGET): MSA_NMAI.O $(SRC_OTHER) $(TEST_C) $(HDRS)
	$(CC) $(CFLAGS) -x c $(SRC_OTHER) $(TEST_C) -x none MSA_NMAI.O -o $(TARGET)

run: $(TARGET)
	./$(TARGET)

clean:
	rm -f $(TARGET) TMPTEST.ASM MSA_NMAI.O *.o

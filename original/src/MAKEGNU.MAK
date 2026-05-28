# POSIX / GNU make build for MSA2.
# The other MAKEFILE in this directory is for MS-DOS Open Watcom (wcl) and
# cannot be used with GNU make. Use this file under Linux/macOS:
#
#     make -f MAKEGNU.MAK
#     make -f MAKEGNU.MAK test
#     make -f MAKEGNU.MAK clean
#
# (File name is 8.3 compliant per project rules.)

CC      ?= gcc
CFLAGS  ?= -O3 -Wall -Wno-missing-braces -std=gnu89
SRCS    = MSA.C TABLES.C ASSEMBLR.C MISC.C LEX.C EXPR.C RDF.C
HDRS    = MSA2.H LEX.H EXPR.H RDF.H
TARGET  = msa2

all: $(TARGET)

$(TARGET): $(SRCS) $(HDRS)
	$(CC) $(CFLAGS) -x c $(SRCS) -o $(TARGET)

test:
	$(MAKE) -C ../tests -f MAKEGNU.MAK run

clean:
	rm -f $(TARGET) *.o
	$(MAKE) -C ../tests -f MAKEGNU.MAK clean

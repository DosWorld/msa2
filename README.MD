# MSA2 x86 assembler for real-mode DOS

     ooo        ooooo  .oooooo..o       .o.         .oooo.
     `88.       .888' d8P'    `Y8      .888.      .dP""Y88b
      888b     d'888  Y88bo.          .8"888.           ]8P'
      8 Y88. .P  888   `"Y8888o.     .8' `888.        .d8P'
      8  `888'   888       `"Y88b   .88ooo8888.     .dP'
      8    Y     888  oo     .d8P  .8'     `888.  .oP     .o
     o8o        o888o 8""88888P'  o88o     o8888o 8888888888

**MSA2** is a old and free 8086 two-pass assembler for DOS targets.
It targets the Intel 8086 instruction set. A handful of 186/286-era
mnemonics inherited from the original codebase (`PUSHA`, `POPA`,
`ENTER`, `LEAVE`, `BOUND`) are also accepted; no further 186+
extensions are planned.

Initialy written by a 14-year old Swedish programmer Robert Ostling
(in 2000-2001) in Borland C++ 2.0. Now, MSA2 is ported to Watcom C
and GCC (and/or mingw).

**MSA2** Consume to small piece of memory - i think, will be enougth
just 128K. MSA2 does not require DPMI.

**MSA2** is pretty fast and the only file you'll need to build a program
is MSA2.EXE (50 KB). It runs fine on my old 286.

I had not test MSA2 in FreeDOS, Linux, windows yet, but there should
not be any difference where running MSA2.

To compile your source with MSA2 just type:

    MSA2 coolprog.asm -o coolprog.com

## Output formats

**MSA2** produces next formats:

* **-f bin** flat binary output
* **-f com** .com file
* **-f texe** .exe file with tiny memory model
* **-f rdf** RDOFF2 relocatable object file (NASM-compatible `-f rdf`),
  with relocations, exported globals and BSS.

The `-f rdf` target supports a `SECTION` (or `SEGMENT`) directive switching
between `.text`, `.data` and `.bss`. Use `RESB`, `RESW`, `RESD` inside `.bss`
to reserve uninitialised storage; use `DB`, `DW`, `DD` in `.data` for
initialised storage. See `original/doc/rdoff2-spec.md` for the file format.

## Preprocessor

NASM-style directives:

* `%define NAME body` — single-line text alias for use in operands.
* `%undef NAME` — remove a previously-defined alias.
* `%macro NAME N` ... `%endmacro` — multi-line block macros with up
  to 9 positional args (`%1`..`%9`) and per-invocation labels
  (`%%name`).
* `%include "file.asm"` — inline another source file. Nestable up to
  8 levels deep; diagnostics inside an included file point at the
  included file and its line, not the parent.

Storage is unbounded (linked lists) — no compile-time cap on macro
count, define count, or include depth besides the explicit limits
above. See `original/doc/DOC.TXT` for the full reference.

## Differences with MSA

* Added missed opcodes
* Added .exe generation
* Added .rdf generation
* Added macros
* Added defines
* Added including file

## Differences with NASM

* Limited macros / defines
* No conditional jumps optimization (near/short)

## Build MSA2

MSA2 could be compiled for DOS, Linux and windows with Open Watcom C,
GCC, GCC/MINGW.

### DOS

To build DOS version with Watcom C, just type (in original/src/)

    cd original\src
    make

or

    cd original\src
    wmake

### Linux / macOS (POSIX)

A separate POSIX GNU-make file is provided so it does not collide with the
Watcom `MAKEFILE` (both file names are DOS 8.3-compliant):

    cd original/src
    make -f MAKEGNU.MAK          # build the msa2 binary
    make -f MAKEGNU.MAK test     # build and run the C89 unit tests

### windows

To build win32 version with mingw:

    i686-w64-mingw32-gcc -m32 -O3 -funsigned-char MSA.C TABLES.C ASSEMBLR.C MISC.C LEX.C EXPR.C RDF.C -o MSA2W.EXE
    strip MSA2W.EXE

## License

This is free software: you can redistribute it and/or modify it under
the terms of the MIT license. See LICENSE file.

/*

MIT License

Copyright (c) 2019-2026 DosWorld

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.

        Part of the MSA2 assembler

RDOFF2 (NASM "rdf") object file writer.

Format reference: original/doc/rdoff2-spec.md (sections 2-4).

*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "MSA2.H"
#include "EXPR.H"
#include "RDF.H"

#define RDFREC_RELOC    1
#define RDFREC_IMPORT   2
#define RDFREC_GLOBAL   3
#define RDFREC_BSS      5
#define RDFREC_SEGRELOC 6

static void put_byte(FILE *o, byte v) {
    fputc(v, o);
}

static void put_word(FILE *o, word v) {
    fputc(v & 0xff, o);
    fputc((v >> 8) & 0xff, o);
}

static void put_dword(FILE *o, dword v) {
    fputc(v & 0xff, o);
    fputc((v >> 8) & 0xff, o);
    fputc((v >> 16) & 0xff, o);
    fputc((v >> 24) & 0xff, o);
}

static void write_import_record(FILE *o, t_constant *c) {
    /* RDFREC_IMPORT: byte flags, word segid (import id), ASCIIZ name. */
    int namelen = (int)strlen(c->name);
    int payload = 1 + 2 + namelen + 1;

    put_byte(o, RDFREC_IMPORT);
    put_byte(o, (byte)payload);
    put_byte(o, 0);                 /* flags */
    put_word(o, (word)c->value);    /* segment id assigned at parse time */
    fwrite(c->name, 1, namelen + 1, o);
}

static void write_global_record(FILE *o, t_constant *c) {
    /* RDFREC_GLOBAL: byte flags, byte segid, dword offset, ASCIIZ name. */
    int namelen = (int)strlen(c->name);
    int payload = 1 + 1 + 4 + namelen + 1;
    byte segid;

    switch(c->section) {
    case SECTION_DATA: segid = 1; break;
    case SECTION_BSS:  segid = 2; break;
    default:           segid = 0; break;
    }

    put_byte(o, RDFREC_GLOBAL);
    put_byte(o, (byte)payload);
    put_byte(o, 0x01);              /* exported */
    put_byte(o, segid);
    put_dword(o, (dword)c->value);
    fwrite(c->name, 1, namelen + 1, o);
}

static void write_bss_record(FILE *o, dword amount) {
    put_byte(o, RDFREC_BSS);
    put_byte(o, 4);
    put_dword(o, amount);
}

static void write_reloc_record(FILE *o, t_reloc *r) {
    byte flags;
    if(r->is_segreloc) {
        /* RDFREC_SEGRELOC: same field layout as RELOC but type=6 and
         * the leading byte is the physical segment index to patch
         * (no relative-flag bit). */
        put_byte(o, RDFREC_SEGRELOC);
        put_byte(o, 8);
        put_byte(o, r->seg & 0x3f);
        put_dword(o, r->ofs);
        put_byte(o, r->width);
        put_word(o, r->rseg);
        return;
    }
    flags = (r->seg & 0x3f) | (r->relative ? 0x40 : 0);
    put_byte(o, RDFREC_RELOC);
    put_byte(o, 8);
    put_byte(o, flags);
    put_dword(o, r->ofs);
    put_byte(o, r->width);
    put_word(o, r->rseg);
}

static dword compute_header_size(void) {
    dword sz = 0;
    t_constant *c;
    t_reloc *r;

    for(c = constants; c != NULL; c = (t_constant *)c->next) {
        if(CONST_TYPE(c) == CONST_IMPORT) {
            sz += 2 + 1 + 2 + strlen(c->name) + 1;
        } else if(c->is_export) {
            sz += 2 + 1 + 1 + 4 + strlen(c->name) + 1;
        }
    }
    if(bss_amount > 0) {
        sz += 2 + 4;
    }
    for(r = relocs; r != NULL; r = r->next) {
        sz += 2 + 8;
    }
    return sz;
}

int write_rdf(FILE *o) {
    dword header_size, module_size;
    t_constant *c;
    t_reloc *r;

    fwrite("RDOFF2", 1, 6, o);

    header_size = compute_header_size();
    /* module_size: total file size in bytes, not including the 6-byte sig + 4 module_size + 4 header_size. */
    /* Spec section 2 makes "Module Size" the *total* file size. */
    module_size = 0; /* patched below */

    put_dword(o, 0);            /* module size placeholder */
    put_dword(o, header_size);

    /* Header records. */
    for(c = constants; c != NULL; c = (t_constant *)c->next) {
        if(CONST_TYPE(c) == CONST_IMPORT) {
            write_import_record(o, c);
        } else if(c->is_export) {
            write_global_record(o, c);
        }
    }
    if(bss_amount > 0) {
        write_bss_record(o, bss_amount);
    }
    for(r = relocs; r != NULL; r = r->next) {
        write_reloc_record(o, r);
    }

    /* Code segment. */
    if(code_size > 0) {
        put_word(o, 1);             /* type = code */
        put_word(o, 0);             /* segment number */
        put_word(o, 0);             /* reserved */
        put_dword(o, code_size);
        fwrite(outprog, 1, code_size, o);
    }

    /* Data segment. */
    if(dataptr > 0) {
        put_word(o, 2);             /* type = data */
        put_word(o, 1);             /* segment number */
        put_word(o, 0);             /* reserved */
        put_dword(o, dataptr);
        fwrite(dataprog, 1, dataptr, o);
    }

    /* EOF segment. */
    put_word(o, 0);
    put_word(o, 0);
    put_word(o, 0);
    put_dword(o, 0);

    module_size = (dword)ftell(o);
    fseek(o, 6, SEEK_SET);
    put_dword(o, module_size);

    return 1;
}

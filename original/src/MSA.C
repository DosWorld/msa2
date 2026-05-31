/*

MIT License

Copyright (c) 2000, 2001, 2019 Robert Ostling
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

*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <ctype.h>
#include "MSA2.H"
#include "EXPR.H"
#include "LEX.H"
#include "RDF.H"

#if defined(__APPLE__) || defined(__linux__) || defined(__unix__)
static char *strupr(char *s) {
    char *p = s;
    while(*p) { *p = (char)toupper((unsigned char)*p); p++; }
    return s;
}
#endif

char err_msg[512];
char outname[256];
char *inputname = NULL;
FILE* outfile;
int target;
byte* outprog;
word outptr;

byte *dataprog;
word dataptr;
dword bss_amount;
int cur_section;
t_reloc *relocs;

int errors, warnings;

byte quiet = 1;

int pass;
int passes = 2;

word org = 0x0100;
char is_org_def;

word code_size;
word bss_size;
word entry_point;
char entry_point_def = 0;

void add_reloc(byte seg, dword ofs, byte width, word rseg, byte relative) {
    t_reloc *r;
    if(target != TARGET_RDF || pass != passes - 1) {
        return;
    }
    r = (t_reloc *)MSA_MALLOC(sizeof(t_reloc));
    r->seg = seg;
    r->ofs = ofs;
    r->width = width;
    r->rseg = rseg;
    r->relative = relative;
    r->is_segreloc = 0;
    r->next = relocs;
    relocs = r;
}

void add_segreloc(byte seg, dword ofs, word rseg) {
    t_reloc *r;
    if(target != TARGET_RDF || pass != passes - 1) {
        return;
    }
    r = (t_reloc *)MSA_MALLOC(sizeof(t_reloc));
    r->seg = seg;
    r->ofs = ofs;
    r->width = 2;
    r->rseg = rseg;
    r->relative = 0;
    r->is_segreloc = 1;
    r->next = relocs;
    relocs = r;
}

void check_entry_point() {
    if(!pass) {
        return;
    }
    switch(target) {
    case TARGET_BIN:
    case TARGET_RDF:
        break;
    case TARGET_COM:
        if(entry_point != 0x0100) {
            out_msg_int("Invalid entry point 0x%04X for .COM-file.", 0, entry_point);
        }
        break;
    case TARGET_TEXE:
        if(!entry_point_def) {
            out_msg("No entry point for .exe file", 0);
        }
        break;
    }
}

char write_exe_header(FILE *o, word entry_point, word image_size, word bss_size) {
    word exe_hdr[0x10];
    word bss_par;
    word block_count;
    word inlastblock;

    if(target != TARGET_TEXE) {
        return 1;
    }

    bss_par = (bss_size >> 4) + (bss_size & 0x0f ? 1 : 0);
    bss_par = 0xffff - (image_size + bss_size);
    bss_par = (bss_par >> 4) + (bss_par & 0x0f ? 1 : 0);

    inlastblock = image_size % 512;
    block_count = image_size / 512;
    if(inlastblock) {
        block_count++;
    }

    memset(exe_hdr, 0, sizeof(exe_hdr));
    exe_hdr[0x00] = 0x5a4d;
    exe_hdr[0x01] = inlastblock;
    exe_hdr[0x02] = block_count;
    exe_hdr[0x04] = 2;
    exe_hdr[0x05] = bss_par;
    exe_hdr[0x06] = bss_par;
    exe_hdr[0x08] = 0xfffe; /* Default SP */
    exe_hdr[0x0a] = entry_point; /* Entry point IP */

    return fwrite(exe_hdr, 1, sizeof(exe_hdr), o) == sizeof(exe_hdr);
}

void done(int code) {

    lex_done();
    if(code != 0) {
        remove(outname);
    }

    free(outprog);
    free(dataprog);
    expr_done();
    exit(code);
}

void help(int code) {
    printf("%s assembler Version %d.%d (build %d)\nCopyright(C) 2000, 2001, 2019 Robert Ostling\nCopyright(C) 2019-2026 DosWorld\nMIT License https://opensource.org/licenses/MIT\n\n",PROG_NAME,MAIN_VERSION,SUB_VERSION,BUILD);
    printf("%s file.asm -o file.com [-options]\n\n"
           "options:\n"
           "\t-s xxxx     set starting point to xxxx (default 0x100)\n"
           "\t-m x        set error/waning level (default 2)\n"
           "\t-f xxx      set output format bin, com, texe, rdf (default com)\n"
           "\t-dCONST=VAL set assign VAL to CONST\n\n"
           "Error/Warning levels:\n\n"
           "\t0\tErrors only\n"
           "\t1\tErrors and serious warnings\n"
           "\t2\tAll\n\n", PROG_NAME);
    done(code);
}

static void free_relocs(void) {
    t_reloc *r;
    while(relocs != NULL) {
        r = relocs->next;
        free(relocs);
        relocs = r;
    }
}

int main(int argc, char* argv[]) {
    int i, j, assembleResult;
    char c, *p;
    char tmp[256];

    if(argc < 2) {
        help(1);
    }

    expr_init();

    target = TARGET_UNDEF;
    outname[0] = 0;
    linenr = 0;
    outprog = (byte *)MSA_MALLOC(65000);
    dataprog = (byte *)MSA_MALLOC(65000);
    outptr = org = 0;
    is_org_def = 0;

    for(i = 1; i < argc; i++) {
        c = argv[i][0];
        if((c == '-' || c == '/') && (i + 1 < argc)) {
            switch(toupper(argv[i][1])) {
            case 'F':
                if(!strcasecmp(argv[i + 1],"bin")) {
                    target = TARGET_BIN;
                    i++;
                } else if(!strcasecmp(argv[i + 1],"com")) {
                    target = TARGET_COM;
                    entry_point = outptr = org = 0x100;
                    entry_point_def = 1;
                    is_org_def = 1;
                    i++;
                } else if(!strcasecmp(argv[i + 1],"texe")) {
                    target = TARGET_TEXE;
                    entry_point = outptr = org = 0;
                    entry_point_def = 0;
                    is_org_def = 1;
                    i++;
                } else if(!strcasecmp(argv[i + 1],"rdf")) {
                    target = TARGET_RDF;
                    entry_point = outptr = org = 0;
                    entry_point_def = 0;
                    is_org_def = 1;
                    i++;
                } else {
                    help(1);
                }
                break;
            case 'O':
                strcpy(outname, argv[i + 1]);
                i++;
                break;
            case 'S':
                org = get_const(argv[i + 1]);
                i++;
                break;
            case 'M':
                quiet = get_const(argv[i + 1]);
                i++;
                break;
            default:
                help(1);
            }
        } else if((c == '-' || c == '/')) {
            switch(toupper(argv[i][1])) {
            case 'D':
                p = argv[i] + 2;
                j = 0;
                while((*p) && (*p != '=') && (j < (sizeof(tmp) - 1))) {
                    tmp[j] = *p;
                    p++;
                    j++;
                }
                tmp[j] = 0;
                if(*p != '=') help(1);
                strupr(tmp);
                add_const(tmp, CONST_EXPR, get_const(p));
                break;
            default:
                help(1);
            }
        } else {
            if(inputname != NULL) {
                help(1);
            }
            inputname = argv[i];
        }
    }

    if(outname[0] == 0) {
        out_msg("No output file.", 0);
        done(1);
    }

    if(target == TARGET_UNDEF) {
        target = TARGET_COM;
        outptr = org = 0x100;
        entry_point = 0x100;
        entry_point_def = 1;
        is_org_def = 1;
    }

    switch(target) {
    case TARGET_COM:
        outptr = org = 0x0100;
        break;
    case TARGET_TEXE:
    case TARGET_RDF:
        outptr = org = 0x0000;
        break;
    }

    lex_init();
    bss_size = 0;
    assembleResult = 1;
    code_size = 0;

    add_const("$", CONST_EXPR, outptr);
    add_const("$$", CONST_EXPR, org);

    for(pass = 0; pass < passes && assembleResult; pass++) {
        if((outfile = fopen(outname,"wb"))==0) {
            out_msg("Can't open output file.", 0);
            done(2);
        }
        check_entry_point();
        write_exe_header(outfile, entry_point, code_size, bss_size);
        outptr = org;
        dataptr = 0;
        bss_amount = 0;
        cur_section = SECTION_TEXT;
        free_relocs();
        errors = 0;
        warnings = 0;
        assembleResult = assemble(inputname);
        code_size = outptr - org;
        if(target == TARGET_RDF) {
            write_rdf(outfile);
        } else {
            fwrite(outprog + org, 1, code_size, outfile);
            fseek(outfile, 0, SEEK_SET);
            write_exe_header(outfile, entry_point, code_size, bss_size);
        }
        fclose(outfile);
    }

    if(errors > 0) {
        done(2);
    } else if(warnings > 0) {
        done(1);
    }
    done(0);
    return 0;
}

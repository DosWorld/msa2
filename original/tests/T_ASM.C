/*
 * End-to-end-ish tests that drive assemble() against a temporary .asm
 * file and inspect the resulting outprog/dataprog/relocs/constants/
 * bss_amount state. The RDF writer is *not* invoked here — that's
 * covered by test_rdf.c.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "TEST.H"
#include "../src/MSA2.H"
#include "../src/EXPR.H"
#include "../src/LEX.H"

extern int assemble(char *fname);

static const char *TMP_ASM = "TMPTEST.ASM";

static void reset_asm_state(int tgt) {
    extern byte *outprog;
    extern byte *dataprog;
    extern int target;
    extern int passes;
    extern char is_org_def;
    extern word org;
    extern char entry_point_def;
    extern char *inputname;
    extern int errors, warnings;

    if(outprog == NULL)  outprog = (byte *)calloc(65000, 1);
    if(dataprog == NULL) dataprog = (byte *)calloc(65000, 1);
    memset(outprog, 0, 65000);
    memset(dataprog, 0, 65000);
    target = tgt;
    passes = 2;
    is_org_def = 1;            /* skip org defaulting in assemble() */
    org = 0;
    entry_point_def = 0;
    inputname = (char *)TMP_ASM;
    errors = 0;
    warnings = 0;

    expr_done();
    expr_init();
    lex_done();
    lex_init();
    add_const("$",  CONST_EXPR, 0);
    add_const("$$", CONST_EXPR, 0);
}

static int write_asm(const char *body) {
    FILE *f = fopen(TMP_ASM, "wb");
    if(f == NULL) return 1;
    fputs(body, f);
    fclose(f);
    return 0;
}

static int assemble_twice(void) {
    extern int pass;
    extern word outptr;
    extern word org;
    extern word dataptr;
    extern dword bss_amount;
    extern int cur_section;
    extern t_reloc *relocs;

    /* Pass 0: collect symbol values. */
    pass = 0;
    cur_section = SECTION_TEXT;
    outptr = org;
    dataptr = 0;
    bss_amount = 0;
    if(assemble((char *)TMP_ASM) != 1) return 1;

    /* Pass 1: emit code/data and record relocations. */
    pass = 1;
    cur_section = SECTION_TEXT;
    outptr = org;
    dataptr = 0;
    bss_amount = 0;
    while(relocs != NULL) {
        t_reloc *n = relocs->next;
        free(relocs);
        relocs = n;
    }
    if(assemble((char *)TMP_ASM) != 1) return 1;
    return 0;
}

int test_asm_section_switching(void) {
    /* SECTION .data should redirect DB to dataprog. */
    extern byte *outprog;
    extern byte *dataprog;
    extern word outptr;
    extern word dataptr;

    reset_asm_state(TARGET_RDF);
    TEST_ASSERT_EQ_INT(0, write_asm(
        "    mov ax, 0x1234\n"
        "    ret\n"
        "section .data\n"
        "    db 0x55, 0xAA\n"));

    TEST_ASSERT_EQ_INT(0, assemble_twice());

    /* mov ax,imm16 = b8 34 12, ret = c3 → 4 code bytes. */
    TEST_ASSERT_EQ_INT(4, outptr);
    TEST_ASSERT_EQ_INT(0xb8, outprog[0]);
    TEST_ASSERT_EQ_INT(0x34, outprog[1]);
    TEST_ASSERT_EQ_INT(0x12, outprog[2]);
    TEST_ASSERT_EQ_INT(0xc3, outprog[3]);

    TEST_ASSERT_EQ_INT(2, dataptr);
    TEST_ASSERT_EQ_INT(0x55, dataprog[0]);
    TEST_ASSERT_EQ_INT(0xAA, dataprog[1]);

    remove(TMP_ASM);
    return 0;
}

int test_asm_export_marks_constant(void) {
    /* EXPORT must flip is_export on the named constant. */
    t_constant *c;
    reset_asm_state(TARGET_RDF);
    TEST_ASSERT_EQ_INT(0, write_asm(
        "EXPORT START\n"
        "START:\n"
        "    ret\n"));
    TEST_ASSERT_EQ_INT(0, assemble_twice());
    c = find_const("START");
    TEST_ASSERT(c != NULL);
    TEST_ASSERT_EQ_INT(1, c->is_export);
    TEST_ASSERT_EQ_INT(CONST_LABEL, c->type);
    remove(TMP_ASM);
    return 0;
}

int test_asm_rdf_data_reloc(void) {
    /* mov ax, msg where msg lives in .data must produce one reloc
     * with rseg=1 (data). */
    extern t_reloc *relocs;
    t_reloc *r;
    int count = 0;

    reset_asm_state(TARGET_RDF);
    TEST_ASSERT_EQ_INT(0, write_asm(
        "    mov ax, msg\n"
        "    ret\n"
        "section .data\n"
        "msg:\n"
        "    db \"hi\", 0\n"));
    TEST_ASSERT_EQ_INT(0, assemble_twice());

    for(r = relocs; r != NULL; r = r->next) count++;
    TEST_ASSERT_EQ_INT(1, count);
    TEST_ASSERT_EQ_INT(SECTION_TEXT, relocs->seg);
    TEST_ASSERT_EQ_INT(1, relocs->ofs);        /* disp word follows opcode b8 */
    TEST_ASSERT_EQ_INT(2, relocs->width);
    TEST_ASSERT_EQ_INT(1, relocs->rseg);       /* target is data */
    TEST_ASSERT_EQ_INT(0, relocs->relative);

    remove(TMP_ASM);
    return 0;
}

int test_asm_seg_of_in_mov(void) {
    /* `mov ax, seg msg` (with msg in .data) emits b8 00 00 plus one
     * SEGRELOC at code offset 1 with rseg=data. */
    extern byte *outprog;
    extern word outptr;
    extern t_reloc *relocs;
    t_reloc *r;
    int count = 0;

    reset_asm_state(TARGET_RDF);
    TEST_ASSERT_EQ_INT(0, write_asm(
        "    mov ax, seg msg\n"
        "    ret\n"
        "section .data\n"
        "msg:\n"
        "    db \"hi\", 0\n"));
    TEST_ASSERT_EQ_INT(0, assemble_twice());

    TEST_ASSERT_EQ_INT(4, outptr);            /* b8 00 00 + c3 */
    TEST_ASSERT_EQ_INT(0xb8, outprog[0]);
    TEST_ASSERT_EQ_INT(0x00, outprog[1]);     /* placeholder (linker fills) */
    TEST_ASSERT_EQ_INT(0x00, outprog[2]);
    TEST_ASSERT_EQ_INT(0xc3, outprog[3]);

    for(r = relocs; r != NULL; r = r->next) count++;
    TEST_ASSERT_EQ_INT(1, count);
    TEST_ASSERT_EQ_INT(1, relocs->is_segreloc);
    TEST_ASSERT_EQ_INT(SECTION_TEXT, relocs->seg);
    TEST_ASSERT_EQ_INT(1, relocs->ofs);       /* disp word follows opcode */
    TEST_ASSERT_EQ_INT(2, relocs->width);
    TEST_ASSERT_EQ_INT(1, relocs->rseg);      /* target is data */

    remove(TMP_ASM);
    return 0;
}

int test_asm_seg_of_in_dw(void) {
    /* `dw seg msg` inside .data: two zero bytes plus one SEGRELOC
     * targeting the data section. */
    extern byte *dataprog;
    extern word dataptr;
    extern t_reloc *relocs;
    t_reloc *r;
    int count = 0;

    reset_asm_state(TARGET_RDF);
    TEST_ASSERT_EQ_INT(0, write_asm(
        "    ret\n"
        "section .data\n"
        "msg:\n"
        "    db \"x\", 0\n"
        "ptr:\n"
        "    dw seg msg\n"));
    TEST_ASSERT_EQ_INT(0, assemble_twice());

    /* .data: 'x', 0, then two zero bytes for dw seg msg. */
    TEST_ASSERT_EQ_INT(4, dataptr);
    TEST_ASSERT_EQ_INT('x', dataprog[0]);
    TEST_ASSERT_EQ_INT(0,   dataprog[1]);
    TEST_ASSERT_EQ_INT(0,   dataprog[2]);
    TEST_ASSERT_EQ_INT(0,   dataprog[3]);

    for(r = relocs; r != NULL; r = r->next) count++;
    TEST_ASSERT_EQ_INT(1, count);
    TEST_ASSERT_EQ_INT(1, relocs->is_segreloc);
    TEST_ASSERT_EQ_INT(SECTION_DATA, relocs->seg);
    TEST_ASSERT_EQ_INT(2, relocs->ofs);       /* after 'x' + 0 */
    TEST_ASSERT_EQ_INT(2, relocs->width);
    TEST_ASSERT_EQ_INT(1, relocs->rseg);      /* msg is in data */

    remove(TMP_ASM);
    return 0;
}

int test_asm_encode_add(void) {
    /* Covers four ADD forms:
     *   add al, 5         -> 04 05         (ADD AL, imm8)
     *   add ax, 0x1234    -> 05 34 12      (ADD AX, imm16)
     *   add bx, cx        -> 03 d9         (ADD r16, r/m16)
     *   add al, bl        -> 02 c3         (ADD r8,  r/m8)
     */
    extern byte *outprog;
    extern word outptr;
    static const unsigned char expected[] = {
        0x04, 0x05,
        0x05, 0x34, 0x12,
        0x03, 0xD9,
        0x02, 0xC3
    };

    reset_asm_state(TARGET_BIN);
    TEST_ASSERT_EQ_INT(0, write_asm(
        "    add al, 5\n"
        "    add ax, 0x1234\n"
        "    add bx, cx\n"
        "    add al, bl\n"));
    TEST_ASSERT_EQ_INT(0, assemble_twice());

    TEST_ASSERT_EQ_INT(sizeof(expected), outptr);
    TEST_ASSERT_MEM_EQ(expected, outprog, sizeof(expected));

    remove(TMP_ASM);
    return 0;
}

int test_asm_encode_inc(void) {
    /* INC r16 uses the single-byte 40+r form; INC r8 uses the FE /0
     * group form.
     *   inc ax  -> 40
     *   inc bx  -> 43
     *   inc al  -> fe c0
     *   inc cl  -> fe c1
     */
    extern byte *outprog;
    extern word outptr;
    static const unsigned char expected[] = {
        0x40,
        0x43,
        0xFE, 0xC0,
        0xFE, 0xC1
    };

    reset_asm_state(TARGET_BIN);
    TEST_ASSERT_EQ_INT(0, write_asm(
        "    inc ax\n"
        "    inc bx\n"
        "    inc al\n"
        "    inc cl\n"));
    TEST_ASSERT_EQ_INT(0, assemble_twice());

    TEST_ASSERT_EQ_INT(sizeof(expected), outptr);
    TEST_ASSERT_MEM_EQ(expected, outprog, sizeof(expected));

    remove(TMP_ASM);
    return 0;
}

int test_asm_encode_jmp(void) {
    /* JMP SHORT uses rel8 (eb disp8); a backward JMP without SHORT uses
     * rel16 (e9 disp16). Displacement is measured from the byte after
     * the instruction.
     *
     *   0: start:  jmp short skip   -> eb 02      (skip at offset 4)
     *   2:         nop              -> 90
     *   3:         nop              -> 90
     *   4: skip:   jmp start        -> e9 f9 ff   (-7 little-endian)
     *   7:         ret              -> c3
     */
    extern byte *outprog;
    extern word outptr;
    static const unsigned char expected[] = {
        0xEB, 0x02,
        0x90,
        0x90,
        0xE9, 0xF9, 0xFF,
        0xC3
    };

    reset_asm_state(TARGET_BIN);
    TEST_ASSERT_EQ_INT(0, write_asm(
        "start:\n"
        "    jmp short skip\n"
        "    nop\n"
        "    nop\n"
        "skip:\n"
        "    jmp start\n"
        "    ret\n"));
    TEST_ASSERT_EQ_INT(0, assemble_twice());

    TEST_ASSERT_EQ_INT(sizeof(expected), outptr);
    TEST_ASSERT_MEM_EQ(expected, outprog, sizeof(expected));

    remove(TMP_ASM);
    return 0;
}

int test_asm_seg_rejected_for_non_rdf(void) {
    /* `seg <label>` is meaningful only at link time (RDF). For
     * bin/com/texe targets the assembler must emit a clear diagnostic
     * rather than silently treating SEGfoo as an undefined constant. */
    extern int errors;
    extern byte quiet;
    int saved_errors;
    byte saved_quiet;

    reset_asm_state(TARGET_BIN);
    TEST_ASSERT_EQ_INT(0, write_asm(
        "msg:\n"
        "    db \"hi\", 0\n"
        "    mov ax, seg msg\n"
        "    ret\n"));
    saved_errors = errors;
    saved_quiet  = quiet;
    quiet = 0;       /* suppress diagnostic output */
    TEST_ASSERT_EQ_INT(0, assemble_twice());
    TEST_ASSERT(errors > saved_errors);
    quiet = saved_quiet;

    remove(TMP_ASM);
    return 0;
}

int test_asm_bare_end(void) {
    /* `END` with no argument must stop assembly without producing an
     * error. (Previously called get_const("") and would warn.) */
    extern int errors;
    extern word outptr;

    reset_asm_state(TARGET_BIN);
    TEST_ASSERT_EQ_INT(0, write_asm(
        "    nop\n"
        "END\n"
        "    nop\n"));
    TEST_ASSERT_EQ_INT(0, assemble_twice());

    /* One nop emitted before END; the post-END nop must not be assembled. */
    TEST_ASSERT_EQ_INT(1, outptr);
    TEST_ASSERT_EQ_INT(0, errors);

    remove(TMP_ASM);
    return 0;
}

int test_asm_import_directive(void) {
    /* `import foo, bar` (RDF target) must register two CONST_IMPORT
     * entries with sequential ids starting at 3. */
    t_constant *c;

    reset_asm_state(TARGET_RDF);
    TEST_ASSERT_EQ_INT(0, write_asm(
        "import foo, bar\n"
        "    ret\n"));
    TEST_ASSERT_EQ_INT(0, assemble_twice());

    c = find_const("FOO");
    TEST_ASSERT(c != NULL);
    TEST_ASSERT_EQ_INT(CONST_IMPORT, CONST_TYPE(c));
    TEST_ASSERT_EQ_INT(3, c->value);

    c = find_const("BAR");
    TEST_ASSERT(c != NULL);
    TEST_ASSERT_EQ_INT(CONST_IMPORT, CONST_TYPE(c));
    TEST_ASSERT_EQ_INT(4, c->value);

    remove(TMP_ASM);
    return 0;
}

int test_asm_import_mov_imm16(void) {
    /* `mov ax, foo` where foo is imported → b8 00 00 + reloc
     * (width=2, rseg=import_id, relative=0). */
    extern byte *outprog;
    extern word outptr;
    extern t_reloc *relocs;
    t_reloc *r;
    int count = 0;

    reset_asm_state(TARGET_RDF);
    TEST_ASSERT_EQ_INT(0, write_asm(
        "import foo\n"
        "    mov ax, foo\n"
        "    ret\n"));
    TEST_ASSERT_EQ_INT(0, assemble_twice());

    TEST_ASSERT_EQ_INT(4, outptr);
    TEST_ASSERT_EQ_INT(0xb8, outprog[0]);
    TEST_ASSERT_EQ_INT(0x00, outprog[1]);
    TEST_ASSERT_EQ_INT(0x00, outprog[2]);
    TEST_ASSERT_EQ_INT(0xc3, outprog[3]);

    for(r = relocs; r != NULL; r = r->next) count++;
    TEST_ASSERT_EQ_INT(1, count);
    TEST_ASSERT_EQ_INT(0, relocs->is_segreloc);
    TEST_ASSERT_EQ_INT(SECTION_TEXT, relocs->seg);
    TEST_ASSERT_EQ_INT(1, relocs->ofs);
    TEST_ASSERT_EQ_INT(2, relocs->width);
    TEST_ASSERT_EQ_INT(3, relocs->rseg);     /* first import id */
    TEST_ASSERT_EQ_INT(0, relocs->relative);

    remove(TMP_ASM);
    return 0;
}

int test_asm_import_call_rel16(void) {
    /* `call foo` for imported foo → E8 + 2-byte addend, plus reloc with
     * relative=1 and rseg=import_id. Stored addend = -(patch_ofs + 2),
     * where patch_ofs is the position of the disp word (here = 1). */
    extern byte *outprog;
    extern word outptr;
    extern t_reloc *relocs;
    t_reloc *r;
    int count = 0;
    int addend;

    reset_asm_state(TARGET_RDF);
    TEST_ASSERT_EQ_INT(0, write_asm(
        "import foo\n"
        "    call foo\n"
        "    ret\n"));
    TEST_ASSERT_EQ_INT(0, assemble_twice());

    TEST_ASSERT_EQ_INT(4, outptr);
    TEST_ASSERT_EQ_INT(0xe8, outprog[0]);
    /* addend = -(1 + 2) = -3 → FD FF little-endian. */
    addend = (int)(int16_t)(outprog[1] | (outprog[2] << 8));
    TEST_ASSERT_EQ_INT(-3, addend);
    TEST_ASSERT_EQ_INT(0xc3, outprog[3]);

    for(r = relocs; r != NULL; r = r->next) count++;
    TEST_ASSERT_EQ_INT(1, count);
    TEST_ASSERT_EQ_INT(0, relocs->is_segreloc);
    TEST_ASSERT_EQ_INT(SECTION_TEXT, relocs->seg);
    TEST_ASSERT_EQ_INT(1, relocs->ofs);
    TEST_ASSERT_EQ_INT(2, relocs->width);
    TEST_ASSERT_EQ_INT(3, relocs->rseg);
    TEST_ASSERT_EQ_INT(1, relocs->relative);

    remove(TMP_ASM);
    return 0;
}

int test_asm_import_jmp_short_rejected(void) {
    /* `jmp short foo` to an import has no meaning (out of range across
     * modules). Assembler must emit an error. */
    extern int errors;
    extern byte quiet;
    int saved_errors;
    byte saved_quiet;

    reset_asm_state(TARGET_RDF);
    TEST_ASSERT_EQ_INT(0, write_asm(
        "import foo\n"
        "    jmp short foo\n"
        "    ret\n"));
    saved_errors = errors;
    saved_quiet  = quiet;
    quiet = 0;
    TEST_ASSERT_EQ_INT(0, assemble_twice());
    TEST_ASSERT(errors > saved_errors);
    quiet = saved_quiet;

    remove(TMP_ASM);
    return 0;
}

int test_asm_import_dw(void) {
    /* `dw foo` inside .data for imported foo → 2 zero bytes + reloc at
     * .data offset 0 with rseg=import_id. */
    extern byte *dataprog;
    extern word dataptr;
    extern t_reloc *relocs;
    t_reloc *r;
    int count = 0;

    reset_asm_state(TARGET_RDF);
    TEST_ASSERT_EQ_INT(0, write_asm(
        "import foo\n"
        "    ret\n"
        "section .data\n"
        "    dw foo\n"));
    TEST_ASSERT_EQ_INT(0, assemble_twice());

    TEST_ASSERT_EQ_INT(2, dataptr);
    TEST_ASSERT_EQ_INT(0, dataprog[0]);
    TEST_ASSERT_EQ_INT(0, dataprog[1]);

    for(r = relocs; r != NULL; r = r->next) count++;
    TEST_ASSERT_EQ_INT(1, count);
    TEST_ASSERT_EQ_INT(0, relocs->is_segreloc);
    TEST_ASSERT_EQ_INT(SECTION_DATA, relocs->seg);
    TEST_ASSERT_EQ_INT(0, relocs->ofs);
    TEST_ASSERT_EQ_INT(2, relocs->width);
    TEST_ASSERT_EQ_INT(3, relocs->rseg);
    TEST_ASSERT_EQ_INT(0, relocs->relative);

    remove(TMP_ASM);
    return 0;
}

int test_asm_import_disp16_mem(void) {
    /* `mov ax, [foo]` for imported foo. Picks the r16, r/m16 form:
     *   8B /r with mod=00, reg=AX=0, rm=110b → 8B 06 disp_lo disp_hi.
     * The disp word (offset 2..3) must hold 0 (addend) and produce one
     * reloc with rseg=import_id. */
    extern byte *outprog;
    extern word outptr;
    extern t_reloc *relocs;
    t_reloc *r;
    int count = 0;

    reset_asm_state(TARGET_RDF);
    TEST_ASSERT_EQ_INT(0, write_asm(
        "import foo\n"
        "    mov ax, [foo]\n"
        "    ret\n"));
    TEST_ASSERT_EQ_INT(0, assemble_twice());

    TEST_ASSERT_EQ_INT(5, outptr);
    TEST_ASSERT_EQ_INT(0x8b, outprog[0]);
    TEST_ASSERT_EQ_INT(0x06, outprog[1]);
    TEST_ASSERT_EQ_INT(0x00, outprog[2]);
    TEST_ASSERT_EQ_INT(0x00, outprog[3]);
    TEST_ASSERT_EQ_INT(0xc3, outprog[4]);

    for(r = relocs; r != NULL; r = r->next) count++;
    TEST_ASSERT_EQ_INT(1, count);
    TEST_ASSERT_EQ_INT(0, relocs->is_segreloc);
    TEST_ASSERT_EQ_INT(SECTION_TEXT, relocs->seg);
    TEST_ASSERT_EQ_INT(2, relocs->ofs);       /* disp word follows 8B + ModRM */
    TEST_ASSERT_EQ_INT(2, relocs->width);
    TEST_ASSERT_EQ_INT(3, relocs->rseg);      /* import id */
    TEST_ASSERT_EQ_INT(0, relocs->relative);

    remove(TMP_ASM);
    return 0;
}

int test_asm_import_seg_of(void) {
    /* `mov ax, seg foo` for imported foo → b8 00 00 + SEGRELOC with
     * rseg=import_id. */
    extern byte *outprog;
    extern word outptr;
    extern t_reloc *relocs;
    t_reloc *r;
    int count = 0;

    reset_asm_state(TARGET_RDF);
    TEST_ASSERT_EQ_INT(0, write_asm(
        "import foo\n"
        "    mov ax, seg foo\n"
        "    ret\n"));
    TEST_ASSERT_EQ_INT(0, assemble_twice());

    TEST_ASSERT_EQ_INT(4, outptr);
    TEST_ASSERT_EQ_INT(0xb8, outprog[0]);
    TEST_ASSERT_EQ_INT(0x00, outprog[1]);
    TEST_ASSERT_EQ_INT(0x00, outprog[2]);
    TEST_ASSERT_EQ_INT(0xc3, outprog[3]);

    for(r = relocs; r != NULL; r = r->next) count++;
    TEST_ASSERT_EQ_INT(1, count);
    TEST_ASSERT_EQ_INT(1, relocs->is_segreloc);
    TEST_ASSERT_EQ_INT(SECTION_TEXT, relocs->seg);
    TEST_ASSERT_EQ_INT(1, relocs->ofs);
    TEST_ASSERT_EQ_INT(2, relocs->width);
    TEST_ASSERT_EQ_INT(3, relocs->rseg);     /* import id */

    remove(TMP_ASM);
    return 0;
}

int test_asm_import_rejected_for_non_rdf(void) {
    /* `import foo` is meaningful only for RDF. Other targets must error. */
    extern int errors;
    extern byte quiet;
    int saved_errors;
    byte saved_quiet;

    reset_asm_state(TARGET_BIN);
    TEST_ASSERT_EQ_INT(0, write_asm(
        "import foo\n"
        "    ret\n"));
    saved_errors = errors;
    saved_quiet  = quiet;
    quiet = 0;
    TEST_ASSERT_EQ_INT(0, assemble_twice());
    TEST_ASSERT(errors > saved_errors);
    quiet = saved_quiet;

    remove(TMP_ASM);
    return 0;
}

int test_asm_bss_resb_resw(void) {
    /* RESB / RESW in .bss must advance bss_amount without touching
     * outprog or dataprog. */
    extern dword bss_amount;
    extern word outptr;
    extern word dataptr;
    t_constant *c;

    reset_asm_state(TARGET_RDF);
    TEST_ASSERT_EQ_INT(0, write_asm(
        "    ret\n"
        "section .bss\n"
        "var1:\n"
        "    resw 1\n"
        "var2:\n"
        "    resb 64\n"));
    TEST_ASSERT_EQ_INT(0, assemble_twice());

    TEST_ASSERT_EQ_INT(1, outptr);        /* just the ret */
    TEST_ASSERT_EQ_INT(0, dataptr);
    TEST_ASSERT_EQ_INT(2 + 64, bss_amount);

    c = find_const("VAR1");
    TEST_ASSERT(c != NULL);
    TEST_ASSERT_EQ_INT(CONST_BSS, c->type);
    TEST_ASSERT_EQ_INT(0, c->value);

    c = find_const("VAR2");
    TEST_ASSERT(c != NULL);
    TEST_ASSERT_EQ_INT(CONST_BSS, c->type);
    TEST_ASSERT_EQ_INT(2, c->value);

    remove(TMP_ASM);
    return 0;
}

int test_asm_jmp_far_direct_import(void) {
    /* `jmp far foo` for imported foo (RDF target):
     *   ea 00 00 00 00  + RELOC(width=2, rseg=import_id, abs)
     *                   + SEGRELOC(rseg=import_id)
     * Both relocs target the same rseg (the import id). */
    extern byte *outprog;
    extern word outptr;
    extern t_reloc *relocs;
    t_reloc *r;
    int count = 0;
    int saw_reloc = 0, saw_segreloc = 0;

    reset_asm_state(TARGET_RDF);
    TEST_ASSERT_EQ_INT(0, write_asm(
        "import foo\n"
        "    jmp far foo\n"
        "    ret\n"));
    TEST_ASSERT_EQ_INT(0, assemble_twice());

    TEST_ASSERT_EQ_INT(6, outptr);
    TEST_ASSERT_EQ_INT(0xea, outprog[0]);
    TEST_ASSERT_EQ_INT(0x00, outprog[1]);
    TEST_ASSERT_EQ_INT(0x00, outprog[2]);
    TEST_ASSERT_EQ_INT(0x00, outprog[3]);
    TEST_ASSERT_EQ_INT(0x00, outprog[4]);
    TEST_ASSERT_EQ_INT(0xc3, outprog[5]);

    for(r = relocs; r != NULL; r = r->next) {
        count++;
        if(r->is_segreloc) {
            saw_segreloc = 1;
            TEST_ASSERT_EQ_INT(SECTION_TEXT, r->seg);
            TEST_ASSERT_EQ_INT(3, r->ofs);     /* segment16 at offset 3 */
            TEST_ASSERT_EQ_INT(2, r->width);
            TEST_ASSERT_EQ_INT(3, r->rseg);    /* import id */
        } else {
            saw_reloc = 1;
            TEST_ASSERT_EQ_INT(SECTION_TEXT, r->seg);
            TEST_ASSERT_EQ_INT(1, r->ofs);     /* offset16 at offset 1 */
            TEST_ASSERT_EQ_INT(2, r->width);
            TEST_ASSERT_EQ_INT(3, r->rseg);    /* import id */
            TEST_ASSERT_EQ_INT(0, r->relative);
        }
    }
    TEST_ASSERT_EQ_INT(2, count);
    TEST_ASSERT_EQ_INT(1, saw_reloc);
    TEST_ASSERT_EQ_INT(1, saw_segreloc);

    remove(TMP_ASM);
    return 0;
}

int test_asm_call_far_direct_local(void) {
    /* `call far foo` for a local label in .text (RDF target):
     *   9a 00 00 00 00  + RELOC(width=2, rseg=text=0, abs)
     *                   + SEGRELOC(rseg=text=0)
     * The offset reloc points to foo's offset; the segment reloc carries
     * the .text paragraph base — both linker-resolved. */
    extern byte *outprog;
    extern word outptr;
    extern t_reloc *relocs;
    t_reloc *r;
    int count = 0;
    int saw_reloc = 0, saw_segreloc = 0;

    reset_asm_state(TARGET_RDF);
    TEST_ASSERT_EQ_INT(0, write_asm(
        "    call far foo\n"
        "foo:\n"
        "    ret\n"));
    TEST_ASSERT_EQ_INT(0, assemble_twice());

    TEST_ASSERT_EQ_INT(6, outptr);
    TEST_ASSERT_EQ_INT(0x9a, outprog[0]);
    TEST_ASSERT_EQ_INT(0xc3, outprog[5]);

    for(r = relocs; r != NULL; r = r->next) {
        count++;
        if(r->is_segreloc) {
            saw_segreloc = 1;
            TEST_ASSERT_EQ_INT(3, r->ofs);
            TEST_ASSERT_EQ_INT(0, r->rseg);    /* .text segid */
        } else {
            saw_reloc = 1;
            TEST_ASSERT_EQ_INT(1, r->ofs);
            TEST_ASSERT_EQ_INT(0, r->rseg);    /* .text segid */
            TEST_ASSERT_EQ_INT(0, r->relative);
        }
    }
    TEST_ASSERT_EQ_INT(2, count);
    TEST_ASSERT_EQ_INT(1, saw_reloc);
    TEST_ASSERT_EQ_INT(1, saw_segreloc);

    remove(TMP_ASM);
    return 0;
}

int test_asm_jmp_far_direct_rejected_for_non_rdf(void) {
    /* Direct far jmp/call (immediate symbol form) can only be resolved
     * by the RDF linker. For bin/com/texe targets it must error. */
    extern int errors;
    extern byte quiet;
    int saved_errors;
    byte saved_quiet;

    reset_asm_state(TARGET_BIN);
    TEST_ASSERT_EQ_INT(0, write_asm(
        "foo:\n"
        "    jmp far foo\n"
        "    ret\n"));
    saved_errors = errors;
    saved_quiet  = quiet;
    quiet = 0;
    TEST_ASSERT_EQ_INT(0, assemble_twice());
    TEST_ASSERT(errors > saved_errors);
    quiet = saved_quiet;

    remove(TMP_ASM);
    return 0;
}

int test_asm_jmp_far_indirect_mem(void) {
    /* `jmp far [foo]` is the FF /5 form (m16:16 indirect). The CPU
     * fetches a 4-byte offset:segment pair from memory and jumps to it.
     * ModRM byte: mod=00, reg=5 (sub-opcode), rm=110b → 0x2E.
     * For a local data label foo at .data offset 0, the disp word holds
     * 0 with an RDF RELOC pointing at the .data segment.
     *
     *   FF 2E 00 00      jmp far [foo]
     *   C3               ret
     */
    extern byte *outprog;
    extern word outptr;
    extern t_reloc *relocs;
    t_reloc *r;
    int count = 0;

    reset_asm_state(TARGET_RDF);
    TEST_ASSERT_EQ_INT(0, write_asm(
        "    jmp far [foo]\n"
        "    ret\n"
        "section .data\n"
        "foo:\n"
        "    dd 0\n"));
    TEST_ASSERT_EQ_INT(0, assemble_twice());

    TEST_ASSERT_EQ_INT(5, outptr);
    TEST_ASSERT_EQ_INT(0xff, outprog[0]);
    TEST_ASSERT_EQ_INT(0x2e, outprog[1]);    /* mod=00, reg=5, rm=110 */
    TEST_ASSERT_EQ_INT(0x00, outprog[2]);
    TEST_ASSERT_EQ_INT(0x00, outprog[3]);
    TEST_ASSERT_EQ_INT(0xc3, outprog[4]);

    for(r = relocs; r != NULL; r = r->next) count++;
    TEST_ASSERT_EQ_INT(1, count);
    TEST_ASSERT_EQ_INT(0, relocs->is_segreloc);
    TEST_ASSERT_EQ_INT(SECTION_TEXT, relocs->seg);
    TEST_ASSERT_EQ_INT(2, relocs->ofs);      /* disp word follows FF + modrm */
    TEST_ASSERT_EQ_INT(2, relocs->width);
    TEST_ASSERT_EQ_INT(1, relocs->rseg);     /* .data segid */
    TEST_ASSERT_EQ_INT(0, relocs->relative);

    remove(TMP_ASM);
    return 0;
}

int test_asm_call_far_indirect_mem(void) {
    /* `call far [foo]` is FF /3 — same modrm layout as jmp far indirect
     * but reg field is 3. For .data label foo at offset 0:
     *
     *   FF 1E 00 00      call far [foo]   (mod=00, reg=3, rm=110)
     *   C3               ret
     */
    extern byte *outprog;
    extern word outptr;

    reset_asm_state(TARGET_RDF);
    TEST_ASSERT_EQ_INT(0, write_asm(
        "    call far [foo]\n"
        "    ret\n"
        "section .data\n"
        "foo:\n"
        "    dd 0\n"));
    TEST_ASSERT_EQ_INT(0, assemble_twice());

    TEST_ASSERT_EQ_INT(5, outptr);
    TEST_ASSERT_EQ_INT(0xff, outprog[0]);
    TEST_ASSERT_EQ_INT(0x1e, outprog[1]);    /* mod=00, reg=3, rm=110 */
    TEST_ASSERT_EQ_INT(0xc3, outprog[4]);

    remove(TMP_ASM);
    return 0;
}

int test_asm_local_label_two_procs(void) {
    /* Each proc has its own .loop; both should resolve to its own
     * qualified form (PROC1.LOOP / PROC2.LOOP). No collision.
     *
     *   0: proc1: mov cx, 1
     *   3: .loop: loop .loop      -> e2 fe       (-2 to .loop at 3)
     *   5:        ret
     *   6: proc2: mov cx, 2
     *   9: .loop: loop .loop      -> e2 fe       (-2 to .loop at 9)
     *  11:        ret
     */
    extern byte *outprog;
    extern word outptr;
    static const unsigned char expected[] = {
        0xB9, 0x01, 0x00,
        0xE2, 0xFE,
        0xC3,
        0xB9, 0x02, 0x00,
        0xE2, 0xFE,
        0xC3
    };

    reset_asm_state(TARGET_BIN);
    TEST_ASSERT_EQ_INT(0, write_asm(
        "proc1:\n"
        "    mov cx, 1\n"
        ".loop:\n"
        "    loop .loop\n"
        "    ret\n"
        "proc2:\n"
        "    mov cx, 2\n"
        ".loop:\n"
        "    loop .loop\n"
        "    ret\n"));
    TEST_ASSERT_EQ_INT(0, assemble_twice());

    TEST_ASSERT_EQ_INT(sizeof(expected), outptr);
    TEST_ASSERT_MEM_EQ(expected, outprog, sizeof(expected));

    /* Both qualified forms exist and have distinct offsets. */
    {
        t_constant *l1 = find_const("PROC1.LOOP");
        t_constant *l2 = find_const("PROC2.LOOP");
        TEST_ASSERT(l1 != NULL);
        TEST_ASSERT(l2 != NULL);
        TEST_ASSERT_EQ_INT(3, l1->value);
        TEST_ASSERT_EQ_INT(9, l2->value);
    }

    remove(TMP_ASM);
    return 0;
}

int test_asm_local_label_no_parent(void) {
    /* A '.local:' before any non-local label has no parent. The
     * assembler must emit a diagnostic on pass 1 (the message is gated
     * on pass to avoid double-printing during the symbol-collect pass). */
    extern int errors;
    extern byte quiet;
    int saved_errors;
    byte saved_quiet;

    reset_asm_state(TARGET_BIN);
    TEST_ASSERT_EQ_INT(0, write_asm(
        ".foo:\n"
        "    ret\n"));
    saved_errors = errors;
    saved_quiet  = quiet;
    quiet = 0;
    TEST_ASSERT_EQ_INT(0, assemble_twice());
    TEST_ASSERT(errors > saved_errors);
    quiet = saved_quiet;

    remove(TMP_ASM);
    return 0;
}

int test_asm_local_label_section_resets_parent(void) {
    /* SECTION switch resets last_global. A '.x' in .data after
     * 'msg:' in .text must NOT bind to 'msg' as its parent. */
    extern int errors;
    extern byte quiet;
    int saved_errors;
    byte saved_quiet;

    reset_asm_state(TARGET_RDF);
    TEST_ASSERT_EQ_INT(0, write_asm(
        "msg:\n"
        "    ret\n"
        "section .data\n"
        ".x:\n"
        "    db 0\n"));
    saved_errors = errors;
    saved_quiet  = quiet;
    quiet = 0;
    TEST_ASSERT_EQ_INT(0, assemble_twice());
    TEST_ASSERT(errors > saved_errors);
    quiet = saved_quiet;

    remove(TMP_ASM);
    return 0;
}

int test_asm_export_local_rejected(void) {
    /* EXPORT of a name starting with '.' has no defined meaning
     * (locals are scoped to a parent). Must error. */
    extern int errors;
    extern byte quiet;
    int saved_errors;
    byte saved_quiet;

    reset_asm_state(TARGET_RDF);
    TEST_ASSERT_EQ_INT(0, write_asm(
        "proc:\n"
        ".inner:\n"
        "    ret\n"
        "EXPORT .inner\n"));
    saved_errors = errors;
    saved_quiet  = quiet;
    quiet = 0;
    TEST_ASSERT_EQ_INT(0, assemble_twice());
    TEST_ASSERT(errors > saved_errors);
    quiet = saved_quiet;

    remove(TMP_ASM);
    return 0;
}

int test_asm_local_label_disp_reloc(void) {
    /* mov ax, [.tbl] where .tbl is a .data local: the disp word
     * gets a RELOC targeting .data with the qualified label resolved. */
    extern byte *outprog;
    extern word outptr;
    extern t_reloc *relocs;
    t_reloc *r;
    int count = 0;

    reset_asm_state(TARGET_RDF);
    TEST_ASSERT_EQ_INT(0, write_asm(
        "proc:\n"
        "    mov ax, [.tbl]\n"
        "    ret\n"
        "section .data\n"
        "proc:\n"
        ".tbl:\n"
        "    dw 0\n"));
    TEST_ASSERT_EQ_INT(0, assemble_twice());

    TEST_ASSERT_EQ_INT(0x8b, outprog[0]);
    TEST_ASSERT_EQ_INT(0x06, outprog[1]);   /* mod=00 reg=AX rm=110 */

    for(r = relocs; r != NULL; r = r->next) count++;
    TEST_ASSERT_EQ_INT(1, count);
    TEST_ASSERT_EQ_INT(SECTION_TEXT, relocs->seg);
    TEST_ASSERT_EQ_INT(2, relocs->ofs);
    TEST_ASSERT_EQ_INT(1, relocs->rseg);    /* .data */

    /* The qualified form lives in the symbol table; the raw '.tbl' does not. */
    TEST_ASSERT(find_const("PROC.TBL") != NULL);
    TEST_ASSERT(find_const(".TBL") == NULL);

    remove(TMP_ASM);
    return 0;
}

int test_asm_define_numeric(void) {
    /* %define NAME numeric-expr  -> usable as a constant in operands. */
    extern byte *outprog;
    extern word outptr;
    static const unsigned char expected[] = { 0xB8, 0x00, 0xB8, 0xC3 };

    reset_asm_state(TARGET_BIN);
    TEST_ASSERT_EQ_INT(0, write_asm(
        "%define VIDSEG 0xB800\n"
        "    mov ax, VIDSEG\n"
        "    ret\n"));
    TEST_ASSERT_EQ_INT(0, assemble_twice());

    TEST_ASSERT_EQ_INT(sizeof(expected), outptr);
    TEST_ASSERT_MEM_EQ(expected, outprog, sizeof(expected));
    return 0;
}

int test_asm_define_expression(void) {
    /* %define body is re-evaluated at each use, so a forward reference
     * to another %define works as long as it resolves at lookup time. */
    extern byte *outprog;
    extern word outptr;

    reset_asm_state(TARGET_BIN);
    TEST_ASSERT_EQ_INT(0, write_asm(
        "%define BASE 0x1000\n"
        "%define OFS  BASE+0x10\n"
        "    mov ax, OFS\n"
        "    ret\n"));
    TEST_ASSERT_EQ_INT(0, assemble_twice());

    TEST_ASSERT_EQ_INT(4, outptr);
    TEST_ASSERT_EQ_INT(0xB8, outprog[0]);
    TEST_ASSERT_EQ_INT(0x10, outprog[1]);
    TEST_ASSERT_EQ_INT(0x10, outprog[2]);
    TEST_ASSERT_EQ_INT(0xC3, outprog[3]);
    return 0;
}

int test_asm_undef(void) {
    /* %undef removes the name; subsequent use produces an "Undefined
     * constant" warning (severity 1, increments warnings not errors). */
    extern int warnings;
    extern byte quiet;
    int saved_warnings;
    byte saved_quiet;

    reset_asm_state(TARGET_BIN);
    TEST_ASSERT_EQ_INT(0, write_asm(
        "%define VIDSEG 0xB800\n"
        "%undef VIDSEG\n"
        "    mov ax, VIDSEG\n"
        "    ret\n"));
    saved_warnings = warnings;
    saved_quiet  = quiet;
    quiet = 0;
    TEST_ASSERT_EQ_INT(0, assemble_twice());
    TEST_ASSERT(warnings > saved_warnings);
    quiet = saved_quiet;
    return 0;
}

int test_asm_define_cycle_guard(void) {
    /* A %define body that names itself (or chains into a cycle) used
     * to recurse through get_const() until the host stack was
     * exhausted -- fatal on DOS where the stack is ~4 KB. P3 added a
     * 64-deep recursion guard. Verify the guard fires cleanly:
     * %define A A produces an error rather than crashing. */
    extern int errors;
    extern byte quiet;
    int saved_errors;
    byte saved_quiet;

    reset_asm_state(TARGET_BIN);
    TEST_ASSERT_EQ_INT(0, write_asm(
        "%define A A\n"
        "    mov ax, A\n"
        "    ret\n"));
    saved_errors = errors;
    saved_quiet  = quiet;
    quiet = 0;
    TEST_ASSERT_EQ_INT(0, assemble_twice());
    TEST_ASSERT(errors > saved_errors);
    quiet = saved_quiet;
    return 0;
}

int test_asm_define_mutual_cycle_guard(void) {
    /* Mutual %define cycle (A -> B -> A) -- also caught by the same
     * recursion guard. */
    extern int errors;
    extern byte quiet;
    int saved_errors;
    byte saved_quiet;

    reset_asm_state(TARGET_BIN);
    TEST_ASSERT_EQ_INT(0, write_asm(
        "%define A B\n"
        "%define B A\n"
        "    mov ax, A\n"
        "    ret\n"));
    saved_errors = errors;
    saved_quiet  = quiet;
    quiet = 0;
    TEST_ASSERT_EQ_INT(0, assemble_twice());
    TEST_ASSERT(errors > saved_errors);
    quiet = saved_quiet;
    return 0;
}

int test_asm_rel_offset_no_overflow_16bit(void) {
    /* Regression: on a 16-bit host, jump-distance math used 'int' (16-bit
     * signed) on both sides, so target labels >= 0x8000 with org >= 0x8000
     * could compute a bogus signed difference and trigger spurious
     * "Too long jump" errors. The fix promotes to int32_t.
     *
     * Drive it from a high org so outptr lives in the >= 0x8000 range.
     *   org = 0x9000
     *   0x9000: jmp far_target   -> e9 03 00   (rel16 disp = 3)
     *   0x9003: nop
     *   0x9004: nop
     *   0x9005: nop
     *   0x9006: far_target: ret
     */
    extern byte *outprog;
    extern word outptr;
    extern word org;
    extern char is_org_def;
    extern int errors;

    reset_asm_state(TARGET_BIN);
    org = 0x9000;
    is_org_def = 1;
    TEST_ASSERT_EQ_INT(0, write_asm(
        "    jmp far_target\n"
        "    nop\n"
        "    nop\n"
        "    nop\n"
        "far_target:\n"
        "    ret\n"));
    TEST_ASSERT_EQ_INT(0, assemble_twice());
    TEST_ASSERT_EQ_INT(0, errors);

    /* Code starts at outprog[org] in this driver model — but the test
     * harness writes to outprog[0..]; outptr begins at org and walks up.
     * The bytes written start at outprog[org]. */
    TEST_ASSERT_EQ_INT(0x9007, outptr);
    TEST_ASSERT_EQ_INT(0xE9, outprog[0x9000]);
    TEST_ASSERT_EQ_INT(0x03, outprog[0x9001]);
    TEST_ASSERT_EQ_INT(0x00, outprog[0x9002]);
    return 0;
}

int test_asm_label_value_high_16bit(void) {
    /* Regression for the add_const(int value) signature bug: on a
     * 16-bit host, label values >= 0x8000 would narrow through 'int'
     * and sign-extend when assigned to c->value (int32_t). Verify
     * the symbol table round-trips a high address verbatim. */
    extern word org;
    extern char is_org_def;
    t_constant *c;

    reset_asm_state(TARGET_BIN);
    org = 0x9000;
    is_org_def = 1;
    TEST_ASSERT_EQ_INT(0, write_asm(
        "    nop\n"
        "foo:\n"
        "    ret\n"));
    TEST_ASSERT_EQ_INT(0, assemble_twice());

    c = find_const("FOO");
    TEST_ASSERT(c != NULL);
    TEST_ASSERT_EQ_INT(CONST_LABEL, CONST_TYPE(c));
    TEST_ASSERT_EQ_INT(0x9001, c->value);

    /* Verify $ is also captured at the full width. After the nop at
     * 0x9000, $ on the 'foo:' line should be 0x9001. */
    {
        t_constant *dollar = find_const("$");
        TEST_ASSERT(dollar != NULL);
        /* $ is updated per line; final value reflects the last line
         * processed. After 'ret' at 0x9001, $ is 0x9002 (end of file
         * with no further line). Whatever value it holds, it must NOT
         * be negative — that's the bug signature. */
        TEST_ASSERT(dollar->value >= 0);
    }
    return 0;
}

int test_asm_macro_no_args(void) {
    /* %macro with argc=0, invoked twice. The assembler's table picks
     * the FF /6 group form for 'push <r16>' before the 0x50+r form, so
     * each push ax / push bx emits 2 bytes (FF F0 / FF F3). Two
     * invocations + ret = 9 bytes. */
    extern byte *outprog;
    extern word outptr;
    static const unsigned char expected[] = {
        0xFF, 0xF0, 0xFF, 0xF3,
        0xFF, 0xF0, 0xFF, 0xF3,
        0xC3
    };

    reset_asm_state(TARGET_BIN);
    TEST_ASSERT_EQ_INT(0, write_asm(
        "%macro PUSHALL 0\n"
        "    push ax\n"
        "    push bx\n"
        "%endmacro\n"
        "    PUSHALL\n"
        "    PUSHALL\n"
        "    ret\n"));
    TEST_ASSERT_EQ_INT(0, assemble_twice());

    TEST_ASSERT_EQ_INT(sizeof(expected), outptr);
    TEST_ASSERT_MEM_EQ(expected, outprog, sizeof(expected));
    return 0;
}

int test_asm_macro_one_arg(void) {
    /* %macro DELAY 1 with body 'mov cx, %1'. Two invocations with
     * different args produce different mov immediates. */
    extern byte *outprog;
    extern word outptr;
    static const unsigned char expected[] = {
        0xB9, 0x0A, 0x00,
        0xB9, 0x14, 0x00,
        0xC3
    };

    reset_asm_state(TARGET_BIN);
    TEST_ASSERT_EQ_INT(0, write_asm(
        "%macro DELAY 1\n"
        "    mov cx, %1\n"
        "%endmacro\n"
        "    DELAY 10\n"
        "    DELAY 20\n"
        "    ret\n"));
    TEST_ASSERT_EQ_INT(0, assemble_twice());

    TEST_ASSERT_EQ_INT(sizeof(expected), outptr);
    TEST_ASSERT_MEM_EQ(expected, outprog, sizeof(expected));
    return 0;
}

int test_asm_macro_local_label(void) {
    /* %%w inside a macro body becomes a per-invocation unique label.
     * Two invocations produce two distinct internal labels, each loop
     * resolving to its own .w.
     *
     *   %macro DELAY 1
     *      mov cx, %1
     *   %%w: loop %%w
     *   %endmacro
     *
     *   DELAY 1   -> b9 01 00 e2 fe
     *   DELAY 2   -> b9 02 00 e2 fe
     *   ret       -> c3
     */
    extern byte *outprog;
    extern word outptr;
    static const unsigned char expected[] = {
        0xB9, 0x01, 0x00, 0xE2, 0xFE,
        0xB9, 0x02, 0x00, 0xE2, 0xFE,
        0xC3
    };

    reset_asm_state(TARGET_BIN);
    TEST_ASSERT_EQ_INT(0, write_asm(
        "%macro DELAY 1\n"
        "    mov cx, %1\n"
        "%%w:\n"
        "    loop %%w\n"
        "%endmacro\n"
        "    DELAY 1\n"
        "    DELAY 2\n"
        "    ret\n"));
    TEST_ASSERT_EQ_INT(0, assemble_twice());

    TEST_ASSERT_EQ_INT(sizeof(expected), outptr);
    TEST_ASSERT_MEM_EQ(expected, outprog, sizeof(expected));
    return 0;
}

int test_asm_macro_argc_mismatch(void) {
    /* Invoking a 1-arg macro with 0 args must error. */
    extern int errors;
    extern byte quiet;
    int saved_errors;
    byte saved_quiet;

    reset_asm_state(TARGET_BIN);
    TEST_ASSERT_EQ_INT(0, write_asm(
        "%macro DELAY 1\n"
        "    mov cx, %1\n"
        "%endmacro\n"
        "    DELAY\n"
        "    ret\n"));
    saved_errors = errors;
    saved_quiet  = quiet;
    quiet = 0;
    TEST_ASSERT_EQ_INT(0, assemble_twice());
    TEST_ASSERT(errors > saved_errors);
    quiet = saved_quiet;
    return 0;
}

int test_asm_include_basic(void) {
    /* %include "file" reads file inline. A %define in the included
     * file is visible to the caller after the include line. */
    extern byte *outprog;
    extern word outptr;
    FILE *f;
    static const unsigned char expected[] = {
        0xB8, 0x34, 0x12,
        0xC3
    };
    const char *inc_path = "TMPINC.ASM";

    reset_asm_state(TARGET_BIN);

    f = fopen(inc_path, "wb");
    TEST_ASSERT(f != NULL);
    fputs("%define K 0x1234\n", f);
    fclose(f);

    TEST_ASSERT_EQ_INT(0, write_asm(
        "%include \"TMPINC.ASM\"\n"
        "    mov ax, K\n"
        "    ret\n"));
    TEST_ASSERT_EQ_INT(0, assemble_twice());

    TEST_ASSERT_EQ_INT(sizeof(expected), outptr);
    TEST_ASSERT_MEM_EQ(expected, outprog, sizeof(expected));

    remove(inc_path);
    remove(TMP_ASM);
    return 0;
}

int test_asm_include_unquoted(void) {
    /* Path may be given without quotes; the token runs to end of
     * line (whitespace already collapsed by strip). */
    extern byte *outprog;
    extern word outptr;
    FILE *f;
    const char *inc_path = "TMPINC.ASM";

    reset_asm_state(TARGET_BIN);

    f = fopen(inc_path, "wb");
    TEST_ASSERT(f != NULL);
    fputs("    nop\n", f);
    fclose(f);

    TEST_ASSERT_EQ_INT(0, write_asm(
        "%include TMPINC.ASM\n"
        "    ret\n"));
    TEST_ASSERT_EQ_INT(0, assemble_twice());

    TEST_ASSERT_EQ_INT(2, outptr);
    TEST_ASSERT_EQ_INT(0x90, outprog[0]);
    TEST_ASSERT_EQ_INT(0xC3, outprog[1]);

    remove(inc_path);
    remove(TMP_ASM);
    return 0;
}

int test_asm_include_nested(void) {
    /* Three-level include chain. Each inner file contributes a nop;
     * the outermost emits ret last. */
    extern byte *outprog;
    extern word outptr;
    FILE *f;
    static const unsigned char expected[] = { 0x90, 0x90, 0x90, 0xC3 };

    reset_asm_state(TARGET_BIN);

    f = fopen("TMPI1.ASM", "wb");
    TEST_ASSERT(f != NULL);
    fputs("%include \"TMPI2.ASM\"\n    nop\n", f);
    fclose(f);

    f = fopen("TMPI2.ASM", "wb");
    TEST_ASSERT(f != NULL);
    fputs("%include \"TMPI3.ASM\"\n    nop\n", f);
    fclose(f);

    f = fopen("TMPI3.ASM", "wb");
    TEST_ASSERT(f != NULL);
    fputs("    nop\n", f);
    fclose(f);

    TEST_ASSERT_EQ_INT(0, write_asm(
        "%include \"TMPI1.ASM\"\n"
        "    ret\n"));
    TEST_ASSERT_EQ_INT(0, assemble_twice());

    TEST_ASSERT_EQ_INT(sizeof(expected), outptr);
    TEST_ASSERT_MEM_EQ(expected, outprog, sizeof(expected));

    remove("TMPI1.ASM");
    remove("TMPI2.ASM");
    remove("TMPI3.ASM");
    remove(TMP_ASM);
    return 0;
}

int test_asm_include_missing_file(void) {
    /* Missing include file emits an error. */
    extern int errors;
    extern byte quiet;
    int saved_errors;
    byte saved_quiet;

    reset_asm_state(TARGET_BIN);
    TEST_ASSERT_EQ_INT(0, write_asm(
        "%include \"NOSUCH.ASM\"\n"
        "    ret\n"));
    saved_errors = errors;
    saved_quiet  = quiet;
    quiet = 0;
    TEST_ASSERT_EQ_INT(0, assemble_twice());
    TEST_ASSERT(errors > saved_errors);
    quiet = saved_quiet;

    remove(TMP_ASM);
    return 0;
}

int test_asm_include_too_deep(void) {
    /* A self-including file blows past the 8-deep cap and errors out
     * rather than recursing until malloc fails. */
    extern int errors;
    extern byte quiet;
    int saved_errors;
    byte saved_quiet;
    FILE *f;
    const char *self_inc = "TMPSELF.ASM";

    reset_asm_state(TARGET_BIN);

    /* TMPSELF.ASM includes itself unconditionally. */
    f = fopen(self_inc, "wb");
    TEST_ASSERT(f != NULL);
    fputs("%include \"TMPSELF.ASM\"\n    nop\n", f);
    fclose(f);

    TEST_ASSERT_EQ_INT(0, write_asm(
        "%include \"TMPSELF.ASM\"\n"
        "    ret\n"));
    saved_errors = errors;
    saved_quiet  = quiet;
    quiet = 0;
    TEST_ASSERT_EQ_INT(0, assemble_twice());
    TEST_ASSERT(errors > saved_errors);
    quiet = saved_quiet;

    remove(self_inc);
    remove(TMP_ASM);
    return 0;
}

int test_asm_include_error_attribution(void) {
    /* Diagnostic messages must point at the *included* file and the
     * line within it, not the parent. We can't easily intercept the
     * printf output from here, but we can inspect inputname/linenr at
     * the moment the diagnostic fires by triggering an undefined-
     * constant warning inside the included file and checking the
     * exposed globals right after assembly finishes.
     *
     * After assemble() returns, inputname is restored to the caller's
     * value -- so this test instead checks that errors were raised
     * (proving the included file was parsed) and that inputname has
     * been restored cleanly (no use-after-free on the outer pointer). */
    extern int warnings;
    extern byte quiet;
    extern char *inputname;
    int saved_warnings;
    byte saved_quiet;
    char *saved_inputname;
    FILE *f;

    reset_asm_state(TARGET_BIN);

    f = fopen("TMPINC.ASM", "wb");
    TEST_ASSERT(f != NULL);
    /* Line 1 of the include defines K. Line 2 references an undefined
     * symbol, which on pass 1 emits "Undefined constant 'NOPE'" as a
     * warning. The diagnostic must attribute it to TMPINC.ASM:2. */
    fputs("%define K 5\n    mov ax, NOPE\n", f);
    fclose(f);

    TEST_ASSERT_EQ_INT(0, write_asm(
        "%include \"TMPINC.ASM\"\n"
        "    mov bx, K\n"
        "    ret\n"));

    saved_warnings = warnings;
    saved_inputname = inputname;
    saved_quiet  = quiet;
    quiet = 0;
    TEST_ASSERT_EQ_INT(0, assemble_twice());
    TEST_ASSERT(warnings > saved_warnings);   /* the undef-warning fired */
    /* inputname must be restored to the caller's pointer (the outer
     * file path), not leak as a malloced include name. */
    TEST_ASSERT(inputname == saved_inputname);
    quiet = saved_quiet;

    remove("TMPINC.ASM");
    remove(TMP_ASM);
    return 0;
}

int test_asm_macro_no_arbitrary_caps(void) {
    /* Regression: storage was changed from fixed arrays to linked lists.
     * Declare more %defines and more %macros than the old hard caps
     * (256 each) and verify they all assemble. Also exercise a deep
     * macro body to confirm no cap on per-macro body length. */
    extern byte *outprog;
    extern word outptr;
    FILE *f;
    int i;
    int n = 300;   /* > old DEFINE_MAX/MACRO_MAX (256) */
    int body_lines = 64;  /* moderate body to exercise the per-macro list */

    reset_asm_state(TARGET_BIN);

    f = fopen(TMP_ASM, "wb");
    TEST_ASSERT(f != NULL);
    /* 300 %defines numbered D1..D300, each value = its index. */
    for(i = 1; i <= n; i++) {
        fprintf(f, "%%define D%d %d\n", i, i);
    }
    /* A macro with 64 body lines, each a 1-byte nop. */
    fprintf(f, "%%macro BIG 0\n");
    for(i = 0; i < body_lines; i++) {
        fprintf(f, "    nop\n");
    }
    fprintf(f, "%%endmacro\n");
    /* 300 zero-arg macros, each containing a single 'nop'. */
    for(i = 1; i <= n; i++) {
        fprintf(f, "%%macro M%d 0\n    nop\n%%endmacro\n", i);
    }
    /* Use a few of the late %defines so resolution must walk the list. */
    fprintf(f, "    mov ax, D250\n");
    fprintf(f, "    mov bx, D299\n");
    /* Invoke the big-body macro once and a late macro twice. */
    fprintf(f, "    BIG\n");
    fprintf(f, "    M299\n");
    fprintf(f, "    M300\n");
    fprintf(f, "    ret\n");
    fclose(f);

    TEST_ASSERT_EQ_INT(0, assemble_twice());

    /* mov ax, 250 = B8 FA 00 (3 bytes)
     * mov bx, 299 = BB 2B 01 (3 bytes)
     * BIG         = 64 * 0x90 (64 bytes)
     * M299        = 0x90 (1 byte)
     * M300        = 0x90 (1 byte)
     * ret         = 0xC3 (1 byte)
     * Total       = 73 bytes
     */
    TEST_ASSERT_EQ_INT(3 + 3 + body_lines + 1 + 1 + 1, outptr);
    TEST_ASSERT_EQ_INT(0xB8, outprog[0]);
    TEST_ASSERT_EQ_INT(0xFA, outprog[1]);
    TEST_ASSERT_EQ_INT(0x00, outprog[2]);
    TEST_ASSERT_EQ_INT(0xBB, outprog[3]);
    TEST_ASSERT_EQ_INT(0x2B, outprog[4]);
    TEST_ASSERT_EQ_INT(0x01, outprog[5]);
    /* The first byte of the BIG body should be 0x90. */
    TEST_ASSERT_EQ_INT(0x90, outprog[6]);
    /* And the very last code byte before ret should be a nop from M300. */
    TEST_ASSERT_EQ_INT(0x90, outprog[outptr - 2]);
    TEST_ASSERT_EQ_INT(0xC3, outprog[outptr - 1]);

    remove(TMP_ASM);
    return 0;
}

int test_asm_macro_nested_rejected(void) {
    /* A %macro inside another %macro body is rejected. */
    extern int errors;
    extern byte quiet;
    int saved_errors;
    byte saved_quiet;

    reset_asm_state(TARGET_BIN);
    TEST_ASSERT_EQ_INT(0, write_asm(
        "%macro OUTER 0\n"
        "%macro INNER 0\n"
        "    nop\n"
        "%endmacro\n"
        "%endmacro\n"
        "    ret\n"));
    saved_errors = errors;
    saved_quiet  = quiet;
    quiet = 0;
    TEST_ASSERT_EQ_INT(0, assemble_twice());
    TEST_ASSERT(errors > saved_errors);
    quiet = saved_quiet;
    return 0;
}

int test_asm_macro_recursion_guard(void) {
    /* A %macro that invokes itself would expand without bound,
     * exhausting heap. pp_macro_invoke counts in-flight invocations
     * via macro_save_head and caps at 64. On overflow it emits
     * "Macro recursion of 'M' too deep (cycle?)" and aborts the
     * invocation, so assembly terminates cleanly. */
    extern int errors;
    extern byte quiet;
    int saved_errors;
    byte saved_quiet;

    reset_asm_state(TARGET_BIN);
    TEST_ASSERT_EQ_INT(0, write_asm(
        "%macro M 0\n"
        "    nop\n"
        "    M\n"
        "%endmacro\n"
        "    M\n"
        "    ret\n"));
    saved_errors = errors;
    saved_quiet  = quiet;
    quiet = 0;
    TEST_ASSERT_EQ_INT(0, assemble_twice());
    TEST_ASSERT(errors > saved_errors);
    quiet = saved_quiet;
    return 0;
}

int test_asm_macro_mutual_recursion_guard(void) {
    /* Mutual macro recursion (A invokes B which invokes A) is the
     * same situation indirectly; the same 64-deep cap catches it. */
    extern int errors;
    extern byte quiet;
    int saved_errors;
    byte saved_quiet;

    reset_asm_state(TARGET_BIN);
    TEST_ASSERT_EQ_INT(0, write_asm(
        "%macro A 0\n"
        "    nop\n"
        "    B\n"
        "%endmacro\n"
        "%macro B 0\n"
        "    nop\n"
        "    A\n"
        "%endmacro\n"
        "    A\n"
        "    ret\n"));
    saved_errors = errors;
    saved_quiet  = quiet;
    quiet = 0;
    TEST_ASSERT_EQ_INT(0, assemble_twice());
    TEST_ASSERT(errors > saved_errors);
    quiet = saved_quiet;
    return 0;
}

int test_asm_jmp_far_reg_rejected(void) {
    /* `jmp far ax` (or any register) is not a real 8086 form — the far
     * indirect path requires a memory operand (m16:16). Must error. */
    extern int errors;
    extern byte quiet;
    int saved_errors;
    byte saved_quiet;

    reset_asm_state(TARGET_RDF);
    TEST_ASSERT_EQ_INT(0, write_asm(
        "    jmp far ax\n"
        "    ret\n"));
    saved_errors = errors;
    saved_quiet  = quiet;
    quiet = 0;
    TEST_ASSERT_EQ_INT(0, assemble_twice());
    TEST_ASSERT(errors > saved_errors);
    quiet = saved_quiet;

    remove(TMP_ASM);
    return 0;
}

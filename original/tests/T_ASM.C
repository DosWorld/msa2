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

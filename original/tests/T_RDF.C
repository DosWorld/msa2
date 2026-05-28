/*
 * Tests for the RDF writer (RDF.C). We populate the in-memory state
 * that write_rdf() reads (outprog/dataprog/relocs/constants/bss_amount/
 * code_size/target) and then capture the output via tmpfile().
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "TEST.H"
#include "../src/MSA2.H"
#include "../src/EXPR.H"
#include "../src/RDF.H"

/* Helpers ----------------------------------------------------------- */

static void reset_rdf_state(void) {
    /* outprog/dataprog allocated by main()/msa2_unused_main is not
     * called in tests, so allocate ourselves. Free at end of test. */
    extern byte *outprog;
    extern byte *dataprog;
    extern word outptr;
    extern word dataptr;
    extern dword bss_amount;
    extern int cur_section;
    extern t_reloc *relocs;
    extern int target;
    extern word code_size;
    extern int pass, passes;

    if(outprog == NULL)  outprog = (byte *)calloc(65000, 1);
    if(dataprog == NULL) dataprog = (byte *)calloc(65000, 1);
    memset(outprog, 0, 65000);
    memset(dataprog, 0, 65000);
    outptr = 0;
    dataptr = 0;
    bss_amount = 0;
    cur_section = SECTION_TEXT;
    code_size = 0;
    target = TARGET_RDF;
    passes = 2;
    pass = 1; /* last pass — needed for add_reloc() to record */

    /* Drop any reloc list from a previous test. */
    while(relocs != NULL) {
        t_reloc *n = relocs->next;
        free(relocs);
        relocs = n;
    }

    expr_done();
    expr_init();
}

static size_t capture_rdf(unsigned char *buf, size_t cap) {
    FILE *f;
    size_t n;
    f = tmpfile();
    if(f == NULL) return 0;
    write_rdf(f);
    fseek(f, 0, SEEK_END);
    n = (size_t)ftell(f);
    if(n > cap) n = cap;
    fseek(f, 0, SEEK_SET);
    fread(buf, 1, n, f);
    fclose(f);
    return n;
}

/* Tests ------------------------------------------------------------- */

int test_rdf_signature_and_header_size(void) {
    /* Empty module: only the signature, module/header size, and EOF
     * segment header. */
    unsigned char buf[64];
    size_t n;
    dword module_size, header_size;

    reset_rdf_state();
    n = capture_rdf(buf, sizeof(buf));
    TEST_ASSERT(n >= 14 + 10);  /* sig + sizes + EOF segment */

    TEST_ASSERT_MEM_EQ("RDOFF2", buf, 6);

    module_size = buf[6] | (buf[7] << 8) | (buf[8] << 16) | ((dword)buf[9] << 24);
    header_size = buf[10] | (buf[11] << 8) | (buf[12] << 16) | ((dword)buf[13] << 24);
    TEST_ASSERT_EQ_INT(n, module_size);
    TEST_ASSERT_EQ_INT(0, header_size);
    return 0;
}

int test_rdf_global_record(void) {
    /* One EXPORTed label in .text at offset 0x10 → RDFREC_GLOBAL. */
    unsigned char buf[128];
    t_constant *c;
    dword header_size;
    const unsigned char *rec;

    reset_rdf_state();
    c = add_const("MY_SYM", CONST_LABEL, 0x10);
    c->section = SECTION_TEXT;
    c->is_export = 1;

    (void)capture_rdf(buf, sizeof(buf));
    header_size = buf[10] | (buf[11] << 8) | (buf[12] << 16) | ((dword)buf[13] << 24);
    /* Payload = 1 flags + 1 segid + 4 offset + strlen+1. */
    TEST_ASSERT_EQ_INT(2 + 1 + 1 + 4 + 7, header_size);

    rec = buf + 14;
    TEST_ASSERT_EQ_INT(3,   rec[0]);  /* RDFREC_GLOBAL */
    TEST_ASSERT_EQ_INT(13,  rec[1]);  /* payload length */
    TEST_ASSERT_EQ_INT(0x01, rec[2]); /* flags: exported */
    TEST_ASSERT_EQ_INT(0,    rec[3]); /* segid: text */
    TEST_ASSERT_EQ_INT(0x10, rec[4]); /* offset low byte */
    TEST_ASSERT_EQ_INT(0,    rec[5]);
    TEST_ASSERT_EQ_INT(0,    rec[6]);
    TEST_ASSERT_EQ_INT(0,    rec[7]);
    TEST_ASSERT_EQ_STR("MY_SYM", (const char *)(rec + 8));
    return 0;
}

int test_rdf_bss_record(void) {
    /* bss_amount > 0 → one RDFREC_BSS in the header. */
    extern dword bss_amount;
    unsigned char buf[64];
    const unsigned char *rec;

    reset_rdf_state();
    bss_amount = 0x42;
    (void)capture_rdf(buf, sizeof(buf));

    rec = buf + 14;
    TEST_ASSERT_EQ_INT(5, rec[0]);   /* RDFREC_BSS */
    TEST_ASSERT_EQ_INT(4, rec[1]);
    TEST_ASSERT_EQ_INT(0x42, rec[2]);
    TEST_ASSERT_EQ_INT(0,    rec[3]);
    TEST_ASSERT_EQ_INT(0,    rec[4]);
    TEST_ASSERT_EQ_INT(0,    rec[5]);
    return 0;
}

int test_rdf_reloc_records(void) {
    /* Two relocs: one absolute code→data, one relative code→code. */
    unsigned char buf[128];
    const unsigned char *rec;

    reset_rdf_state();
    /* add_reloc inserts at head; we add code→data first so it ends up
     * second in the on-disk order. */
    add_reloc(SECTION_TEXT, 0x05, 2, 1 /*data*/, 0 /*abs*/);
    add_reloc(SECTION_TEXT, 0x0A, 2, 0 /*code*/, 1 /*relative*/);

    (void)capture_rdf(buf, sizeof(buf));
    rec = buf + 14;
    /* First record: the most-recently-added reloc (relative code→code). */
    TEST_ASSERT_EQ_INT(1,    rec[0]);  /* RDFREC_RELOC */
    TEST_ASSERT_EQ_INT(8,    rec[1]);
    TEST_ASSERT_EQ_INT(0x40, rec[2]);  /* relative bit, seg=text(0) */
    TEST_ASSERT_EQ_INT(0x0A, rec[3]);  /* offset */
    TEST_ASSERT_EQ_INT(2,    rec[7]);  /* width */
    TEST_ASSERT_EQ_INT(0,    rec[8]);  /* rseg lo = code */
    TEST_ASSERT_EQ_INT(0,    rec[9]);

    rec += 10;
    TEST_ASSERT_EQ_INT(1,    rec[0]);
    TEST_ASSERT_EQ_INT(8,    rec[1]);
    TEST_ASSERT_EQ_INT(0,    rec[2]);  /* abs, seg=text */
    TEST_ASSERT_EQ_INT(0x05, rec[3]);
    TEST_ASSERT_EQ_INT(2,    rec[7]);
    TEST_ASSERT_EQ_INT(1,    rec[8]);  /* rseg lo = data */
    return 0;
}

int test_rdf_import_record(void) {
    /* One CONST_IMPORT entry → RDFREC_IMPORT (type 2) in header:
     * byte type, byte payload_len, byte flags=0, word segid, ASCIIZ name. */
    unsigned char buf[128];
    t_constant *c;
    dword header_size;
    const unsigned char *rec;

    reset_rdf_state();
    c = add_const("PUTCHAR", CONST_IMPORT, 3);
    (void)c;

    (void)capture_rdf(buf, sizeof(buf));
    header_size = buf[10] | (buf[11] << 8) | (buf[12] << 16) | ((dword)buf[13] << 24);
    /* Payload = 1 flags + 2 segid + strlen+1. */
    TEST_ASSERT_EQ_INT(2 + 1 + 2 + 8, header_size);

    rec = buf + 14;
    TEST_ASSERT_EQ_INT(2,  rec[0]);   /* RDFREC_IMPORT */
    TEST_ASSERT_EQ_INT(11, rec[1]);   /* payload length */
    TEST_ASSERT_EQ_INT(0,  rec[2]);   /* flags */
    TEST_ASSERT_EQ_INT(3,  rec[3]);   /* segid lo */
    TEST_ASSERT_EQ_INT(0,  rec[4]);   /* segid hi */
    TEST_ASSERT_EQ_STR("PUTCHAR", (const char *)(rec + 5));
    return 0;
}

int test_rdf_segreloc_record(void) {
    /* A SEGRELOC must serialise as type=6, payload=8, then
     * [seg byte, dword offset, width byte, word rseg]. */
    unsigned char buf[64];
    const unsigned char *rec;

    reset_rdf_state();
    add_segreloc(SECTION_TEXT, 0x07, 1 /* target = data */);
    (void)capture_rdf(buf, sizeof(buf));

    rec = buf + 14;
    TEST_ASSERT_EQ_INT(6,    rec[0]);  /* RDFREC_SEGRELOC */
    TEST_ASSERT_EQ_INT(8,    rec[1]);  /* payload length */
    TEST_ASSERT_EQ_INT(0,    rec[2]);  /* patched seg = text */
    TEST_ASSERT_EQ_INT(0x07, rec[3]);  /* offset lo */
    TEST_ASSERT_EQ_INT(0,    rec[4]);
    TEST_ASSERT_EQ_INT(0,    rec[5]);
    TEST_ASSERT_EQ_INT(0,    rec[6]);
    TEST_ASSERT_EQ_INT(2,    rec[7]);  /* width */
    TEST_ASSERT_EQ_INT(1,    rec[8]);  /* rseg lo = data */
    TEST_ASSERT_EQ_INT(0,    rec[9]);
    return 0;
}

int test_rdf_segment_blocks(void) {
    /* Code: 4 bytes. Data: 3 bytes. Expect: code seg hdr, code bytes,
     * data seg hdr, data bytes, EOF seg hdr. */
    extern byte *outprog;
    extern byte *dataprog;
    extern word dataptr;
    extern word code_size;
    unsigned char buf[128];
    const unsigned char *p;
    dword header_size;

    reset_rdf_state();
    outprog[0] = 0x90; outprog[1] = 0x91; outprog[2] = 0x92; outprog[3] = 0xC3;
    code_size = 4;
    dataprog[0] = 'A'; dataprog[1] = 'B'; dataprog[2] = 0;
    dataptr = 3;

    (void)capture_rdf(buf, sizeof(buf));
    header_size = buf[10] | (buf[11] << 8) | (buf[12] << 16) | ((dword)buf[13] << 24);
    TEST_ASSERT_EQ_INT(0, header_size);  /* no records */

    p = buf + 14;
    /* Code segment header. */
    TEST_ASSERT_EQ_INT(1, p[0]); TEST_ASSERT_EQ_INT(0, p[1]);  /* type=1 */
    TEST_ASSERT_EQ_INT(0, p[2]); TEST_ASSERT_EQ_INT(0, p[3]);  /* num=0 */
    TEST_ASSERT_EQ_INT(0, p[4]); TEST_ASSERT_EQ_INT(0, p[5]);  /* reserved */
    TEST_ASSERT_EQ_INT(4, p[6]); TEST_ASSERT_EQ_INT(0, p[7]);  /* length lo */
    TEST_ASSERT_EQ_INT(0, p[8]); TEST_ASSERT_EQ_INT(0, p[9]);
    p += 10;
    TEST_ASSERT_EQ_INT(0x90, p[0]);
    TEST_ASSERT_EQ_INT(0xC3, p[3]);
    p += 4;
    /* Data segment header. */
    TEST_ASSERT_EQ_INT(2, p[0]); TEST_ASSERT_EQ_INT(0, p[1]);  /* type=2 */
    TEST_ASSERT_EQ_INT(1, p[2]); TEST_ASSERT_EQ_INT(0, p[3]);  /* num=1 */
    TEST_ASSERT_EQ_INT(3, p[6]); TEST_ASSERT_EQ_INT(0, p[7]);
    p += 10;
    TEST_ASSERT_EQ_INT('A', p[0]);
    TEST_ASSERT_EQ_INT('B', p[1]);
    TEST_ASSERT_EQ_INT(0,   p[2]);
    p += 3;
    /* EOF segment header. */
    TEST_ASSERT_EQ_INT(0, p[0]); TEST_ASSERT_EQ_INT(0, p[1]);
    return 0;
}

/*
 * Tests for pure helpers in MISC.C: hashing, number parsing, type
 * classification, build_address.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "TEST.H"
#include "../src/MSA2.H"
#include "../src/LEX.H"
#include "../src/EXPR.H"

int test_misc_hashcode_basic(void) {
    /* hashCode("") == 0. Single char "A" == 65. */
    TEST_ASSERT_EQ_INT(0,  hashCode(""));
    TEST_ASSERT_EQ_INT(65, hashCode("A"));
    /* "AB" = 65*31 + 66 = 2081. */
    TEST_ASSERT_EQ_INT(2081, hashCode("AB"));
    return 0;
}

int test_misc_hashcode_distinct(void) {
    /* Different strings should not collide for the lexer's needs. */
    TEST_ASSERT(hashCode("MOV") != hashCode("ADD"));
    TEST_ASSERT(hashCode("AX")  != hashCode("AL"));
    TEST_ASSERT_EQ_INT(hashCode("MOV"), hashCode("MOV"));
    return 0;
}

int test_misc_get_number_hex(void) {
    char buf[16];
    strcpy(buf, "0X1A");
    TEST_ASSERT_EQ_INT(0x1A, get_number(buf));
    strcpy(buf, "0XFF");
    TEST_ASSERT_EQ_INT(0xFF, get_number(buf));
    strcpy(buf, "0X0");
    TEST_ASSERT_EQ_INT(0, get_number(buf));
    return 0;
}

int test_misc_get_number_bin(void) {
    char buf[16];
    strcpy(buf, "0B1010");
    TEST_ASSERT_EQ_INT(10, get_number(buf));
    strcpy(buf, "0B11111111");
    TEST_ASSERT_EQ_INT(255, get_number(buf));
    return 0;
}

int test_misc_get_number_dec(void) {
    char buf[16];
    strcpy(buf, "42");
    TEST_ASSERT_EQ_INT(42, get_number(buf));
    strcpy(buf, "0");
    TEST_ASSERT_EQ_INT(0, get_number(buf));
    strcpy(buf, "1234");
    TEST_ASSERT_EQ_INT(1234, get_number(buf));
    return 0;
}

int test_misc_decdigit_rejects_letters(void) {
    /* Regression for the `||` vs `&&` bug in decdigit(): non-digit
     * characters in a decimal literal must produce an error and treat
     * the offending digit as 0, not as (c - '0'). */
    extern int errors;
    extern byte quiet;
    char buf[16];
    int saved_errors = errors;
    byte saved_quiet = quiet;
    errors = 0;
    quiet = 0; /* suppress diagnostic printing during the test */
    strcpy(buf, "9A");
    TEST_ASSERT_EQ_INT(9 * 10 + 0, get_number(buf));
    TEST_ASSERT(errors > saved_errors);
    errors = saved_errors;
    quiet = saved_quiet;
    return 0;
}

int test_misc_get_dword_terminator(void) {
    /* get_dword stops at the first non-digit and returns the pointer. */
    char buf[16];
    long v;
    char *tail;
    strcpy(buf, "0X10,REST");
    tail = get_dword(buf, &v);
    TEST_ASSERT_EQ_INT(0x10, v);
    TEST_ASSERT_EQ_INT(',', *tail);
    return 0;
}

int test_misc_get_type_registers(void) {
    TEST_ASSERT_EQ_INT(AL, get_type("AL"));
    TEST_ASSERT_EQ_INT(AX, get_type("AX"));
    TEST_ASSERT_EQ_INT(BX, get_type("BX"));
    TEST_ASSERT_EQ_INT(DS, get_type("DS"));
    return 0;
}

int test_misc_get_type_memory(void) {
    TEST_ASSERT_EQ_INT(MEM_16, get_type("[BX]"));
    TEST_ASSERT_EQ_INT(MEM_8,  get_type("BYTE[BX]"));
    TEST_ASSERT_EQ_INT(MEM_16, get_type("WORD[BX]"));
    TEST_ASSERT_EQ_INT(IMM,    get_type("42"));
    TEST_ASSERT_EQ_INT(IMM,    get_type("SOMETHING"));
    return 0;
}

int test_misc_build_address_reg(void) {
    t_address a;
    memset(&a, 0, sizeof(a));
    /* mod=3 means register form; modrm = (3<<6 | reg<<3 | rm). */
    a.mod = 3;
    a.reg = 0;
    a.rm  = 3;
    build_address(&a);
    TEST_ASSERT_EQ_INT(1, a.op_len);
    TEST_ASSERT_EQ_INT((3 << 6) | (0 << 3) | 3, a.op[0]);
    return 0;
}

int test_misc_build_address_disp16(void) {
    /* mod=0, rm=6 is direct [disp16] (3 bytes total). */
    t_address a;
    memset(&a, 0, sizeof(a));
    a.mod = 0;
    a.reg = 1;          /* CX */
    a.rm  = 6;
    a.disp = 0x1234;
    build_address(&a);
    TEST_ASSERT_EQ_INT(3, a.op_len);
    TEST_ASSERT_EQ_INT((0 << 6) | (1 << 3) | 6, a.op[0]);
    TEST_ASSERT_EQ_INT(0x34, a.op[1]);
    TEST_ASSERT_EQ_INT(0x12, a.op[2]);
    return 0;
}

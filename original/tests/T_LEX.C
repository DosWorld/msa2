/*
 * Tests for LEX.C: lookupLex and the SECTION directive registration.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "TEST.H"
#include "../src/MSA2.H"
#include "../src/LEX.H"

int test_lex_lookup_known(void) {
    int prescan = -1;
    lex_init();
    TEST_ASSERT_EQ_INT(LEX_MOV,  lookupLex("MOV", &prescan));
    TEST_ASSERT_EQ_INT(LEX_DB,   lookupLex("DB",  &prescan));
    TEST_ASSERT_EQ_INT(LEX_DW,   lookupLex("DW",  &prescan));
    TEST_ASSERT_EQ_INT(LEX_DD,   lookupLex("DD",  &prescan));
    TEST_ASSERT_EQ_INT(LEX_RET,  lookupLex("RET", &prescan));
    lex_done();
    return 0;
}

int test_lex_lookup_unknown(void) {
    int prescan = -1;
    lex_init();
    TEST_ASSERT_EQ_INT(LEX_NONE, lookupLex("",        &prescan));
    TEST_ASSERT_EQ_INT(LEX_NONE, lookupLex("NOSUCH",  &prescan));
    lex_done();
    return 0;
}

int test_lex_section_directive(void) {
    /* SECTION and SEGMENT must both map to LEX_SECTION. RESB/RESW/RESD
     * to their own ids. SEG is reserved for the segment-of operator. */
    int prescan = -1;
    lex_init();
    TEST_ASSERT_EQ_INT(LEX_SECTION, lookupLex("SECTION", &prescan));
    TEST_ASSERT_EQ_INT(LEX_SECTION, lookupLex("SEGMENT", &prescan));
    TEST_ASSERT_EQ_INT(LEX_RESB,    lookupLex("RESB", &prescan));
    TEST_ASSERT_EQ_INT(LEX_RESW,    lookupLex("RESW", &prescan));
    TEST_ASSERT_EQ_INT(LEX_RESD,    lookupLex("RESD", &prescan));
    TEST_ASSERT_EQ_INT(LEX_EXPORT,  lookupLex("EXPORT", &prescan));
    TEST_ASSERT_EQ_INT(LEX_SEG,     lookupLex("SEG", &prescan));
    lex_done();
    return 0;
}

/*
 * Tests for EXPR.C: add_const, find_const, get_const.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "TEST.H"
#include "../src/MSA2.H"
#include "../src/EXPR.H"

int test_expr_add_find(void) {
    t_constant *c;
    expr_init();
    add_const("FOO", CONST_EXPR, 0x1234);
    add_const("BAR", CONST_LABEL, 0x42);

    c = find_const("FOO");
    TEST_ASSERT(c != NULL);
    TEST_ASSERT_EQ_INT(0x1234, c->value);
    TEST_ASSERT_EQ_INT(CONST_EXPR, c->type);

    c = find_const("BAR");
    TEST_ASSERT(c != NULL);
    TEST_ASSERT_EQ_INT(0x42, c->value);
    TEST_ASSERT_EQ_INT(CONST_LABEL, c->type);

    c = find_const("NOTTHERE");
    TEST_ASSERT(c == NULL);

    expr_done();
    return 0;
}

int test_expr_get_const_arithmetic(void) {
    /* get_const supports +, -, *, /. */
    expr_init();
    add_const("A", CONST_EXPR, 10);
    add_const("B", CONST_EXPR, 3);

    TEST_ASSERT_EQ_INT(13, get_const("A+B"));
    TEST_ASSERT_EQ_INT(7,  get_const("A-B"));
    TEST_ASSERT_EQ_INT(30, get_const("A*B"));
    TEST_ASSERT_EQ_INT(3,  get_const("A/B"));
    /* Numeric literal mixed with name. */
    TEST_ASSERT_EQ_INT(15, get_const("A+5"));

    expr_done();
    return 0;
}

int test_expr_get_const_div_by_zero(void) {
    /* `A/0` must emit a diagnostic and return without dividing. */
    extern int errors;
    extern byte quiet;
    int saved_errors;
    byte saved_quiet;

    expr_init();
    add_const("A", CONST_EXPR, 10);
    saved_errors = errors;
    saved_quiet = quiet;
    errors = 0;
    quiet = 0;

    (void)get_const("A/0");
    TEST_ASSERT(errors > 0);
    errors = 0;
    (void)get_const("A%0");
    TEST_ASSERT(errors > 0);

    errors = saved_errors;
    quiet = saved_quiet;
    expr_done();
    return 0;
}

int test_expr_get_const_undef_pass1(void) {
    /* On pass 0, undefined names silently resolve to 0 (no error). */
    extern int pass;
    int saved_pass = pass;
    int saved_errors = errors;

    expr_init();
    pass = 0;
    errors = 0;
    TEST_ASSERT_EQ_INT(0, get_const("UNDEFINED_SYMBOL"));
    TEST_ASSERT_EQ_INT(0, errors);

    pass = saved_pass;
    errors = saved_errors;
    expr_done();
    return 0;
}

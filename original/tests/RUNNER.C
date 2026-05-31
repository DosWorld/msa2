/*
 * MSA2 test runner. Defines main(), tallies failures, exits nonzero on
 * any failed test.
 *
 * Each test_*.c file lists its tests in a static table that this runner
 * pulls in via extern declarations and the TEST_LIST macro. To add a
 * test:
 *   1. Implement int test_group_case(void) in some test_*.c file.
 *   2. Add an entry below in the all_tests[] table.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "TEST.H"

int g_test_failures = 0;
const char *g_test_current = "";

/* Forward declarations of every test function. */
int test_misc_hashcode_basic(void);
int test_misc_hashcode_distinct(void);
int test_misc_get_number_hex(void);
int test_misc_get_number_bin(void);
int test_misc_get_number_dec(void);
int test_misc_decdigit_rejects_letters(void);
int test_misc_get_dword_terminator(void);
int test_misc_get_type_registers(void);
int test_misc_get_type_memory(void);
int test_misc_build_address_reg(void);
int test_misc_build_address_disp16(void);

int test_expr_add_find(void);
int test_expr_get_const_arithmetic(void);
int test_expr_get_const_div_by_zero(void);
int test_expr_get_const_undef_pass1(void);

int test_lex_lookup_known(void);
int test_lex_lookup_unknown(void);
int test_lex_section_directive(void);

int test_rdf_signature_and_header_size(void);
int test_rdf_global_record(void);
int test_rdf_bss_record(void);
int test_rdf_reloc_records(void);
int test_rdf_import_record(void);
int test_rdf_segreloc_record(void);
int test_rdf_segment_blocks(void);

int test_asm_section_switching(void);
int test_asm_export_marks_constant(void);
int test_asm_rdf_data_reloc(void);
int test_asm_seg_of_in_mov(void);
int test_asm_seg_of_in_dw(void);
int test_asm_encode_add(void);
int test_asm_encode_inc(void);
int test_asm_encode_jmp(void);
int test_asm_seg_rejected_for_non_rdf(void);
int test_asm_bare_end(void);
int test_asm_bss_resb_resw(void);
int test_asm_import_directive(void);
int test_asm_import_mov_imm16(void);
int test_asm_import_seg_of(void);
int test_asm_import_call_rel16(void);
int test_asm_import_jmp_short_rejected(void);
int test_asm_import_dw(void);
int test_asm_import_disp16_mem(void);
int test_asm_import_rejected_for_non_rdf(void);
int test_asm_jmp_far_direct_import(void);
int test_asm_call_far_direct_local(void);
int test_asm_jmp_far_direct_rejected_for_non_rdf(void);
int test_asm_jmp_far_indirect_mem(void);
int test_asm_call_far_indirect_mem(void);
int test_asm_jmp_far_reg_rejected(void);
int test_asm_local_label_two_procs(void);
int test_asm_local_label_no_parent(void);
int test_asm_local_label_section_resets_parent(void);
int test_asm_export_local_rejected(void);
int test_asm_local_label_disp_reloc(void);
int test_asm_define_numeric(void);
int test_asm_define_expression(void);
int test_asm_undef(void);
int test_asm_define_cycle_guard(void);
int test_asm_define_mutual_cycle_guard(void);
int test_asm_rel_offset_no_overflow_16bit(void);
int test_asm_label_value_high_16bit(void);
int test_asm_macro_no_args(void);
int test_asm_macro_one_arg(void);
int test_asm_macro_local_label(void);
int test_asm_macro_argc_mismatch(void);
int test_asm_macro_nested_rejected(void);
int test_asm_macro_recursion_guard(void);
int test_asm_macro_mutual_recursion_guard(void);
int test_asm_macro_no_arbitrary_caps(void);
int test_asm_include_basic(void);
int test_asm_include_unquoted(void);
int test_asm_include_nested(void);
int test_asm_include_missing_file(void);
int test_asm_include_too_deep(void);
int test_asm_include_error_attribution(void);

typedef int (*test_fn)(void);

struct test_entry {
    const char *name;
    test_fn fn;
};

static struct test_entry all_tests[] = {
    { "misc_hashcode_basic",        test_misc_hashcode_basic },
    { "misc_hashcode_distinct",     test_misc_hashcode_distinct },
    { "misc_get_number_hex",        test_misc_get_number_hex },
    { "misc_get_number_bin",        test_misc_get_number_bin },
    { "misc_get_number_dec",        test_misc_get_number_dec },
    { "misc_decdigit_rejects_letters", test_misc_decdigit_rejects_letters },
    { "misc_get_dword_terminator",  test_misc_get_dword_terminator },
    { "misc_get_type_registers",    test_misc_get_type_registers },
    { "misc_get_type_memory",       test_misc_get_type_memory },
    { "misc_build_address_reg",     test_misc_build_address_reg },
    { "misc_build_address_disp16",  test_misc_build_address_disp16 },

    { "expr_add_find",              test_expr_add_find },
    { "expr_get_const_arithmetic",  test_expr_get_const_arithmetic },
    { "expr_get_const_div_by_zero", test_expr_get_const_div_by_zero },
    { "expr_get_const_undef_pass1", test_expr_get_const_undef_pass1 },

    { "lex_lookup_known",           test_lex_lookup_known },
    { "lex_lookup_unknown",         test_lex_lookup_unknown },
    { "lex_section_directive",      test_lex_section_directive },

    { "rdf_signature_and_header_size", test_rdf_signature_and_header_size },
    { "rdf_global_record",          test_rdf_global_record },
    { "rdf_bss_record",             test_rdf_bss_record },
    { "rdf_reloc_records",          test_rdf_reloc_records },
    { "rdf_import_record",          test_rdf_import_record },
    { "rdf_segreloc_record",        test_rdf_segreloc_record },
    { "rdf_segment_blocks",         test_rdf_segment_blocks },

    { "asm_section_switching",      test_asm_section_switching },
    { "asm_export_marks_constant",  test_asm_export_marks_constant },
    { "asm_rdf_data_reloc",         test_asm_rdf_data_reloc },
    { "asm_seg_of_in_mov",          test_asm_seg_of_in_mov },
    { "asm_seg_of_in_dw",           test_asm_seg_of_in_dw },
    { "asm_encode_add",             test_asm_encode_add },
    { "asm_encode_inc",             test_asm_encode_inc },
    { "asm_encode_jmp",             test_asm_encode_jmp },
    { "asm_seg_rejected_for_non_rdf", test_asm_seg_rejected_for_non_rdf },
    { "asm_bare_end",               test_asm_bare_end },
    { "asm_bss_resb_resw",          test_asm_bss_resb_resw },
    { "asm_import_directive",       test_asm_import_directive },
    { "asm_import_mov_imm16",       test_asm_import_mov_imm16 },
    { "asm_import_seg_of",          test_asm_import_seg_of },
    { "asm_import_call_rel16",      test_asm_import_call_rel16 },
    { "asm_import_jmp_short_rejected", test_asm_import_jmp_short_rejected },
    { "asm_import_dw",              test_asm_import_dw },
    { "asm_import_disp16_mem",      test_asm_import_disp16_mem },
    { "asm_import_rejected_for_non_rdf", test_asm_import_rejected_for_non_rdf },
    { "asm_jmp_far_direct_import",  test_asm_jmp_far_direct_import },
    { "asm_call_far_direct_local",  test_asm_call_far_direct_local },
    { "asm_jmp_far_direct_rejected_for_non_rdf", test_asm_jmp_far_direct_rejected_for_non_rdf },
    { "asm_jmp_far_indirect_mem",   test_asm_jmp_far_indirect_mem },
    { "asm_call_far_indirect_mem",  test_asm_call_far_indirect_mem },
    { "asm_jmp_far_reg_rejected",   test_asm_jmp_far_reg_rejected },
    { "asm_local_label_two_procs",  test_asm_local_label_two_procs },
    { "asm_local_label_no_parent",  test_asm_local_label_no_parent },
    { "asm_local_label_section_resets_parent", test_asm_local_label_section_resets_parent },
    { "asm_export_local_rejected",  test_asm_export_local_rejected },
    { "asm_local_label_disp_reloc", test_asm_local_label_disp_reloc },
    { "asm_define_numeric",         test_asm_define_numeric },
    { "asm_define_expression",      test_asm_define_expression },
    { "asm_undef",                  test_asm_undef },
    { "asm_define_cycle_guard",     test_asm_define_cycle_guard },
    { "asm_define_mutual_cycle_guard", test_asm_define_mutual_cycle_guard },
    { "asm_rel_offset_no_overflow_16bit", test_asm_rel_offset_no_overflow_16bit },
    { "asm_label_value_high_16bit", test_asm_label_value_high_16bit },
    { "asm_macro_no_args",          test_asm_macro_no_args },
    { "asm_macro_one_arg",          test_asm_macro_one_arg },
    { "asm_macro_local_label",      test_asm_macro_local_label },
    { "asm_macro_argc_mismatch",    test_asm_macro_argc_mismatch },
    { "asm_macro_nested_rejected",  test_asm_macro_nested_rejected },
    { "asm_macro_recursion_guard",  test_asm_macro_recursion_guard },
    { "asm_macro_mutual_recursion_guard", test_asm_macro_mutual_recursion_guard },
    { "asm_macro_no_arbitrary_caps", test_asm_macro_no_arbitrary_caps },
    { "asm_include_basic",          test_asm_include_basic },
    { "asm_include_unquoted",       test_asm_include_unquoted },
    { "asm_include_nested",         test_asm_include_nested },
    { "asm_include_missing_file",   test_asm_include_missing_file },
    { "asm_include_too_deep",       test_asm_include_too_deep },
    { "asm_include_error_attribution", test_asm_include_error_attribution },

    { NULL, NULL }
};

int main(int argc, char *argv[]) {
    struct test_entry *t;
    int run = 0, fail_at_start;
    const char *filter = NULL;

    if(argc > 1) {
        filter = argv[1];
    }

    for(t = all_tests; t->name != NULL; t++) {
        if(filter != NULL && strstr(t->name, filter) == NULL) {
            continue;
        }
        g_test_current = t->name;
        fail_at_start = g_test_failures;
        t->fn();
        run++;
        if(g_test_failures == fail_at_start) {
            printf("PASS: %s\n", t->name);
        }
    }

    printf("\n%d test(s) run, %d failure(s)\n", run, g_test_failures);
    return g_test_failures == 0 ? 0 : 1;
}

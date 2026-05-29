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

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "MSA2.H"
#include "LEX.H"
#include "EXPR.H"

long int linenr;
word old_outptr;
char *param[2];
int param_type[2];

/* Import id allocation. RDF reserves 0/1/2 for code/data/bss, so import
 * ids start at 3. Reset per pass: pass 0 assigns ids; pass 1 finds the
 * already-allocated CONST_IMPORT entries and reuses them. */
static word next_import_id;

inline void out_word(int x) {
    outprog[outptr++] = x & 0xff;
    outprog[outptr++] = (char)(x >> 8) & 0xff;
}

inline void out_long(long int x) {
    outprog[outptr++] = x & 0xff;
    outprog[outptr++] = (char)(x >> 8) & 0xff;
    outprog[outptr++] = (char)(x >> 16) & 0xff;
    outprog[outptr++] = (char)(x >> 24) & 0xff;
}

/* Section-aware emit. SECTION_TEXT writes to outprog/outptr; SECTION_DATA
 * writes to dataprog/dataptr; SECTION_BSS just bumps bss_amount.
 * Returns the offset *before* the write (i.e. the position of the emitted byte).
 */
static dword cur_offset(void) {
    switch(cur_section) {
    case SECTION_DATA: return dataptr;
    case SECTION_BSS:  return bss_amount;
    default:           return outptr;
    }
}

static void emit_byte(byte v) {
    switch(cur_section) {
    case SECTION_DATA: dataprog[dataptr++] = v; break;
    case SECTION_BSS:  bss_amount++; break;
    default:           outprog[outptr++] = v; break;
    }
}

static void emit_word_val(int v) {
    emit_byte(v & 0xff);
    emit_byte((v >> 8) & 0xff);
}

static void emit_long_val(long int v) {
    emit_byte(v & 0xff);
    emit_byte((v >> 8) & 0xff);
    emit_byte((v >> 16) & 0xff);
    emit_byte((v >> 24) & 0xff);
}

/* If 's' refers to a single label/data/bss/import symbol (no math, no
 * number), return its t_constant*; else NULL. Used to decide whether to
 * emit a relocation record for the about-to-be-patched word.
 */
static t_constant *single_symbol_ref(const char *s) {
    char tmp[64];
    int j = 0;
    t_constant *c;
    char first;

    /* skip optional leading '+' but not '-' (a leading '-' means math). */
    if(*s == '+') s++;
    first = *s;
    if(first == 0) return NULL;
    if(first >= '0' && first <= '9') return NULL;  /* numeric literal */
    if(first == '\'') return NULL;                  /* char literal */
    while(*s && j < (int)sizeof(tmp) - 1) {
        char c = *s;
        if(c == '+' || c == '-' || c == '*' || c == '/' || c == '%') {
            return NULL;
        }
        tmp[j++] = c;
        s++;
    }
    tmp[j] = 0;
    c = find_const(tmp);
    if(c == NULL) return NULL;
    if(CONST_TYPE(c) != CONST_LABEL && CONST_TYPE(c) != CONST_DATA
        && CONST_TYPE(c) != CONST_BSS && CONST_TYPE(c) != CONST_IMPORT) {
        return NULL;
    }
    return c;
}

/* True iff `expr` lexically looks like the SEG-of operator (SEG<name>).
 * Detection is independent of whether <name> is a defined symbol — that
 * lets callers reject `seg foo` early for non-RDF targets with a clear
 * diagnostic, instead of silently falling through to an "undefined
 * constant SEGfoo" warning. */
static int is_seg_of_form(const char *expr) {
    return expr[0] == 'S' && expr[1] == 'E' && expr[2] == 'G' && expr[3] != 0;
}

/* Return the target t_constant* for a SEG<name> operand, or NULL if the
 * operand isn't a SEG-of form or the symbol is not a label/data/bss/import
 * reference. Callers must already have verified the target supports
 * SEG (RDF only). */
static t_constant *seg_of_symbol(const char *expr) {
    t_constant *c;
    if(!is_seg_of_form(expr)) return NULL;
    c = find_const(expr + 3);
    if(c == NULL) return NULL;
    if(CONST_TYPE(c) != CONST_LABEL && CONST_TYPE(c) != CONST_DATA
        && CONST_TYPE(c) != CONST_BSS && CONST_TYPE(c) != CONST_IMPORT) {
        return NULL;
    }
    return c;
}

/* True iff `expr` uses the SEG operator on a target that doesn't support
 * it. Emits a diagnostic and returns 1 if so; otherwise returns 0. */
static int reject_seg_for_non_rdf(const char *expr) {
    if(target == TARGET_RDF) return 0;
    if(!is_seg_of_form(expr)) return 0;
    out_msg("seg operator is only valid for -f rdf target", 0);
    return 1;
}

static word rseg_for_section(int sec) {
    switch(sec) {
    case SECTION_DATA: return 1;
    case SECTION_BSS:  return 2;
    default:           return 0;
    }
}

/* Reference segment for a symbol: an import resolves to its allocated
 * import id (stored in c->value); everything else maps via its section. */
static word rseg_for_symbol(t_constant *c) {
    if(CONST_TYPE(c) == CONST_IMPORT) return (word)c->value;
    return rseg_for_section(c->section);
}

/* Record an absolute RDF reloc at `patch_ofs` if `expr` resolves to a
 * single label/data/bss/import symbol. No-op for SECTION_BSS (which has no
 * backing bytes to patch). */
static void reloc_if_symbol(const char *expr, dword patch_ofs, byte width) {
    t_constant *c = single_symbol_ref(expr);
    if(c == NULL) return;
    add_reloc((byte)cur_section, patch_ofs, width,
              rseg_for_symbol(c), 0);
}

/* Emit a 16-bit value with optional RDF relocation. Handles SEG<name>. */
static void emit_word_with_reloc(const char *expr) {
    dword patch_ofs = cur_offset();
    t_constant *c;
    if(reject_seg_for_non_rdf(expr)) {
        emit_word_val(0);
        return;
    }
    c = seg_of_symbol(expr);
    if(c != NULL) {
        add_segreloc((byte)cur_section, patch_ofs, rseg_for_symbol(c));
        emit_word_val(0);
        return;
    }
    reloc_if_symbol(expr, patch_ofs, 2);
    emit_word_val(get_const(expr));
}

static void emit_long_with_reloc(long int v, const char *expr) {
    if(expr != NULL) {
        reloc_if_symbol(expr, cur_offset(), 4);
    }
    emit_long_val(v);
}

char match_params(t_instruction *cinstr, int pcount) {
    int k, type, t;

    for(k = 0; k < pcount; k++) {
        type = param_type[k];
        switch(t = cinstr->param_type[k]) {
        case RM_8:
            if(type !=MEM_8 && (type < ACC_8 || type > BH)) {
                return 0;
            }
            break;
        case RM_16:
            if(type != MEM_16 && (type < ACC_16 || type >= SEG)) {
                return 0;
            }
            break;
        case REG_8:
            if(type < ACC_8 || type >= ACC_16) {
                return 0;
            }
            break;
        case REG_16:
            if((type < ACC_16 || type >= SEG)) {
                return 0;
            }
            break;
        case REG_SEG:
            if((type < SEG || type > DS)) {
                return 0;
            }
            break;
        default:
            if(type != t) {
                return 0;
            }
            break;
        }
    }
    return 1;
}

inline char getReg(int r, int min, int max, const char *msg) {
    if(r < min || r > max) {
        out_msg(msg, 0);
        return 0;
    }
    return r - min;
}

/* If addr encodes [disp16] (direct addressing: mod==0, rm==6), the disp
 * word is at outprog[op_start+1..+2]; record a reloc against the symbol
 * named in addr->disp_src. */
static void maybe_reloc_for_addr(t_address *addr, dword op_start) {
    if(addr->mod != 0 || addr->rm != 6) return;
    if(addr->disp_src == NULL) return;
    if(cur_section != SECTION_TEXT) return;  /* RM only used in code */
    reloc_if_symbol(addr->disp_src, op_start + 1, 2);
}

/* Helper that drives all 9 OP_CMD_RM* / OP_CMD_RMLINE_* arms in
 * do_instruction. The variants differ only in:
 *
 *   addr_param   - which user param feeds get_address  (op1 or op2)
 *   reg_literal  - if >= 0, the modrm reg field is fixed to this value
 *                  (RMLINE forms); otherwise reg comes from getReg() on
 *                  the *other* param's type.
 *   reg_min/max  - getReg() range used when reg_literal < 0.
 *   err_msg      - getReg() diagnostic on out-of-range.
 *   want_reloc   - 0 for 8-bit forms (no disp16 reloc target), 1 for the
 *                  16-bit / segment forms.
 *
 * Returns nothing; advances outptr.
 */
static void do_rm_op(int addr_param, int reg_param, int reg_literal,
                     int reg_min, int reg_max, const char *err_msg,
                     int want_reloc) {
    t_address addr;
    dword op_start;
    get_address(&addr, param[addr_param]);
    if(reg_literal >= 0) {
        addr.reg = (byte)reg_literal;
    } else {
        addr.reg = getReg(param_type[reg_param], reg_min, reg_max, err_msg);
    }
    build_address(&addr);
    op_start = outptr;
    memcpy(outprog + outptr, &addr.op, addr.op_len);
    outptr += addr.op_len;
    if(want_reloc) {
        maybe_reloc_for_addr(&addr, op_start);
    }
}

inline void do_instruction(t_instruction *cinstr) {
    int j = 0;
    int op1, op2, pt2;
    long z;

    while(cinstr->op[j] != 0) {
        op1 = cinstr->op[j + 1];
        op2 = cinstr->op[j + 2];
        pt2 = param_type[op2];
        switch(cinstr->op[j]) {
        case OP_CMD_OP:
            outprog[outptr++] = op1;
            break;
        case OP_CMD_IMM8:
            outprog[outptr++] = get_const(param[op1]);
            break;
        case OP_CMD_IMM16:
            /* cur_section is guaranteed SECTION_TEXT here (instructions
             * are gated above), so emit_word_with_reloc() emits to
             * outprog/outptr just like out_word() did. */
            emit_word_with_reloc(param[op1]);
            break;
        case OP_CMD_PLUSREG8:
            outprog[outptr++] = op1 + getReg(pt2, ACC_8, BH, "Syntax error, expected reg8");
            j++;
            break;
        case OP_CMD_PLUSREG16:
            outprog[outptr++] = op1 + getReg(pt2, ACC_16, DI, "Syntax error, expected reg16");
            j++;
            break;
        case OP_CMD_PLUSREGSEG:
            outprog[outptr++] = op1 + getReg(pt2, SEG, DS, "Syntax error, expected segreg");
            j++;
            break;
        case OP_CMD_RM1_8:
            do_rm_op(op1, op2, -1, ACC_8,  BH, "Syntax error, expected reg8",  0);
            j++;
            break;
        case OP_CMD_RM1_16:
            do_rm_op(op1, op2, -1, ACC_16, DI, "Syntax error, expected reg16", 1);
            j++;
            break;
        case OP_CMD_RM2_8:
            do_rm_op(op2, op1, -1, ACC_8,  BH, "Syntax error, expected reg8",  0);
            j++;
            break;
        case OP_CMD_RM2_16:
            do_rm_op(op2, op1, -1, ACC_16, DI, "Syntax error, expected reg16", 1);
            j++;
            break;
        case OP_CMD_RM2_SEG:
            do_rm_op(op2, op1, -1, SEG,    DS, "Syntax error, expected segreg", 1);
            j++;
            break;
        case OP_CMD_RM1_SEG:
            do_rm_op(op1, op2, -1, SEG,    DS, "Syntax error, expected segreg", 1);
            j++;
            break;
        case OP_CMD_RMLINE_8:
            do_rm_op(op2, 0, op1, 0, 0, NULL, 0);
            j++;
            break;
        case OP_CMD_RMLINE_16:
            do_rm_op(op2, 0, op1, 0, 0, NULL, 1);
            j++;
            break;
        case OP_CMD_REL8: {
            t_constant *sc = single_symbol_ref(param[op1]);
            if(sc != NULL && CONST_TYPE(sc) == CONST_IMPORT) {
                if(pass) {
                    out_msg("8-bit relative reference to import is out of range", 0);
                }
                outprog[outptr++] = 0;
                break;
            }
            z = get_const(param[op1]) - (outptr + 1);
            if(labs(z) > 127 && pass) {
                out_msg("Too long jump", 1);
            }
            outprog[outptr++] = z & 0xff;
            break;
        }
        case OP_CMD_REL16: {
            t_constant *sc = single_symbol_ref(param[op1]);
            if(sc != NULL && CONST_TYPE(sc) == CONST_IMPORT) {
                /* Spec §3.3: store negative addend -(patch_ofs + width).
                 * patch_ofs here is the disp word's offset (current outptr).
                 * Linker adds the target address; result is the rel16 displacement. */
                dword patch_ofs = outptr;
                long addend = -(long)(patch_ofs + 2);
                add_reloc((byte)cur_section, patch_ofs, 2, rseg_for_symbol(sc), 1);
                out_word((int)addend);
                break;
            }
            out_word(get_const(param[op1])-(outptr + 2));
            break;
        }
        case OP_CMD_FAR_PTR: {
            /* JMP FAR label / CALL FAR label: emit a 4-byte far pointer
             * (offset16 then segment16). RDF-only — the linker resolves
             * both halves from the symbol via a normal RELOC plus a
             * SEGRELOC. */
            t_constant *sc;
            dword patch_ofs;
            word rseg;
            if(target != TARGET_RDF) {
                if(pass) {
                    out_msg("direct far jmp/call is only valid for -f rdf target", 0);
                }
                out_word(0);
                out_word(0);
                break;
            }
            sc = single_symbol_ref(param[op1]);
            if(sc == NULL) {
                if(pass) {
                    out_msg("direct far jmp/call requires a symbol operand", 0);
                }
                out_word(0);
                out_word(0);
                break;
            }
            rseg = rseg_for_symbol(sc);
            patch_ofs = outptr;
            add_reloc((byte)cur_section, patch_ofs, 2, rseg, 0);
            out_word(0);
            patch_ofs = outptr;
            add_segreloc((byte)cur_section, patch_ofs, rseg);
            out_word(0);
            break;
        }
        }
        j += 2;
    }
}

int assemble(char* fname) {
    FILE *infile;
    char *line, *a1, *p;
    t_line *cur;
    char cf, stop, found;
    int l, prescan;
    t_constant *org_const, *ofs_const, *c;
    long int lvalue;
    int lex1, lex2;
    t_instruction *cinstr;

    if((infile = fopen(fname,"rb")) == NULL) {
        out_msg("Can't open input file", 0);
        return 0;
    }

    a1 = line = NULL;
    linenr = 0;
    stop = 0;
    next_import_id = 3;

    ofs_const = find_const("$");
    org_const = find_const("$$");

    cur = (t_line *)MSA_MALLOC(sizeof(t_line));
    line = (char *)MSA_MALLOC(4096);
    a1 = (char *)MSA_MALLOC(4096);
    param[0] = cur->p1;
    param[1] = cur->p2;

    while(fgets(line, 4095, infile) && (!stop)) {
        linenr++;
        strip_line(line);

        /* Reset only the scalar fields and the first byte of each
         * string buffer; sizeof(t_line) is ~8KB and clearing it on
         * every input line is expensive for no benefit (split_line
         * always writes through each buffer it touches and never
         * reads past a terminator). */
        cur->has_label = 0;
        cur->pcount = 0;
        cur->has_lock = 0;
        cur->rep_type = 0;
        cur->lex2 = 0;
        cur->label[0] = 0;
        cur->cmd[0] = 0;
        cur->p1[0] = 0;
        cur->p2[0] = 0;

        split_line(cur, line, a1);

        ofs_const->value = (int)cur_offset();

        if(cur->has_label) {
            int ct = CONST_LABEL;
            if(cur_section == SECTION_DATA) ct = CONST_DATA;
            else if(cur_section == SECTION_BSS) ct = CONST_BSS;
            {
                t_constant *lc = add_const(cur->label, ct, (int)cur_offset());
                lc->section = (char)cur_section;
            }
        }

        if(cur->has_lock) {
            emit_byte(0xf0);
        }

        switch(cur->rep_type) {
        case LEX_REP:
            emit_byte(0xf3);
            break;
        case LEX_REPNZ:
            emit_byte(0xf2);
            break;
        }

        if(cur->cmd[0] == 0) {
            continue;
        }

        lex1 = lookupLex(cur->cmd, &prescan);

        switch(lex1) {
        case LEX_DD:
            l = 0;
            p = cur->p1;
            while(*p) {
                if(*p == ',') {
                    a1[l] = 0;
                    /* Try symbol reloc; else fall back to numeric. */
                    {
                        t_constant *sc = single_symbol_ref(a1);
                        if(sc != NULL) {
                            emit_long_with_reloc((long)get_const(a1), a1);
                        } else {
                            char *tail = get_dword(a1, &lvalue);
                            (void)tail;
                            emit_long_val(lvalue);
                        }
                    }
                    l = 0;
                } else {
                    a1[l++] = *p;
                }
                p++;
            }
            a1[l] = 0;
            if(l != 0) {
                t_constant *sc = single_symbol_ref(a1);
                if(sc != NULL) {
                    emit_long_with_reloc((long)get_const(a1), a1);
                } else {
                    char *tail = get_dword(a1, &lvalue);
                    (void)tail;
                    emit_long_val(lvalue);
                }
            }
            break;
        case LEX_DW:
            l = 0;
            p = cur->p1;
            while(*p) {
                if(*p == ',') {
                    a1[l] = 0;
                    emit_word_with_reloc(a1);
                    l = 0;
                } else {
                    a1[l++] = *p;
                }
                p++;
            }
            a1[l] = 0;
            if(l != 0) {
                emit_word_with_reloc(a1);
            }
            break;
        case LEX_DB:
            p = cur->p1;
            l = 0;
            cf = 0;
            while(*p) {
                if(*p == ',' && !cf) {
                    a1[l] = 0;
                    if(l == 0) {
                        p++;
                        cf = 0;
                    } else {
                        emit_byte(get_const(a1));
                        p++;
                    }
                    if(*p == '\"') {
                        cf = 1;
                        l = 0;
                    } else {
                        cf = 0;
                        l = 0;
                        a1[l++] = *p;
                    }
                } else {
                    if(*p == '\"') {
                        cf ^= 1;
                    } else {
                        if(cf) {
                            emit_byte(*p);
                        } else {
                            a1[l++] = *p;
                        }
                    }
                }
                p++;
            }
            a1[l] = 0;
            if(!cf) {
                emit_byte(get_const(a1));
            }
            break;
        case LEX_RESB:
            bss_amount += get_const(cur->p1);
            break;
        case LEX_RESW:
            bss_amount += 2 * (dword)get_const(cur->p1);
            break;
        case LEX_RESD:
            bss_amount += 4 * (dword)get_const(cur->p1);
            break;
        case LEX_SECTION: {
            char *sn = cur->p1;
            if(!strcmp(sn, ".TEXT") || !strcmp(sn, ".CODE") || !strcmp(sn, "TEXT") || !strcmp(sn, "CODE")) {
                cur_section = SECTION_TEXT;
            } else if(!strcmp(sn, ".DATA") || !strcmp(sn, "DATA")) {
                cur_section = SECTION_DATA;
            } else if(!strcmp(sn, ".BSS") || !strcmp(sn, "BSS")) {
                cur_section = SECTION_BSS;
            } else {
                out_msg_str("Unknown section '%s'", 0, sn);
            }
            break;
        }
        case LEX_EXPORT:
            if((c = find_const(cur->p1)) != NULL) {
                c->is_export = 1;
            }
            break;
        case LEX_IMPORT: {
            char name[MAX_LABEL];
            char *q;
            int n;
            if(target != TARGET_RDF) {
                out_msg("import is only valid for -f rdf target", 0);
                break;
            }
            q = cur->p1;
            while(*q) {
                n = 0;
                while(*q && *q != ',' && n < (int)sizeof(name) - 1) {
                    name[n++] = *q++;
                }
                name[n] = 0;
                if(*q == ',') q++;
                if(n == 0) continue;
                c = find_const(name);
                if(c != NULL) {
                    if(CONST_TYPE(c) == CONST_IMPORT) {
                        /* second pass: already allocated, leave as-is */
                    } else {
                        if(!pass) {
                            out_msg_str("Symbol %s already defined", 0, name);
                        }
                    }
                } else {
                    c = add_const(name, CONST_IMPORT, (int)next_import_id);
                    next_import_id++;
                }
            }
            break;
        }
        case LEX_ORG:
            if(is_org_def) {
                out_msg("Org already defined and could not be changed", 0);
            } else {
                outptr = org = get_const(cur->p1);
                org_const->value = org;
            }
            break;
        case LEX_END:
            if(cur->p1[0] != 0) {
                entry_point = get_const(cur->p1);
                entry_point_def = 1;
            }
            stop = 1;
            break;
        case LEX_EQU:
            add_const(cur->label, CONST_EXPR, get_const(cur->p1));
            break;
        case LEX_NONE:
            out_msg("Syntax error", 0);
            stop = 1;
            break;
        default:
            if(cur_section != SECTION_TEXT) {
                out_msg("Instruction outside .text section", 0);
                break;
            }
            old_outptr = outptr;

            cinstr = &instr86[prescan];

            param_type[0] = cur->pcount > 0 ? get_type(param[0]) : 0;
            param_type[1] = cur->pcount > 1 ? get_type(param[1]) : 0;

            lex2 = cur->lex2;
            found = 0;

            while(cinstr->lex1 != LEX_NONE && !found) {
                if(lex1 != cinstr->lex1) {
                    break;
                }
                /* If user wrote SHORT/NEAR/FAR, require the table entry's
                 * lex2 to match exactly. If user didn't (lex2==LEX_NONE),
                 * only skip table entries that restrict lex2. */
                if(lex2 != LEX_NONE) {
                    if(cinstr->lex2 != lex2) {
                        cinstr++;
                        continue;
                    }
                } else if(cinstr->lex2 != LEX_NONE) {
                    cinstr++;
                    continue;
                }
                if((cur->pcount != cinstr->params) || !match_params(cinstr, cur->pcount)) {
                    cinstr++;
                    continue;
                }
                found = 1;
                do_instruction(cinstr);
                break;
            }
            if(!found) {
                out_msg("Syntax error", 0);
            }
        }
    }
    free(line);
    free(a1);
    fclose(infile);
    return 1;
}

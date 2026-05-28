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

#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <ctype.h>
#include "MSA2.H"
#include "EXPR.H"
#include "LEX.H"

int hashCode(const char *str) {
    int result = 0;

    while(*str) {
        result = result * 31 + *str;
        str++;
    }
    return result;
}

inline char is_numeric(char c) {
    return c >= '0' && c <= '9';
}

inline char is_az(char c) {
    return c >= 'A' && c <= 'Z';
}

inline char is_ident(char c) {
    return is_numeric(c) || is_az(c) || c == '_' || c == '$' || c == '.';
}

inline char is_math(char c) {
    return c == '+' || c == '-' || c == '*' || c == '/' || c == '%';
}

inline char is_spc(char c) {
    return c && (c <= ' ');
}

void out_msg(const char *s, int x) {
    if(x == 0) {
        errors++;
    } else {
        warnings++;
    }
    if(quiet >= x) {
        printf("%s:%s:%ld: %s\n", (x == 0 ? "ERROR" : "WARN"), inputname, linenr, s);
    }
}

void out_msg_str(const char *s, int x, const char *param) {
    sprintf(err_msg, s, param);
    out_msg(err_msg, x);
}

void out_msg_chr(const char *s, int x, char c) {
    sprintf(err_msg, s, c);
    out_msg(err_msg, x);
}

void out_msg_int(const char *s, int x, int i) {
    sprintf(err_msg, s, i);
    out_msg(err_msg, x);
}

void build_address(t_address *a) {
    if(a->mod == 3) {
        a->op_len = 1;
        a->op[0] = (a->mod << 6) + (a->reg << 3) + a->rm;
        return;
    }
    if(a->mod < 3) {
        a->op_len = a->mod + 1;
        if(a->mod == 0 && a->rm == 6) {
            a->op_len = 3;
        }
    }
    a->op[0] = (a->mod << 6) + (a->reg << 3) + a->rm;
    if(a->mod == 1) {
        a->op[1] = a->disp;
    } else if(a->mod == 2 || (a->mod == 0 && a->rm == 6)) {
        a->op[1] = (a->disp) & 0xff;
        a->op[2] = (a->disp >> 8) & 0xff;
    }
}

/* Return the zero-based index of the register `s` in the lookup table
 * `k` (which is a flat string of 2-char register names). The return is
 * `int` (not `char`) and the sentinel for "not found" is -1; previous
 * versions returned `char` and used 255, which sign-extended to -1 on
 * signed-char hosts and broke get_type() unless the build forced
 * -funsigned-char. Using a wide return + signed sentinel removes the
 * platform-specific workaround. */
int is_reg(const char *s, const char *k) {
    int i;
    char c1, c2, r1, r2;

    c1 = *s;
    s++;
    c2 = *s;
    s++;
    if(*s != 0) {
        return -1;
    }

    i = 0;
    while((r1 = *k)) {
        k++;
        r2 = *k;
        k++;
        if((c1 == r1) && (c2 == r2)) {
            return i;
        }
        i++;
    }
    return -1;
}

int get_type(const char* s) {
    int i = is_reg(s, "ALCLDLBLAHCHDHBHAXCXDXBXSPBPSIDIESCSSSDS");

    if(i >= 0 && i < 8)  return ACC_8  + i;
    if(i >= 8 && i < 16) return ACC_16 + i - 8;
    if(i >= 16)          return SEG    + i - 16;

    if(s[4] == '[') {
        if(!memcmp(s,"BYTE[", 5)) return MEM_8;
        if(!memcmp(s,"WORD[", 5)) return MEM_16;
    }
    if(*s == '[') return MEM_16;
    return IMM;
}

char inline bindigit(char c) {
    if(c == '0' || c == '1') return c - '0';
    out_msg_chr("Invalid binary digit %c", 0, c);
    return 0;
}

char inline decdigit(char c) {
    if(c >= '0' && c <= '9') return c - '0';
    out_msg_chr("Invalid decimal digit %c", 0, c);
    return 0;
}

char inline hexdigit(char c) {
    if(c >= '0' && c <= '9') return c - '0';
    if(c >= 'A' && c <= 'F') return c - 'A' + 10;
    out_msg_chr("Invalid hex digit %c", 0, c);
    return 0;
}

char inline is_hex(char c) {
    if(c >= '0' && c <= '9') return 1;
    if(c >= 'A' && c <= 'F') return 1;
    return 0;
}

char *get_dword(char *s, long int *value) {
    char c = s[1];

    *value = 0;
    if(c == 'X' && *s == '0') {
        s += 2;
        while(is_hex(c = *s)) {
            *value = ((*value) << 4) | hexdigit(c);
            s++;
        }
    } else if(c == 'B' && *s == '0') {
        s += 2;
        c = *s;
        while(c == '0' || c == '1') {
            *value = ((*value) << 1) | (c - '0');
            s++;
            c = *s;
        }
    } else {
        c = *s;
        while(c >= '0' && c <= '9') {
            *value = ((*value) * 10) + (c - '0');
            s++;
            c = *s;
        }
    }
    return s;
}

int get_number(char* s) {
    char c1, c2;
    int value = 0;

    c1 = s[0];
    c2 = s[1];
    if(c1 == '0') {
        if(c2 == 'X') {
            s += 2;
            while((c1 = *s)) {
                value = (value << 4) | hexdigit(c1);
                s++;
            }
            return value;
        } else if(c2 == 'B') {
            s += 2;
            while((c1 = *s)) {
                value = (value << 1) | bindigit(c1);
                s++;
            }
            return value;
        }
    }
    while((c1 = *s)) {
        value = (value * 10) + decdigit(c1);
        s++;
    }
    return value;
}

int get_const(const char* s) {
    int j;
    char tmp[32], sign;
    int value, x;
    t_constant *c;

    value = 0;

    while(*s) {
        if(is_math(sign = *s)) {
            s++;
        } else {
            sign = '+';
        }
        if(*s == '\'') {
            s++;
            x = *s;
            s++;
            s++;
        } else {
            j = 0;
            while(is_ident(*s)) {
                tmp[j] = *s;
                j++;
                s++;
            }
            tmp[j] = 0;
            x = 0;
            if(is_numeric(tmp[0])) {
                x = get_number(tmp);
            } else {
                if((c = find_const(tmp)) != NULL) {
                    /* Imports have no value at assemble time; the linker
                     * patches the real address. Reading them here yields
                     * 0 so the patch site holds the standard addend. */
                    x = CONST_TYPE(c) == CONST_IMPORT ? 0 : c->value;
                } else {
                    if(pass) {
                        out_msg_str("Undefined constant '%s'", 1, tmp);
                    }
                    return 0x00;
                }
            }
        }
        switch(sign) {
        case '+':
            value += x;
            break;
        case '-':
            value -= x;
            break;
        case '*':
            value *= x;
            break;
        case '/':
            if(x == 0) {
                out_msg("Division by zero in constant expression", 0);
                break;
            }
            value /= x;
            break;
        case '%':
            if(x == 0) {
                out_msg("Modulo by zero in constant expression", 0);
                break;
            }
            value = value % x;
            break;
        default:
            out_msg_chr("Unknown math operation '%c'", 0, sign);
            break;
        }
    }

    return value;
}

inline char *skip_until(char *s, char c) {
    while((*s != c) && *s) {
        s++;
    }
    if(*s) {
        s++;
    }
    return s;
}

int get_address(t_address* a, char* s) {
    int i = 0, k;
    char c1, j1, j2;
    byte seg_pre;

    a->disp_src = NULL;
    i = is_reg(s, "ALCLDLBLAHCHDHBHAXCXDXBXSPBPSIDI");

    if(i >= 0 && i < 16) {
        a->rm = i < 8 ? i : i - 8;
        a->mod = 3;
        return 0;
    }

    s = skip_until(s, '[');

    if(*s == 0) {
        return 1;
    }
    i = 0;

    if(s[2] == ':') {
        c1 = *s;
        if(s[1] != 'S') {
            out_msg("Syntax error in segment register name", 0);
            seg_pre = 0x90;
        } else if(c1 == 'C') {
            seg_pre = 0x2e;
        } else if(c1 == 'D') {
            seg_pre = 0x3e;
        } else if(c1 == 'E') {
            seg_pre = 0x26;
        } else if(c1 == 'S') {
            seg_pre = 0x36;
        } else {
            out_msg("Syntax error in segment register name", 0);
            seg_pre = 0x90;
        }

        for(k = old_outptr + 4; k > old_outptr; k--) {
            outprog[k] = outprog[k-1];
        }
        s += 3;
        outprog[old_outptr] = seg_pre;
        outptr++;
    }
    c1 = s[2] == '+';
    j1 = *s;
    j2 = *(s + 1);
    if(c1 && !memcmp(s, "BX+SI", 5)) {
        a->rm = 0;
        s += 5;
    } else if(c1 && !memcmp(s, "BX+DI", 5)) {
        a->rm = 1;
        s += 5;
    } else if(c1 && !memcmp(s, "BP+SI", 5)) {
        a->rm = 2;
        s += 5;
    } else if(c1 && !memcmp(s, "BP+DI", 5)) {
        a->rm = 3;
        s += 5;
    } else if(j1 == 'S' && j2 == 'I') {
        a->rm = 4;
        s += 2;
    } else if(j1 == 'D' && j2 == 'I') {
        a->rm = 5;
        s += 2;
    } else if(j1 == 'B' && j2 == 'P') {
        a->rm = 6;
        s += 2;
    } else if(j1 == 'B' && j2 == 'X') {
        a->rm = 7;
        s += 2;
    } else {
        a->mod = 0;
        a->rm = 6;
        s[strlen(s) - 1] = 0;
        a->disp = get_const(s);
        a->disp_src = s;
        return 0;
    }

    if(is_math(*s)) {
        s[strlen(s) - 1] = 0;
        a->disp = get_const(s);
        a->disp_src = s;
        a->mod = a->disp < 0x80 ? 1 : 2;
    } else {
        a->mod = 0;
    }

    if((a->rm == 6) && (a->mod == 0)) {
        a->disp = 0;
        a->mod = 1;
    }

    return 0;
}

void *msa_malloc(size_t s) {
    void *r;
    if((r = malloc(s)) == NULL) {
        out_msg("Could not allocate memory", 0);
        done(4);
    }
    return r;
}

char *get_param(char *dest, char *line) {
    char c;
    while((c = *line)) {
        if(c == ',') {
            break;
        } else if(c == ' ') {
            line++;
        } else if(c == '\'' || c == '"') {
            *dest = c;
            dest++;
            line++;
            while((*line) && (*line != c)) {
                *dest = *line;
                dest++;
                line++;
            }
            if(*line) {
                *dest = *line;
                dest++;
                line++;
            }
        } else {
            *dest = c;
            dest++;
            line++;
        }
    }
    *dest = 0;
    return line;
}

void split_line(t_line *cur, char *line, char *a1) {
    char *p, *p2, c, c1, c2, c3, c4, c5;
    char full_param;
    p = line;
    p2 = a1;

    cur->p1[0] = 0;
    cur->p2[0] = 0;
    full_param = 0;
    cur->lex2 = LEX_NONE;
    while((c = *p)) {
        if((c == ' ') || (c == ':')) {
            break;
        }
        *p2 = c;
        p++;
        p2++;
    }
    *p2 = 0;
    while((c = *p) && (c == ' ')) {
        p++;
    }
    if(c == ':') {
        strcpy(cur->label, a1);
        cur->has_label = 1;
        line = p + 1;
    } else {
        c1 = *p;
        c2 = *(p+1);
        c3 = *(p+2);
        c4 = *(p+3);
        if(c1 == 'E' && c2 == 'Q' && c3 == 'U' && c4 == ' ') {
            strcpy(cur->label, a1);
            full_param = 1;
            line = p;
        }
    }
    if(!*line) {
        return;
    }
    while((c = *line) && (c == ' ')) {
        line++;
    }

    c1 = line[0];
    c2 = line[1];
    c3 = line[2];
    c4 = line[3];
    c5 = line[4];

    if((c1 == 'L' && c2 == 'O' && c3 == 'C' && c4 == 'K' && c5 == ' ')) {
        cur->has_lock = 1;
        line += 5;
    }

    c1 = line[0];
    c2 = line[1];
    c3 = line[2];
    c4 = line[3];
    c5 = line[4];

    if((c1 == 'R' && c2 == 'E' && c3 == 'P')) {
        if(c4 == ' ') {
            cur->rep_type = LEX_REP;
            line += 4;
        } else if((c4 == 'Z' || c4 == 'E') && c5 == ' ') {
            cur->rep_type = LEX_REP;
            line += 5;
        } else if((c4 == 'N') && (c5 == 'Z' || c5 == 'E')) {
            cur->rep_type = LEX_REPNZ;
            line += 6;
        }
    }

    p = cur->cmd;
    while((c = toupper(*line)) && (c > ' ')) {
        *p = c;
        line++;
        p++;
    }

    *p = 0;

    while((c = *line) && (c == ' ')) {
        line++;
    }

    if(!*line) {
        return;
    }

    if(!full_param) {
        c1 = cur->cmd[0];
        c2 = cur->cmd[1];
        c3 = cur->cmd[2];
        if((c3 == 0) && (c1 == 'R' || c1 == 'D')) {
            if(c2 == 'B' || c2 == 'W' || c3 == 'D') {
                full_param = 1;
            }
        }
        /* IMPORT takes a comma-separated symbol list; treat the rest of
         * the line as one parameter so the dispatch can split on commas. */
        if(!full_param && !strcmp(cur->cmd, "IMPORT")) {
            full_param = 1;
        }
    }

    if(full_param) {
        p = cur->p1;
        while((c = *line)) {
            if(c == ' ') {
                line++;
            } else if(c == '\'' || c == '"') {
                *p = c;
                p++;
                line++;
                while((*line) && (*line != c)) {
                    *p = *line;
                    p++;
                    line++;
                }
                if(*line) {
                    *p = *line;
                    p++;
                    line++;
                }
            } else {
                *p = c;
                p++;
                line++;
            }
        }
        *p = 0;
        cur->pcount = 1;
        return;
    }
    if(!memcmp(line, "SHORT ", 6)) {
        cur->lex2 = LEX_SHORT;
        line += 6;
    } else if(!memcmp(line, "NEAR ", 5)) {
        cur->lex2 = LEX_NEAR;
        line += 5;
    } else if(!memcmp(line, "FAR ", 4)) {
        cur->lex2 = LEX_FAR;
        line += 4;
    }

    line = get_param(a1, line);
    if(*a1 != 0) {
        strcpy(cur->p1, a1);
        cur->pcount++;
    }
    if(*line == ',') {
        line++;
        line = get_param(a1, line);
        strcpy(cur->p2, a1);
        cur->pcount++;
    }
}

char strip_line(char *line) {
    char *line_b, *line_e, *p, *p2, c;
    int error = 0;

    line_b = line;
    while(is_spc(*line_b)) {
        line_b++;
    }
    line_e = line_b;
    while((c = *line_e) && !error) {
        if(c == '\'' || c == '"') {
            line_e++;
            line_e = skip_until(line_e, c);
            if(!*line_e) {
                error = 1;
            }
        } else if(c < ' ') {
            *line_e = ' ';
        } else if(c == ';') {
            *line_e = 0;
            break;
        } else {
            line_e++;
        }
    }
    if(error) {
        return 0;
    }
    p = line_b - 1;
    while(p != line_e) {
        if(*line_e > ' ') {
            break;
        }
        *line_e = 0;
        line_e--;
    }
    p = line;
    p2 = line_b;
    while((c = *p2)) {
        if(c == '"' || c == '\'') {
            *p = *p2;
            p++;
            p2++;
            while((*p2) && (*p2 != c)) {
                *p = *p2;
                p++;
                p2++;
            }
            if(*p2) {
                *p = *p2;
                p++;
                p2++;
            }
        } else if(c == ' ') {
            *p = *p2;
            p++;
            p2++;
            while((c = *p2) && (c == ' ')) {
                p2++;
            }
        } else {
            *p = toupper(*p2);
            p++;
            p2++;
        }
    }
    *p = 0;
    return 1;
}

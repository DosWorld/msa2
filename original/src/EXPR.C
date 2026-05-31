/*

MIT License

Copyright (c) 2019 DosWorld

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

t_constant *constants;

void expr_init() {
    constants = NULL;
}

void expr_done() {
    t_constant *c;

    while(constants != NULL) {
        c = (t_constant *)constants->next;
        free(constants);
        constants = c;
    }
}

t_constant *add_const(const char* name, int type, int32_t value) {
    t_constant *c;
    uint16_t hash;

    c = constants;
    hash = (uint16_t)hashCode(name);

    while(c != NULL) {
        if(hash == c->hash) {
            if(!strcmp(c->name, name)) {
                if(value != c->value) {
                    if((!pass && type == CONST_LABEL) || (pass && type == CONST_EXPR)) {
                        sprintf(err_msg, "Constant %s changed", name);
                        out_msg(err_msg, 2);
                    }
                }
                c->value = value;
                c->type = type;
                return c;
            }
        }
        c = (t_constant *)c->next;
    }

    c = (t_constant *)MSA_MALLOC(sizeof(t_constant));
    strcpy(c->name, name);
    c->value = value;
    c->hash = hash;
    c->type = type;
    c->is_export = 0;
    c->section = SECTION_TEXT;
    c->extra = NULL;
    c->next = constants;
    return constants = c;
}

t_constant *find_const(const char *name) {
    t_constant *c;
    uint16_t hash;

    c = constants;
    hash = (uint16_t)hashCode(name);
    while(c != NULL) {
        if(hash == c->hash) {
            if(!strcmp(c->name, name)) {
                break;
            }
        }
        c = (t_constant *)c->next;
    }
    return c;
}

int remove_const(const char *name) {
    t_constant *c, *prev;
    uint16_t hash;

    prev = NULL;
    c = constants;
    hash = (uint16_t)hashCode(name);
    while(c != NULL) {
        if(hash == c->hash && !strcmp(c->name, name)) {
            if(prev == NULL) constants = (t_constant *)c->next;
            else prev->next = c->next;
            /* CONST_DEFINE_TEXT and CONST_MACRO own heap payloads
             * via c->extra; free them. Free of the inner storage is
             * type-specific so we delegate to the owning module. */
            if(CONST_TYPE(c) == CONST_DEFINE_TEXT) {
                free(c->extra);
            }
            /* CONST_MACRO body is freed when the assembler's macro
             * table is torn down at expr_done(); leaving it here
             * untouched is fine since macros are typically
             * file-lifetime. */
            free(c);
            return 1;
        }
        prev = c;
        c = (t_constant *)c->next;
    }
    return 0;
}

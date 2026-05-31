/*

MIT License

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

Preprocessor implementation.

%define bodies hang off t_constant.extra (CONST_DEFINE_TEXT entries);
%macro bodies hang off t_constant.extra (CONST_MACRO entries) as a
t_macrobody pointer with a linked list of body lines. Both are
managed by EXPR.C's add_const/remove_const; we just attach the
extra pointer.

Runtime state -- macro expansion stack, include file stack, last-
global save stack -- are linked lists with no compile-time caps.
A small depth cap on includes (8) catches cyclic %include chains
that would otherwise blow the host stack.

The 4 KB expansion-substitution buffer lives in module-level static
storage rather than on the stack. DOS Watcom's default stack is
~4 KB; a per-call 4 KB frame inside pp_macro_invoke() would push the
stack to the edge on every macro call. Macro expansion is single-
threaded and pp_macro_invoke() does not re-enter itself, so a single
static buffer is safe.

*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <ctype.h>
#include "MSA2.H"
#include "EXPR.H"
#include "MACRO.H"

/* Module-level state. ASSEMBLR.C does not touch these directly --
 * everything flows through the pp_* functions exposed by MACRO.H. */
static int macro_invocation_id;  /* monotonic, for %% label uniqueness */

typedef struct t_pending_line {
    char *text;
    struct t_pending_line *next;
} t_pending_line;
static t_pending_line *macro_stack_head;

typedef struct t_macro_save {
    char saved[64];
    struct t_macro_save *next;
} t_macro_save;
static t_macro_save *macro_save_head;

#define MACRO_END_SENTINEL "%%MACRO_END%%"

/* Runaway-recursion guard for %macro invocations. A macro that
 * invokes itself (directly or via another macro) would keep pushing
 * body lines and save frames until heap is exhausted. The save-stack
 * length equals the number of currently-in-flight expansions, so we
 * cap it. 64 matches the %define recursion guard in get_const(). */
#define MACRO_RECURSION_MAX 64

#define INCLUDE_MAX_DEPTH 8
typedef struct t_include_frame {
    FILE *file;
    long  saved_linenr;
    char *saved_inputname;
    struct t_include_frame *next;
} t_include_frame;
static FILE *current_file;
static t_include_frame *include_stack_head;
static int include_depth;

/* P1: macro-substitution buffer in static storage instead of on the
 * stack. pp_macro_invoke() never re-enters itself during a single
 * invocation (substitution is straight string copy), so one buffer
 * is enough. Sized to match the per-pending-line cap elsewhere. */
#define MACRO_EXPAND_BUF 4096
static char macro_expand_buf[MACRO_EXPAND_BUF];

/* ----- expansion-stack helpers ---------------------------------- */

static int macro_push_line(const char *line) {
    t_pending_line *p = (t_pending_line *)MSA_MALLOC(sizeof(t_pending_line));
    p->text = (char *)MSA_MALLOC(strlen(line) + 1);
    strcpy(p->text, line);
    p->next = macro_stack_head;
    macro_stack_head = p;
    return 1;
}

void pp_push_line(const char *line) {
    macro_push_line(line);
}

/* Substitute %1..%9 with the corresponding arg, %%label with the
 * invocation-specific prefix @@<id>.label. Writes into out (size out_sz).
 * Result is always NUL-terminated. */
static void macro_substitute(const char *src, char *out, size_t out_sz,
                             char **args, int argc, int invoc_id) {
    size_t o = 0;
    while(*src && o < out_sz - 1) {
        if(src[0] == '%' && src[1] == '%' && isalpha((unsigned char)src[2])) {
            int n = snprintf(out + o, out_sz - o, "@@%d.", invoc_id);
            if(n < 0 || (size_t)n >= out_sz - o) break;
            o += (size_t)n;
            src += 2;
            while(*src && (isalnum((unsigned char)*src) || *src == '_')) {
                if(o >= out_sz - 1) break;
                out[o++] = *src++;
            }
            continue;
        }
        if(src[0] == '%' && src[1] >= '1' && src[1] <= '9') {
            int i = src[1] - '1';
            if(i < argc && args[i]) {
                size_t alen = strlen(args[i]);
                if(o + alen >= out_sz) break;
                memcpy(out + o, args[i], alen);
                o += alen;
            }
            src += 2;
            continue;
        }
        out[o++] = *src++;
    }
    out[o] = 0;
}

/* ----- macro body slurp / invoke -------------------------------- */

int pp_macro_slurp(char *buf, int sz, const char *name, int argc) {
    t_macrobody *mb;
    t_macroline *tail = NULL;
    mb = (t_macrobody *)MSA_MALLOC(sizeof(t_macrobody));
    mb->argc = argc;
    mb->lines = NULL;
    while(fgets(buf, sz, current_file)) {
        t_macroline *ln;
        linenr++;
        strip_line(buf);
        if(!memcmp(buf, "%ENDMACRO", 9) && (buf[9] == 0 || buf[9] == ' ')) {
            t_constant *c = add_const(name, CONST_MACRO, 0);
            c->extra = mb;
            return 1;
        }
        if(!memcmp(buf, "%MACRO ", 7)) {
            out_msg("Nested %macro is not supported", 0);
            return 0;
        }
        ln = (t_macroline *)MSA_MALLOC(sizeof(t_macroline));
        ln->text = (char *)MSA_MALLOC(strlen(buf) + 1);
        strcpy(ln->text, buf);
        ln->next = NULL;
        if(tail == NULL) mb->lines = ln;
        else tail->next = ln;
        tail = ln;
    }
    out_msg("%macro without %endmacro", 0);
    return 0;
}

void pp_skip_macro_body(char *buf, int sz) {
    while(fgets(buf, sz, current_file)) {
        linenr++;
        strip_line(buf);
        if(!memcmp(buf, "%ENDMACRO", 9)) break;
    }
}

void pp_macro_invoke(t_macrobody *mb, const char *name, char *argstr) {
    char *args[9];
    int argc = 0;
    int invoc_id;
    int depth;
    char parent[64];
    t_macroline *ln;
    t_macro_save *save;
    int nlines = 0;

    for(ln = mb->lines; ln != NULL; ln = ln->next) nlines++;

    if(argstr) {
        char *p = argstr;
        while(*p && argc < 9) {
            char *start;
            while(*p == ' ') p++;
            start = p;
            while(*p && *p != ',') p++;
            args[argc++] = start;
            if(*p == ',') {
                *p = 0;
                p++;
            }
        }
    }
    if(argc != mb->argc) {
        out_msg_int("Macro arg count mismatch (expected %d)", 0, mb->argc);
        return;
    }

    /* Recursion guard: the save-stack length is the count of macro
     * invocations currently in flight. Cap at MACRO_RECURSION_MAX so
     * a self-invoking macro (or A->B->A cycle) emits a diagnostic
     * instead of growing the expansion stack until heap is exhausted. */
    depth = 0;
    {
        t_macro_save *s;
        for(s = macro_save_head; s != NULL; s = s->next) depth++;
    }
    if(depth >= MACRO_RECURSION_MAX) {
        out_msg_str("Macro recursion of '%s' too deep (cycle?)", 0, name);
        return;
    }

    invoc_id = ++macro_invocation_id;

    /* Save current last_global; install @@<id> as parent for local
     * labels inside the expansion. Linked save-stack -- no fixed cap. */
    save = (t_macro_save *)MSA_MALLOC(sizeof(t_macro_save));
    strncpy(save->saved, last_global, sizeof(save->saved) - 1);
    save->saved[sizeof(save->saved) - 1] = 0;
    save->next = macro_save_head;
    macro_save_head = save;
    snprintf(parent, sizeof(parent), "@@%d", invoc_id);
    strncpy(last_global, parent, sizeof(last_global) - 1);
    last_global[sizeof(last_global) - 1] = 0;

    /* Push end-sentinel first so it pops last (after all body lines). */
    macro_push_line(MACRO_END_SENTINEL);

    /* Push body lines in reverse so they pop in source order. We use
     * a small array on the heap (no caps, no stack pressure on DOS).
     * macro_expand_buf is shared and module-static -- the loop below
     * is sequential, so reusing it is safe. */
    if(nlines > 0) {
        t_macroline **frame = (t_macroline **)MSA_MALLOC(nlines * sizeof(t_macroline *));
        int i = 0;
        for(ln = mb->lines; ln != NULL; ln = ln->next) frame[i++] = ln;
        for(i = nlines - 1; i >= 0; i--) {
            macro_substitute(frame[i]->text, macro_expand_buf,
                             sizeof(macro_expand_buf),
                             args, argc, invoc_id);
            macro_push_line(macro_expand_buf);
        }
        free(frame);
    }
}

/* ----- file / include layer ------------------------------------- */

int pp_open_source(const char *fname) {
    current_file = fopen(fname, "rb");
    if(current_file == NULL) {
        out_msg("Can't open input file", 0);
        return 0;
    }
    return 1;
}

int pp_include(const char *path) {
    FILE *nf;
    t_include_frame *frame;
    char *namecopy;
    if(include_depth >= INCLUDE_MAX_DEPTH) {
        out_msg("%include nested too deep (limit 8)", 0);
        return 0;
    }
    nf = fopen(path, "rb");
    if(nf == NULL) {
        out_msg_str("Cannot open included file '%s'", 0, path);
        return 0;
    }
    /* Save the current frame and inputname. inputname may be the
     * caller's argv pointer (for the outermost source) or a heap copy
     * from a deeper include; the pop path always restores whatever
     * was saved, and the closing pp_close_source() unwinds frames. */
    frame = (t_include_frame *)MSA_MALLOC(sizeof(t_include_frame));
    frame->file = current_file;
    frame->saved_linenr = linenr;
    frame->saved_inputname = inputname;
    frame->next = include_stack_head;
    include_stack_head = frame;

    namecopy = (char *)MSA_MALLOC(strlen(path) + 1);
    strcpy(namecopy, path);
    inputname = namecopy;
    current_file = nf;
    linenr = 0;
    include_depth++;
    return 1;
}

int pp_next_line(char *buf, int sz) {
    while(macro_stack_head != NULL) {
        t_pending_line *p = macro_stack_head;
        macro_stack_head = p->next;
        if(!strcmp(p->text, MACRO_END_SENTINEL)) {
            free(p->text);
            free(p);
            if(macro_save_head != NULL) {
                t_macro_save *s = macro_save_head;
                macro_save_head = s->next;
                strncpy(last_global, s->saved, sizeof(last_global) - 1);
                last_global[sizeof(last_global) - 1] = 0;
                free(s);
            }
            continue;
        }
        strncpy(buf, p->text, sz - 1);
        buf[sz - 1] = 0;
        free(p->text);
        free(p);
        return 1;
    }
    while(current_file != NULL) {
        if(fgets(buf, sz, current_file)) return 1;
        if(include_stack_head == NULL) return 0;
        {
            t_include_frame *f = include_stack_head;
            include_stack_head = f->next;
            fclose(current_file);
            current_file = f->file;
            linenr = f->saved_linenr;
            free(inputname);
            inputname = f->saved_inputname;
            free(f);
            include_depth--;
        }
    }
    return 0;
}

void pp_close_source(void) {
    if(current_file != NULL) {
        fclose(current_file);
        current_file = NULL;
    }
    while(include_stack_head != NULL) {
        t_include_frame *f = include_stack_head;
        include_stack_head = f->next;
        if(f->file != NULL) fclose(f->file);
        free(f->saved_inputname);
        free(f);
    }
    include_depth = 0;
}

void pp_reset_pass(void) {
    while(macro_stack_head != NULL) {
        t_pending_line *p = macro_stack_head;
        macro_stack_head = p->next;
        free(p->text);
        free(p);
    }
    while(macro_save_head != NULL) {
        t_macro_save *s = macro_save_head;
        macro_save_head = s->next;
        free(s);
    }
    macro_invocation_id = 0;
}

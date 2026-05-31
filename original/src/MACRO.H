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

Preprocessor: %define, %macro, %include, plus the source-line reader that
weaves macro expansions and included files into the main parse loop.

*/

#ifndef _MACRO_H_
#define _MACRO_H_

/* t_macrobody is the value stored on t_constant.extra for CONST_MACRO
 * entries. Fields are accessed by ASSEMBLR.C's invocation dispatch via
 * pp_macro_invoke(); the body line list is otherwise private. */
typedef struct t_macroline {
    char *text;
    struct t_macroline *next;
} t_macroline;

typedef struct {
    int argc;
    t_macroline *lines;  /* head = first body line */
} t_macrobody;

/* Open the initial source file. Sets current file state. Returns 1 on
 * success, 0 on failure (after emitting the diagnostic). */
extern int  pp_open_source(const char *fname);

/* Close every open file and free per-pass preprocessor state.
 * Called once at the end of each pass from assemble(). */
extern void pp_close_source(void);

/* Reset macro expansion stack, include stack, last-global save stack,
 * and invocation counter. Called at the top of each pass. */
extern void pp_reset_pass(void);

/* Read the next logical source line. Precedence:
 *   1. pending macro-expansion lines (LIFO);
 *   2. fgets() from the currently-active file;
 *   3. on EOF, pop a suspended file from the include stack.
 * Updates the global linenr and inputname on pop so diagnostics point
 * at the file the line came from. Returns 1 on a line read, 0 on EOF. */
extern int  pp_next_line(char *buf, int sz);

/* %include "path": push the current file/linenr/inputname and switch
 * to `path`. Capped at 8 levels of nesting (cyclic-include guard). */
extern int  pp_include(const char *path);

/* %macro NAME N ... %endmacro: slurp body lines from the current file
 * up to %endmacro and register `name` as CONST_MACRO. Used by the
 * directive dispatch on pass 0; pass 1 uses pp_skip_macro_body. */
extern int  pp_macro_slurp(char *buf, int sz, const char *name, int argc);

/* Pass-1 helper: consume the body of an already-registered macro
 * (no re-registration) so linenr stays correct. */
extern void pp_skip_macro_body(char *buf, int sz);

/* Expand a macro invocation. `name` is the macro's name (used only
 * for diagnostics; pass the symbol-table key the caller used).
 * `mb` is the body (CONST_MACRO entry's extra pointer). `argstr`
 * is the post-cmd remainder of the source line (may be NULL or
 * empty for 0-arg macros). */
extern void pp_macro_invoke(t_macrobody *mb, const char *name, char *argstr);

/* Push a synthetic source line onto the top of the read stack. Used
 * by ASSEMBLR.C to inject a "LABEL:" line in front of an expanded
 * macro body when the user wrote `LABEL: MACROCALL`. */
extern void pp_push_line(const char *line);

#endif

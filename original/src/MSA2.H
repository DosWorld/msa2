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

#ifndef _MSA2_H_
#define _MSA2_H_

#define MAIN_VERSION    1
#define SUB_VERSION     0
#define BUILD           19
#define PROG_NAME       "MSA2"

#define MAX_LABEL 32

#define TARGET_UNDEF 0
#define TARGET_BIN 1
#define TARGET_COM 2
#define TARGET_TEXE 3
#define TARGET_RDF 4

#define SECTION_TEXT 0
#define SECTION_DATA 1
#define SECTION_BSS  2

#define MSA_MALLOC(x) msa_malloc(x)

typedef uint8_t byte;
typedef uint16_t word;
typedef uint32_t dword;

#define IMM                     0X00
#define REG_8                   0X01
#define REG_16                  0X02
#define REG_SEG                 0X03
#define MEM_8                   0X04
#define MEM_16                  0X05
#define RM_8                    0X06
#define RM_16                   0X07
#define ACC_8                   0X10
#define AL                      0X10
#define CL                      0X11
#define DL                      0X12
#define BL                      0X13
#define AH                      0X14
#define CH                      0X15
#define DH                      0X16
#define BH                      0X17
#define ACC_16                  0X20
#define AX                      0X20
#define CX                      0X21
#define DX                      0X22
#define BX                      0X23
#define SP                      0X24
#define BP                      0X25
#define SI                      0X26
#define DI                      0X27
#define SEG                     0X30
#define ES                      0X30
#define CS                      0X31
#define SS                      0X32
#define DS                      0X33

#define OP_CMD_OP               0X01
#define OP_CMD_IMM8             0X02
#define OP_CMD_IMM16            0X03
#define OP_CMD_PLUSREG8         0X04
#define OP_CMD_PLUSREG16        0X05
#define OP_CMD_PLUSREGSEG       0X06
#define OP_CMD_RM1_8            0X07
#define OP_CMD_RM1_16           0X08
#define OP_CMD_RM2_8            0X09
#define OP_CMD_RM2_16           0X0A
#define OP_CMD_RMLINE_8         0X0B
#define OP_CMD_RMLINE_16        0X0C
#define OP_CMD_REL8             0X0D
#define OP_CMD_REL16            0X0E
#define OP_CMD_RM1_SEG          0X0F
#define OP_CMD_RM2_SEG          0X10
#define OP_CMD_FAR_PTR          0X11

#define CONST_LABEL 1
#define CONST_EXPR 2
#define CONST_BSS 3
#define CONST_DATA 4
#define CONST_IMPORT 5
#define CONST_DEFINE_TEXT 6  /* %define body stored in define_bodies[value] */
#define CONST_MACRO 7        /* %macro body stored in macro_bodies[value] */

#define CONST_TYPE(x) ((x)->type & 0x7f)

typedef struct {
    byte    mod;
    byte    reg;
    byte    rm;
    int32_t disp;
    byte    op_len;
    byte    op[0x06];
    const char *disp_src; /* original expression for disp, or NULL */
} t_address;

typedef struct {
    char name[64];
    char is_export;
    uint16_t hash;  /* 16-bit string hash; gate strcmp in find_const */
    char type;
    char section;   /* SECTION_TEXT / SECTION_DATA / SECTION_BSS, for CONST_LABEL/CONST_DATA/CONST_BSS */
    int32_t value;
    void *extra;    /* CONST_DEFINE_TEXT -> char *body; CONST_MACRO -> t_macrobody*; else NULL */
    void *next;
} t_constant;

typedef struct {
    int    lex1, lex2;
    int    params;
    int    param_type[4];
    int    op[10];
} t_instruction;

typedef struct {
    dword lnum;
    char has_label;
    char pcount;
    char has_lock;
    int rep_type;
    int lex2;
    char label[MAX_LABEL];
    char cmd[16];
    char p1[512];
    char p2[512];
} t_line;

/* RDF relocation record (in-memory; serialised by RDF.C). */
typedef struct t_reloc {
    byte    seg;          /* segment being patched: SECTION_TEXT or SECTION_DATA */
    dword   ofs;          /* offset within that segment */
    byte    width;        /* 1, 2, 4 */
    word    rseg;         /* target reference segment: 0=code,1=data,2=bss */
    byte    relative;     /* nonzero => bit 6 (0x40) of RELOC flags */
    byte    is_segreloc;  /* nonzero => emit RDFREC_SEGRELOC (type 6) instead of RDFREC_RELOC */
    struct t_reloc *next;
} t_reloc;

extern t_instruction instr86[];

extern int assemble(char* fname);

extern void out_msg(const char *s, int x);
extern void out_msg_str(const char *s, int x, const char *param);
extern void out_msg_chr(const char *s, int x, char c);
extern void out_msg_int(const char *s, int x, int i);

extern void build_address(t_address* a);

extern int get_type(const char* s);
extern int hashCode(const char *str);
extern int32_t get_number(char* s);
extern int get_address(t_address* a, char* s);

/* NASM-style local labels: if raw starts with '.', produce
 * "<last_global><raw>"; else copy raw verbatim. `out` must hold at
 * least 64 bytes (matches t_constant.name). Emits an error if raw is
 * local but no non-local parent has been seen since the last reset. */
extern void qualify_label(const char *raw, char *out);
extern char last_global[64];

/* %define body now lives directly on t_constant.extra (char *) for
 * CONST_DEFINE_TEXT entries; no helper needed. */

extern char err_msg[];
extern int errors, warnings, pass, passes;
extern word outptr;
extern byte quiet;
extern byte *outprog;
extern word org;
extern char is_org_def;
extern FILE *outfile;
extern long int linenr;
extern char *param[2];
extern int param_type[2];
extern word old_outptr;

extern int target;
extern int cur_section;     /* SECTION_TEXT / SECTION_DATA / SECTION_BSS */
extern byte *dataprog;      /* data segment buffer (RDF only) */
extern word dataptr;        /* data write pointer */
extern dword bss_amount;    /* bytes reserved in .bss (RDF only) */
extern t_reloc *relocs;     /* head of reloc list (RDF only) */
extern word code_size;

extern void add_reloc(byte seg, dword ofs, byte width, word rseg, byte relative);
extern void add_segreloc(byte seg, dword ofs, word rseg);

extern int32_t get_const(const char *s);
extern char *get_dword(char *s, int32_t *x);

extern word entry_point;
extern char entry_point_def;
extern char *inputname;

extern char strip_line(char *line);
extern void split_line(t_line *cur, char *line, char *a1);

extern void *msa_malloc(size_t size);
extern void done(int c);

#endif

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

#ifndef _LEX_H_
#define _LEX_H_

#define LEX_NONE -1

#define LEX_DB 100
#define LEX_DW 101
#define LEX_DD 102
#define LEX_EXPORT 103
#define LEX_ORG 104
#define LEX_EQU 105
#define LEX_END 106
#define LEX_SECTION 107
#define LEX_RESB 108
#define LEX_RESW 109
#define LEX_RESD 130

#define LEX_SHORT 110
#define LEX_NEAR 111
#define LEX_FAR 112
#define LEX_SEG 113
#define LEX_IMPORT 114

#define LEX_CS2DOT 120
#define LEX_DS2DOT 121
#define LEX_ES2DOT 122
#define LEX_SS2DOT 123

#define LEX_AAA 300
#define LEX_AAS 320
#define LEX_AAD 340
#define LEX_AAM 360
#define LEX_ADC 380
#define LEX_ADD 400
#define LEX_AND 420
#define LEX_BOUND 440
#define LEX_CALL 460
#define LEX_CBW 480
#define LEX_CWD 500
#define LEX_CLC 520
#define LEX_CLD 540
#define LEX_CLI 560
#define LEX_CLTS 580
#define LEX_CMC 600
#define LEX_CMP 620
#define LEX_CMPSB 640
#define LEX_CMPSW 660
#define LEX_DAA 680
#define LEX_DAS 700
#define LEX_DEC 720
#define LEX_DIV 740
#define LEX_ENTER 760
#define LEX_IDIV 780
#define LEX_IMUL 800
#define LEX_IN 820
#define LEX_INC 840
#define LEX_INSB 860
#define LEX_INSW 880
#define LEX_HALT 900
#define LEX_INT3 920
#define LEX_INTO 940
#define LEX_IRET 960
#define LEX_JCXZ 980
#define LEX_JA 1000
#define LEX_JAE 1020
#define LEX_JB 1040
#define LEX_JBE 1060
#define LEX_JC 1080
#define LEX_JE 1100
#define LEX_JG 1120
#define LEX_JGE 1140
#define LEX_JL 1160
#define LEX_JLE 1180
#define LEX_JNA 1200
#define LEX_JNAE 1220
#define LEX_JNB 1240
#define LEX_JNBE 1260
#define LEX_JNC 1280
#define LEX_JNE 1300
#define LEX_JNG 1320
#define LEX_JNGE 1340
#define LEX_JLG 1360
#define LEX_JNL  1370
#define LEX_JNLE 1380
#define LEX_JNO 1400
#define LEX_JNP 1420
#define LEX_JNS 1440
#define LEX_JNZ 1460
#define LEX_JO 1480
#define LEX_JP 1500
#define LEX_JPE 1520
#define LEX_JPO 1540
#define LEX_JS 1560
#define LEX_JZ 1580
#define LEX_LAHF 1600
#define LEX_LDS 1620
#define LEX_LES 1640
#define LEX_LEA 1660
#define LEX_LEAVE 1680
#define LEX_LOOP 1740
#define LEX_LOOPE 1760
#define LEX_LOOPNE 1780
#define LEX_LOOPZ 1800
#define LEX_LOOPNZ 1820
#define LEX_MOV 1840
#define LEX_MOVSB 1860
#define LEX_MOVSW 1880
#define LEX_MUL 1900
#define LEX_NEG 1920
#define LEX_NOT 1940
#define LEX_NOP 1960
#define LEX_OR 1980
#define LEX_OUT 2000
#define LEX_OUTSB 2020
#define LEX_OUTSW 2040
#define LEX_LODSB 2060
#define LEX_LODSW 2080
#define LEX_POP 2100
#define LEX_POPA 2120
#define LEX_POPF 2140
#define LEX_PUSH 2160
#define LEX_PUSHA 2180
#define LEX_PUSHF 2200
#define LEX_REP 2220
#define LEX_REPNZ 2300
#define LEX_RCL1 2320
#define LEX_RCL 2340
#define LEX_RCR1 2360
#define LEX_RCR 2380
#define LEX_ROL1 2400
#define LEX_ROL 2420
#define LEX_ROR1 2440
#define LEX_ROR 2460
#define LEX_SAHF 2480
#define LEX_SAL1 2500
#define LEX_SAL 2520
#define LEX_SAR1 2540
#define LEX_SAR 2560
#define LEX_RET 2580
#define LEX_RETF 2600
#define LEX_SALC 2620
#define LEX_SBB 2640
#define LEX_SCASB 2660
#define LEX_SCASW 2680
#define LEX_SHL1 2700
#define LEX_SHL 2720
#define LEX_SHR1 2740
#define LEX_SHR 2760
#define LEX_STC 2780
#define LEX_STD 2800
#define LEX_STI 2820
#define LEX_STOSB 2840
#define LEX_STOSW 2860
#define LEX_SUB 2880
#define LEX_TEST 2900
#define LEX_WAIT 2920
#define LEX_XCHG 2940
#define LEX_XLATB 2960
#define LEX_XOR 2980
#define LEX_INT 3000
#define LEX_JMP 3020

extern int lookupLex(const char *, int *);
extern void lex_init();
extern void lex_done();

#endif

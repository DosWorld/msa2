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

#include <stdio.h>
#include <stdint.h>
#include <ctype.h>
#include <string.h>
#include "MSA2.H"
#include "LEX.H"

int lexId[192];
int lexHash[192];
int lexPreScan[192];
const char *lexStr[192];
int lexPtr;

void addLex(int id, const char *str) {
    int i;
    lexId[lexPtr] = id;
    lexStr[lexPtr] = str;
    lexHash[lexPtr] = hashCode(str);
    lexPreScan[lexPtr] = i = 0;

    while(instr86[i].lex1 != LEX_NONE) {
        if(instr86[i].lex1 == id) {
                lexPreScan[lexPtr] = i;
                break;
        }
        i++;
    }
    lexPtr++;
}

int lookupLex(const char *str, int *prescan) {
    int i, hash;

    if(*str == 0) {
        return LEX_NONE;
    }

    hash = hashCode(str);
    for(i = 0; i < lexPtr; i++) {
        if(hash == lexHash[i]) {
            if(!strcmp(lexStr[i], str)) {
                *prescan = lexPreScan[i];
                return lexId[i];
            }
        }
    }

    *prescan = 0;
    return LEX_NONE;
}

void lex_init() {
    lexPtr = 0;

    addLex(LEX_MOV, "MOV");
    addLex(LEX_XOR, "XOR");
    addLex(LEX_CMP, "CMP");
    addLex(LEX_ADD, "ADD");
    addLex(LEX_SUB, "SUB");
    addLex(LEX_PUSH, "PUSH");
    addLex(LEX_POP, "POP");
    addLex(LEX_INC, "INC");
    addLex(LEX_DEC, "DEC");
    addLex(LEX_OR, "OR");
    addLex(LEX_AND, "AND");
    addLex(LEX_IDIV, "IDIV");
    addLex(LEX_DIV, "DIV");
    addLex(LEX_IMUL,"IMUL");
    addLex(LEX_MUL, "MUL");
    addLex(LEX_TEST, "TEST");
    addLex(LEX_XCHG, "XCHG");
    addLex(LEX_JMP, "JMP");
    addLex(LEX_CALL, "CALL");
    addLex(LEX_NEG, "NEG");
    addLex(LEX_NOT, "NOT");
    addLex(LEX_NOP, "NOP");

    addLex(LEX_CMPSB, "CMPSB");
    addLex(LEX_CMPSW, "CMPSW");
    addLex(LEX_STOSB, "STOSB");
    addLex(LEX_STOSW, "STOSW");
    addLex(LEX_MOVSB, "MOVSB");
    addLex(LEX_MOVSW, "MOVSW");
    addLex(LEX_LODSB, "LODSB");
    addLex(LEX_LODSW, "LODSW");
    addLex(LEX_SCASB, "SCASB");
    addLex(LEX_SCASW, "SCASW");

    addLex(LEX_POPA, "POPA");
    addLex(LEX_POPF, "POPF");
    addLex(LEX_PUSHA, "PUSHA");
    addLex(LEX_PUSHF, "PUSHF");

    addLex(LEX_CBW, "CBW");
    addLex(LEX_CWD, "CWD");
    addLex(LEX_CLC, "CLC");
    addLex(LEX_CLD, "CLD");
    addLex(LEX_CLI, "CLI");
    addLex(LEX_ENTER, "ENTER");
    addLex(LEX_HALT, "HALT");
    addLex(LEX_INTO, "INTO");
    addLex(LEX_IRET, "IRET");
    addLex(LEX_JCXZ, "JCXZ");
    addLex(LEX_JA, "JA");
    addLex(LEX_JAE, "JAE");
    addLex(LEX_JB, "JB");
    addLex(LEX_JBE, "JBE");
    addLex(LEX_JC, "JC");
    addLex(LEX_JE, "JE");
    addLex(LEX_JG, "JG");
    addLex(LEX_JGE, "JGE");
    addLex(LEX_JL, "JL");
    addLex(LEX_JLE, "JLE");
    addLex(LEX_JNA, "JNA");
    addLex(LEX_JNAE, "JNAE");
    addLex(LEX_JNB, "JNB");
    addLex(LEX_JNBE, "JNBE");
    addLex(LEX_JNC, "JNC");
    addLex(LEX_JNE, "JNE");
    addLex(LEX_JNG, "JNG");
    addLex(LEX_JNGE, "JNGE");
    addLex(LEX_JLG, "JLG");
    addLex(LEX_JNL, "JNL");
    addLex(LEX_JNLE, "JNLE");
    addLex(LEX_JNO, "JNO");
    addLex(LEX_JNP, "JNP");
    addLex(LEX_JNS, "JNS");
    addLex(LEX_JNZ, "JNZ");
    addLex(LEX_JO, "JO");
    addLex(LEX_JP, "JP");
    addLex(LEX_JPE, "JPE");
    addLex(LEX_JPO, "JPO");
    addLex(LEX_JS, "JS");
    addLex(LEX_JZ, "JZ");
    addLex(LEX_LDS, "LDS");
    addLex(LEX_LES, "LES");
    addLex(LEX_LEA, "LEA");
    addLex(LEX_LEAVE, "LEAVE");
    addLex(LEX_LOOP, "LOOP");
    addLex(LEX_LOOPE, "LOPOPE");
    addLex(LEX_LOOPNE, "LOOPNE");
    addLex(LEX_LOOPZ, "LOOPZ");
    addLex(LEX_LOOPNZ, "LOOPNZ");
    addLex(LEX_IN, "IN");
    addLex(LEX_OUT, "OUT");

    addLex(LEX_REP, "REP");
    addLex(LEX_REP, "REPE");
    addLex(LEX_REP, "REPZ");
    addLex(LEX_REPNZ, "REPNE");
    addLex(LEX_REPNZ, "REPNZ");

    addLex(LEX_INT, "INT");

    addLex(LEX_RET, "RET");
    addLex(LEX_RET, "RETN");
    addLex(LEX_RETF, "RETF");

    addLex(LEX_INSB, "INSB");
    addLex(LEX_INSW, "INSW");
    addLex(LEX_OUTSB, "OUTSB");
    addLex(LEX_OUTSW, "OUTSW");
    addLex(LEX_RCL1, "RCL1");
    addLex(LEX_RCL, "RCL");
    addLex(LEX_RCR1, "RCR1");
    addLex(LEX_RCR, "RCR");
    addLex(LEX_ROL1, "ROL1");
    addLex(LEX_ROL, "ROL");
    addLex(LEX_ROR1, "ROR1");
    addLex(LEX_ROR, "ROR");
    addLex(LEX_LAHF, "LAHF");
    addLex(LEX_SAHF, "SAHF");
    addLex(LEX_SAL1, "SAL1");
    addLex(LEX_SAL, "SAL");
    addLex(LEX_SAR1, "SAR1");
    addLex(LEX_SAR, "SAR");

    addLex(LEX_SALC, "SALC");
    addLex(LEX_SBB, "SBB");
    addLex(LEX_SHL1, "SHL1");
    addLex(LEX_SHL, "SHL");
    addLex(LEX_SHR1, "SHR1");
    addLex(LEX_SHR, "SHR");
    addLex(LEX_STC, "STC");
    addLex(LEX_STD, "STD");
    addLex(LEX_STI, "STI");

    addLex(LEX_AAA, "AAA");
    addLex(LEX_AAS, "AAS");
    addLex(LEX_AAD, "AAD");
    addLex(LEX_AAM, "AAM");
    addLex(LEX_ADC, "ADC");
    addLex(LEX_BOUND, "BOUND");
    addLex(LEX_CLTS, "CLTS");
    addLex(LEX_CMC, "CMC");
    addLex(LEX_DAA, "DAA");
    addLex(LEX_DAS, "DAS");
    addLex(LEX_WAIT, "WAIT");
    addLex(LEX_XLATB, "XLATB");
    addLex(LEX_INT3, "INT3");

    addLex(LEX_DB, "DB");
    addLex(LEX_DW, "DW");
    addLex(LEX_DD, "DD");
    addLex(LEX_ORG, "ORG");
    addLex(LEX_END, "END");

    addLex(LEX_EXPORT, "EXPORT");
    addLex(LEX_IMPORT, "IMPORT");
    addLex(LEX_SECTION, "SECTION");
    addLex(LEX_SECTION, "SEGMENT");
    addLex(LEX_RESB, "RESB");
    addLex(LEX_RESW, "RESW");
    addLex(LEX_RESD, "RESD");

    addLex(LEX_CS2DOT, "CS:");
    addLex(LEX_DS2DOT, "DS:");
    addLex(LEX_ES2DOT, "ES:");
    addLex(LEX_SS2DOT, "SS:");

    addLex(LEX_SHORT, "SHORT");
    addLex(LEX_NEAR, "NEAR");
    addLex(LEX_FAR, "FAR");
    addLex(LEX_SEG, "SEG");
    addLex(LEX_EQU, "EQU");
}

void lex_done() {
}

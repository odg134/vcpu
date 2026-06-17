#pragma once

#include "Types.hpp"

namespace Vcpu
{
    enum class OpcodeType : UInt8
    {
        Nop = 0x00,
        Halt = 0x01,

        Mov = 0x10,
        Ldi = 0x11,
        Ld = 0x12,
        St = 0x13,
        Ldb = 0x14,
        Stb = 0x15,

        Add = 0x20,
        Sub = 0x21,
        Mul = 0x22,
        Div = 0x23,
        Mod = 0x24,
        Addi = 0x25,
        Subi = 0x26,
        Neg = 0x27,
        Inc = 0x28,
        Dec = 0x29,

        And = 0x30,
        Or = 0x31,
        Xor = 0x32,
        Not = 0x33,
        Shl = 0x34,
        Shr = 0x35,
        Shli = 0x36,
        Shri = 0x37,

        Cmp = 0x40,
        Cmpi = 0x41,

        Jmp = 0x50,
        Je = 0x51,
        Jne = 0x52,
        Jg = 0x53,
        Jge = 0x54,
        Jl = 0x55,
        Jle = 0x56,

        Push = 0x60,
        Pop = 0x61,
        Call = 0x62,
        Ret = 0x63,

        Sys = 0x70,
    };

    
    enum class OperandFormType : UInt8
    {
        None,
        R,
        Rr,
        Ri,
        Rri,
        Imm,
    };

    typedef struct _OpcodeInfo
    {
        OpcodeType Opcode;
        const char* Mnemonic;
        OperandFormType Form;
    } OpcodeInfo;

    const OpcodeInfo* OpcodeLookupByMnemonic( const char* Mnemonic );
    const OpcodeInfo* OpcodeLookupByOpcode( OpcodeType Opcode );
}

#pragma once

#include "Types.hpp"
#include "Opcodes.hpp"

namespace Vcpu
{
    //
    // Fixed twelve byte instruction word:
    //
    //   [0] Opcode
    //   [1] RegDst   destination / first register operand
    //   [2] RegSrc   source / second register operand
    //   [3] reserved ( always zero )
    //   [4..11] Immediate, little endian
    //
    static const UInt64 InstructionSize = 12;

    typedef struct _DecodedInstruction
    {
        OpcodeType Opcode;
        UInt8 RegDst;
        UInt8 RegSrc;
        UInt64 Immediate;
    } DecodedInstruction;

    void InstructionDecode( const UInt8* Bytes, DecodedInstruction* Out );
    void InstructionEncode( const DecodedInstruction* Instruction, UInt8* Bytes );
}

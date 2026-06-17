#include "Vcpu/Instruction.hpp"

namespace Vcpu
{
    static UInt64 ReadImmediate( const UInt8* Bytes )
    {
        UInt64 Value = 0;

        for ( UInt32 Index = 0; Index < 8; Index++ )
        {
            Value |= ( (UInt64)Bytes[ Index ] ) << ( Index * 8 );
        }

        return Value;
    }

    static void WriteImmediate( UInt8* Bytes, UInt64 Value )
    {
        for ( UInt32 Index = 0; Index < 8; Index++ )
        {
            Bytes[ Index ] = (UInt8)( ( Value >> ( Index * 8 ) ) & 0xFF );
        }
    }

    void InstructionDecode( const UInt8* Bytes, DecodedInstruction* Out )
    {
        Out->Opcode = (OpcodeType)Bytes[ 0 ];
        Out->RegDst = Bytes[ 1 ];
        Out->RegSrc = Bytes[ 2 ];
        Out->Immediate = ReadImmediate( &Bytes[ 4 ] );
    }

    void InstructionEncode( const DecodedInstruction* Instruction, UInt8* Bytes )
    {
        Bytes[ 0 ] = (UInt8)Instruction->Opcode;
        Bytes[ 1 ] = Instruction->RegDst;
        Bytes[ 2 ] = Instruction->RegSrc;
        Bytes[ 3 ] = 0;

        WriteImmediate( &Bytes[ 4 ], Instruction->Immediate );
    }
}

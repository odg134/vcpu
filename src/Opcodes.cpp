#include "Vcpu/Opcodes.hpp"

namespace Vcpu
{
    static const OpcodeInfo OpcodeTable[] =
    {
        { OpcodeType::Nop, "nop", OperandFormType::None },
        { OpcodeType::Halt, "halt", OperandFormType::None },

        { OpcodeType::Mov, "mov", OperandFormType::Rr },
        { OpcodeType::Ldi, "ldi", OperandFormType::Ri },
        { OpcodeType::Ld, "ld", OperandFormType::Rri },
        { OpcodeType::St, "st", OperandFormType::Rri },
        { OpcodeType::Ldb, "ldb", OperandFormType::Rri },
        { OpcodeType::Stb, "stb", OperandFormType::Rri },

        { OpcodeType::Add, "add", OperandFormType::Rr },
        { OpcodeType::Sub, "sub", OperandFormType::Rr },
        { OpcodeType::Mul, "mul", OperandFormType::Rr },
        { OpcodeType::Div, "div", OperandFormType::Rr },
        { OpcodeType::Mod, "mod", OperandFormType::Rr },
        { OpcodeType::Addi, "addi", OperandFormType::Ri },
        { OpcodeType::Subi, "subi", OperandFormType::Ri },
        { OpcodeType::Neg, "neg", OperandFormType::R },
        { OpcodeType::Inc, "inc", OperandFormType::R },
        { OpcodeType::Dec, "dec", OperandFormType::R },

        { OpcodeType::And, "and", OperandFormType::Rr },
        { OpcodeType::Or, "or", OperandFormType::Rr },
        { OpcodeType::Xor, "xor", OperandFormType::Rr },
        { OpcodeType::Not, "not", OperandFormType::R },
        { OpcodeType::Shl, "shl", OperandFormType::Rr },
        { OpcodeType::Shr, "shr", OperandFormType::Rr },
        { OpcodeType::Shli, "shli", OperandFormType::Ri },
        { OpcodeType::Shri, "shri", OperandFormType::Ri },

        { OpcodeType::Cmp, "cmp", OperandFormType::Rr },
        { OpcodeType::Cmpi, "cmpi", OperandFormType::Ri },

        { OpcodeType::Jmp, "jmp", OperandFormType::Imm },
        { OpcodeType::Je, "je", OperandFormType::Imm },
        { OpcodeType::Jne, "jne", OperandFormType::Imm },
        { OpcodeType::Jg, "jg", OperandFormType::Imm },
        { OpcodeType::Jge, "jge", OperandFormType::Imm },
        { OpcodeType::Jl, "jl", OperandFormType::Imm },
        { OpcodeType::Jle, "jle", OperandFormType::Imm },

        { OpcodeType::Push, "push", OperandFormType::R },
        { OpcodeType::Pop, "pop", OperandFormType::R },
        { OpcodeType::Call, "call", OperandFormType::Imm },
        { OpcodeType::Ret, "ret", OperandFormType::None },

        { OpcodeType::Sys, "sys", OperandFormType::Imm },
    };

    static const UInt64 OpcodeTableCount = sizeof( OpcodeTable ) / sizeof( OpcodeTable[ 0 ] );

    static Boolean EqualsIgnoreCase( const char* Left, const char* Right )
    {
        while ( *Left != '\0' && *Right != '\0' )
        {
            char A = *Left;
            char B = *Right;

            if ( A >= 'A' && A <= 'Z' )
            {
                A = (char)( A + ( 'a' - 'A' ) );
            }

            if ( B >= 'A' && B <= 'Z' )
            {
                B = (char)( B + ( 'a' - 'A' ) );
            }

            if ( A != B )
            {
                return False;
            }

            Left++;
            Right++;
        }

        return ( *Left == '\0' && *Right == '\0' );
    }

    const OpcodeInfo* OpcodeLookupByMnemonic( const char* Mnemonic )
    {
        for ( UInt64 Index = 0; Index < OpcodeTableCount; Index++ )
        {
            if ( EqualsIgnoreCase( OpcodeTable[ Index ].Mnemonic, Mnemonic ) )
            {
                return &OpcodeTable[ Index ];
            }
        }

        return nullptr;
    }

    const OpcodeInfo* OpcodeLookupByOpcode( OpcodeType Opcode )
    {
        for ( UInt64 Index = 0; Index < OpcodeTableCount; Index++ )
        {
            if ( OpcodeTable[ Index ].Opcode == Opcode )
            {
                return &OpcodeTable[ Index ];
            }
        }

        return nullptr;
    }
}

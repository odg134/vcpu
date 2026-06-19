#include "Vcpu/Cpu.hpp"
#include "Vcpu/Instruction.hpp"
#include "Vcpu/Assembler.hpp"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <cstdio>
#include <cstring>
#include <string>

namespace Vcpu
{
    //
    // Host services reachable through the Sys instruction. R0 carries the
    // argument and, for the input calls, receives the result.
    //
    enum class SyscallType : UInt64
    {
        Exit = 0,
        PutChar = 1,
        PutInt = 2,
        PutString = 3,
        GetChar = 4,
        GetInt = 5,
        Sleep = 6,
        Ticks = 7,
    };

    static UInt64 HostQpcNow( void )
    {
        LARGE_INTEGER Counter;
        QueryPerformanceCounter( &Counter );
        return (UInt64)Counter.QuadPart;
    }

    static UInt64 HostQpcFrequency( void )
    {
        LARGE_INTEGER Frequency;
        QueryPerformanceFrequency( &Frequency );
        return (UInt64)Frequency.QuadPart;
    }

    static void HostWrite( const void* Data, UInt32 Length )
    {
        HANDLE Output = GetStdHandle( STD_OUTPUT_HANDLE );
        DWORD Written = 0;
        WriteFile( Output, Data, Length, &Written, nullptr );
    }

    static void HostWriteError( const char* Text )
    {
        HANDLE Output = GetStdHandle( STD_ERROR_HANDLE );
        DWORD Written = 0;
        WriteFile( Output, Text, (DWORD)strlen( Text ), &Written, nullptr );
    }

    static void HostWriteInt( Int64 Value )
    {
        char Buffer[ 32 ];
        UInt32 Position = sizeof( Buffer );
        Boolean Negative = ( Value < 0 );

        //
        // Work in unsigned space so INT64_MIN negates cleanly.
        //
        UInt64 Magnitude;

        if ( Negative )
        {
            Magnitude = (UInt64)( -( Value + 1 ) ) + 1;
        }
        else
        {
            Magnitude = (UInt64)Value;
        }

        if ( Magnitude == 0 )
        {
            Buffer[ --Position ] = '0';
        }

        while ( Magnitude > 0 )
        {
            Buffer[ --Position ] = (char)( '0' + ( Magnitude % 10 ) );
            Magnitude /= 10;
        }

        if ( Negative )
        {
            Buffer[ --Position ] = '-';
        }

        HostWrite( &Buffer[ Position ], sizeof( Buffer ) - Position );
    }

    static UInt64 HostReadChar( void )
    {
        HANDLE Input = GetStdHandle( STD_INPUT_HANDLE );
        char Character = 0;
        DWORD Read = 0;

        if ( !ReadFile( Input, &Character, 1, &Read, nullptr ) || Read == 0 )
        {
            return (UInt64)-1;
        }

        return (UInt64)(UInt8)Character;
    }

    static Int64 HostReadInt( void )
    {
        HANDLE Input = GetStdHandle( STD_INPUT_HANDLE );
        char Buffer[ 64 ];
        DWORD Read = 0;

        if ( !ReadFile( Input, Buffer, sizeof( Buffer ) - 1, &Read, nullptr ) )
        {
            return 0;
        }

        Buffer[ Read ] = '\0';

        //
        // Skip leading whitespace, take an optional sign, then digits.
        //
        UInt32 Index = 0;

        while ( Buffer[ Index ] == ' ' || Buffer[ Index ] == '\t' || Buffer[ Index ] == '\r' || Buffer[ Index ] == '\n' )
        {
            Index++;
        }

        Boolean Negative = False;

        if ( Buffer[ Index ] == '-' )
        {
            Negative = True;
            Index++;
        }
        else if ( Buffer[ Index ] == '+' )
        {
            Index++;
        }

        Int64 Value = 0;

        while ( Buffer[ Index ] >= '0' && Buffer[ Index ] <= '9' )
        {
            Value = ( Value * 10 ) + ( Buffer[ Index ] - '0' );
            Index++;
        }

        return Negative ? -Value : Value;
    }

    static UInt64 GetRegister( CpuContext* Cpu, UInt8 Index )
    {
        if ( Index < GeneralRegisterCount )
        {
            return Cpu->Registers[ Index ];
        }

        if ( Index == RegisterSp )
        {
            return Cpu->StackPointer;
        }

        Cpu->Running = False;
        Cpu->StopReason = StopReasonType::InvalidOpcode;
        return 0;
    }

    static void SetRegister( CpuContext* Cpu, UInt8 Index, UInt64 Value )
    {
        if ( Index < GeneralRegisterCount )
        {
            Cpu->Registers[ Index ] = Value;
            return;
        }

        if ( Index == RegisterSp )
        {
            Cpu->StackPointer = Value;
            return;
        }

        Cpu->Running = False;
        Cpu->StopReason = StopReasonType::InvalidOpcode;
    }

    static Boolean PushValue( CpuContext* Cpu, UInt64 Value )
    {
        UInt64 NewSp = Cpu->StackPointer - 8;

        if ( !MemoryWrite64( Cpu->Memory, NewSp, Value ) )
        {
            return False;
        }

        Cpu->StackPointer = NewSp;
        return True;
    }

    static Boolean PopValue( CpuContext* Cpu, UInt64* Value )
    {
        if ( !MemoryRead64( Cpu->Memory, Cpu->StackPointer, Value ) )
        {
            return False;
        }

        Cpu->StackPointer += 8;
        return True;
    }

    //
    // Set ZF / SF / CF / OF from Left - Right, exactly the way the
    // conditional jumps expect to read them back.
    //
    static void UpdateFlags( CpuContext* Cpu, UInt64 Left, UInt64 Right )
    {
        UInt64 Result = Left - Right;
        UInt64 Flags = 0;

        if ( Result == 0 )
        {
            Flags |= FlagZero;
        }

        if ( ( Result >> 63 ) & 1 )
        {
            Flags |= FlagSign;
        }

        if ( Left < Right )
        {
            Flags |= FlagCarry;
        }

        if ( ( ( Left ^ Right ) & ( Left ^ Result ) ) >> 63 )
        {
            Flags |= FlagOverflow;
        }

        Cpu->Flags = Flags;
    }

    static Boolean FlagSet( CpuContext* Cpu, UInt64 Flag )
    {
        return ( Cpu->Flags & Flag ) != 0;
    }

    static void Fault( CpuContext* Cpu, StopReasonType Reason )
    {
        Cpu->Running = False;
        Cpu->StopReason = Reason;
    }

    static void ExecuteSyscall( CpuContext* Cpu, UInt64 Number )
    {
        switch ( (SyscallType)Number )
        {
        case SyscallType::Exit:
            Cpu->ExitCode = Cpu->Registers[ 0 ];
            Cpu->Running = False;
            Cpu->StopReason = StopReasonType::Halted;
            break;

        case SyscallType::PutChar:
        {
            char Character = (char)( Cpu->Registers[ 0 ] & 0xFF );
            HostWrite( &Character, 1 );
            break;
        }

        case SyscallType::PutInt:
            HostWriteInt( (Int64)Cpu->Registers[ 0 ] );
            break;

        case SyscallType::PutString:
        {
            UInt64 Address = Cpu->Registers[ 0 ];
            char Chunk[ 256 ];
            UInt32 Fill = 0;

            //
            // Walk the guest string until the null terminator. A missing
            // terminator simply runs into the end of memory and faults.
            //
            for ( ;; )
            {
                UInt8 Byte = 0;

                if ( !MemoryRead8( Cpu->Memory, Address, &Byte ) )
                {
                    Fault( Cpu, StopReasonType::MemoryFault );
                    break;
                }

                if ( Byte == 0 )
                {
                    break;
                }

                Chunk[ Fill++ ] = (char)Byte;
                Address++;

                if ( Fill == sizeof( Chunk ) )
                {
                    HostWrite( Chunk, Fill );
                    Fill = 0;
                }
            }

            if ( Fill > 0 )
            {
                HostWrite( Chunk, Fill );
            }

            break;
        }

        case SyscallType::GetChar:
            Cpu->Registers[ 0 ] = HostReadChar();
            break;

        case SyscallType::GetInt:
            Cpu->Registers[ 0 ] = (UInt64)HostReadInt();
            break;

        case SyscallType::Sleep:
            Sleep( (DWORD)Cpu->Registers[ 0 ] );
            break;

        case SyscallType::Ticks:
        {
            UInt64 Frequency = HostQpcFrequency();
            UInt64 Elapsed = HostQpcNow() - Cpu->StartTicks;
            Cpu->Registers[ 0 ] = ( Frequency != 0 ) ? ( Elapsed * 1000 / Frequency ) : 0;
            break;
        }

        default:
            Fault( Cpu, StopReasonType::InvalidSyscall );
            break;
        }
    }

    void CpuInitialize( CpuContext* Cpu, MemoryContext* Memory, UInt64 EntryPoint )
    {
        memset( Cpu, 0, sizeof( CpuContext ) );

        Cpu->Memory = Memory;
        Cpu->ProgramCounter = EntryPoint;
        Cpu->StackPointer = Memory->Size;
        Cpu->Running = True;
        Cpu->StopReason = StopReasonType::Running;
        Cpu->StartTicks = HostQpcNow();
    }

    Boolean CpuStep( CpuContext* Cpu )
    {
        if ( !Cpu->Running )
        {
            return False;
        }

        //
        // Fetch / The whole instruction word must be i nside the image.
        //
        UInt64 Pc = Cpu->ProgramCounter;

        if ( Pc > Cpu->Memory->Size || InstructionSize > Cpu->Memory->Size - Pc )
        {
            Fault( Cpu, StopReasonType::MemoryFault );
            return False;
        }

        if ( Cpu->Trace )
        {
            char Prefix[ 32 ];
            std::snprintf( Prefix, sizeof( Prefix ), "%08llX:  ", (unsigned long long)Pc );

            std::string Line = Disassemble( Cpu->Memory->Base, Cpu->Memory->Size, Pc );

            HostWriteError( Prefix );
            HostWriteError( Line.c_str() );
            HostWriteError( "\n" );
        }

        DecodedInstruction Instruction;
        InstructionDecode( &Cpu->Memory->Base[ Pc ], &Instruction );

        //
        // Advance past the instruction before executing, so jumps and calls
        // overwrite a program counter that already points at the next slot.
        //
        Cpu->ProgramCounter = Pc + InstructionSize;

        UInt8 Dst = Instruction.RegDst;
        UInt8 Src = Instruction.RegSrc;
        UInt64 Imm = Instruction.Immediate;

        switch ( Instruction.Opcode )
        {
        case OpcodeType::Nop:
            break;

        case OpcodeType::Halt:
            Cpu->Running = False;
            Cpu->StopReason = StopReasonType::Halted;
            break;

        case OpcodeType::Mov:
            SetRegister( Cpu, Dst, GetRegister( Cpu, Src ) );
            break;

        case OpcodeType::Ldi:
            SetRegister( Cpu, Dst, Imm );
            break;

        case OpcodeType::Ld:
        {
            UInt64 Value = 0;

            if ( !MemoryRead64( Cpu->Memory, GetRegister( Cpu, Src ) + Imm, &Value ) )
            {
                Fault( Cpu, StopReasonType::MemoryFault );
                break;
            }

            SetRegister( Cpu, Dst, Value );
            break;
        }

        case OpcodeType::St:
            if ( !MemoryWrite64( Cpu->Memory, GetRegister( Cpu, Dst ) + Imm, GetRegister( Cpu, Src ) ) )
            {
                Fault( Cpu, StopReasonType::MemoryFault );
            }
            break;

        case OpcodeType::Ldb:
        {
            UInt8 Value = 0;

            if ( !MemoryRead8( Cpu->Memory, GetRegister( Cpu, Src ) + Imm, &Value ) )
            {
                Fault( Cpu, StopReasonType::MemoryFault );
                break;
            }

            SetRegister( Cpu, Dst, (UInt64)Value );
            break;
        }

        case OpcodeType::Stb:
            if ( !MemoryWrite8( Cpu->Memory, GetRegister( Cpu, Dst ) + Imm, (UInt8)( GetRegister( Cpu, Src ) & 0xFF ) ) )
            {
                Fault( Cpu, StopReasonType::MemoryFault );
            }
            break;

        case OpcodeType::Add:
            SetRegister( Cpu, Dst, GetRegister( Cpu, Dst ) + GetRegister( Cpu, Src ) );
            break;

        case OpcodeType::Sub:
            SetRegister( Cpu, Dst, GetRegister( Cpu, Dst ) - GetRegister( Cpu, Src ) );
            break;

        case OpcodeType::Mul:
            SetRegister( Cpu, Dst, GetRegister( Cpu, Dst ) * GetRegister( Cpu, Src ) );
            break;

        case OpcodeType::Div:
        case OpcodeType::Mod:
        {
            Int64 Left = (Int64)GetRegister( Cpu, Dst );
            Int64 Right = (Int64)GetRegister( Cpu, Src );

            if ( Right == 0 )
            {
                Fault( Cpu, StopReasonType::DivideByZero );
                break;
            }

            Int64 Result;

            if ( Left == INT64_MIN && Right == -1 )
            {
                //
                // The one signed case that traps on real hardware, define
                // it instead of letting it overflow.
                // TOOD: full fix?
                //
                Result = ( Instruction.Opcode == OpcodeType::Div ) ? INT64_MIN : 0;
            }
            else if ( Instruction.Opcode == OpcodeType::Div )
            {
                Result = Left / Right;
            }
            else
            {
                Result = Left % Right;
            }

            SetRegister( Cpu, Dst, (UInt64)Result );
            break;
        }

        case OpcodeType::Addi:
            SetRegister( Cpu, Dst, GetRegister( Cpu, Dst ) + Imm );
            break;

        case OpcodeType::Subi:
            SetRegister( Cpu, Dst, GetRegister( Cpu, Dst ) - Imm );
            break;

        case OpcodeType::Neg:
            SetRegister( Cpu, Dst, (UInt64)( 0 - (Int64)GetRegister( Cpu, Dst ) ) );
            break;

        case OpcodeType::Inc:
            SetRegister( Cpu, Dst, GetRegister( Cpu, Dst ) + 1 );
            break;

        case OpcodeType::Dec:
            SetRegister( Cpu, Dst, GetRegister( Cpu, Dst ) - 1 );
            break;

        case OpcodeType::And:
            SetRegister( Cpu, Dst, GetRegister( Cpu, Dst ) & GetRegister( Cpu, Src ) );
            break;

        case OpcodeType::Or:
            SetRegister( Cpu, Dst, GetRegister( Cpu, Dst ) | GetRegister( Cpu, Src ) );
            break;

        case OpcodeType::Xor:
            SetRegister( Cpu, Dst, GetRegister( Cpu, Dst ) ^ GetRegister( Cpu, Src ) );
            break;

        case OpcodeType::Not:
            SetRegister( Cpu, Dst, ~GetRegister( Cpu, Dst ) );
            break;

        case OpcodeType::Shl:
            SetRegister( Cpu, Dst, GetRegister( Cpu, Dst ) << ( GetRegister( Cpu, Src ) & 63 ) );
            break;

        case OpcodeType::Shr:
            SetRegister( Cpu, Dst, GetRegister( Cpu, Dst ) >> ( GetRegister( Cpu, Src ) & 63 ) );
            break;

        case OpcodeType::Shli:
            SetRegister( Cpu, Dst, GetRegister( Cpu, Dst ) << ( Imm & 63 ) );
            break;

        case OpcodeType::Shri:
            SetRegister( Cpu, Dst, GetRegister( Cpu, Dst ) >> ( Imm & 63 ) );
            break;

        case OpcodeType::Cmp:
            UpdateFlags( Cpu, GetRegister( Cpu, Dst ), GetRegister( Cpu, Src ) );
            break;

        case OpcodeType::Cmpi:
            UpdateFlags( Cpu, GetRegister( Cpu, Dst ), Imm );
            break;

        case OpcodeType::Jmp:
            Cpu->ProgramCounter = Imm;
            break;

        case OpcodeType::Je:
            if ( FlagSet( Cpu, FlagZero ) )
            {
                Cpu->ProgramCounter = Imm;
            }
            break;

        case OpcodeType::Jne:
            if ( !FlagSet( Cpu, FlagZero ) )
            {
                Cpu->ProgramCounter = Imm;
            }
            break;

        case OpcodeType::Jg:
            if ( !FlagSet( Cpu, FlagZero ) && ( FlagSet( Cpu, FlagSign ) == FlagSet( Cpu, FlagOverflow ) ) )
            {
                Cpu->ProgramCounter = Imm;
            }
            break;

        case OpcodeType::Jge:
            if ( FlagSet( Cpu, FlagSign ) == FlagSet( Cpu, FlagOverflow ) )
            {
                Cpu->ProgramCounter = Imm;
            }
            break;

        case OpcodeType::Jl:
            if ( FlagSet( Cpu, FlagSign ) != FlagSet( Cpu, FlagOverflow ) )
            {
                Cpu->ProgramCounter = Imm;
            }
            break;

        case OpcodeType::Jle:
            if ( FlagSet( Cpu, FlagZero ) || ( FlagSet( Cpu, FlagSign ) != FlagSet( Cpu, FlagOverflow ) ) )
            {
                Cpu->ProgramCounter = Imm;
            }
            break;

        case OpcodeType::Push:
            if ( !PushValue( Cpu, GetRegister( Cpu, Dst ) ) )
            {
                Fault( Cpu, StopReasonType::StackFault );
            }
            break;

        case OpcodeType::Pop:
        {
            UInt64 Value = 0;

            if ( !PopValue( Cpu, &Value ) )
            {
                Fault( Cpu, StopReasonType::StackFault );
                break;
            }

            SetRegister( Cpu, Dst, Value );
            break;
        }

        case OpcodeType::Call:
            if ( !PushValue( Cpu, Cpu->ProgramCounter ) )
            {
                Fault( Cpu, StopReasonType::StackFault );
                break;
            }

            Cpu->ProgramCounter = Imm;
            break;

        case OpcodeType::Ret:
        {
            UInt64 Return = 0;

            if ( !PopValue( Cpu, &Return ) )
            {
                Fault( Cpu, StopReasonType::StackFault );
                break;
            }

            Cpu->ProgramCounter = Return;
            break;
        }

        case OpcodeType::Sys:
            ExecuteSyscall( Cpu, Imm );
            break;

        default:
            Fault( Cpu, StopReasonType::InvalidOpcode );
            break;
        }

        Cpu->InstructionsRetired++;

        return Cpu->Running;
    }

    UInt64 CpuRun( CpuContext* Cpu )
    {
        while ( Cpu->Running )
        {
            CpuStep( Cpu );
        }

        return Cpu->ExitCode;
    }

    const char* CpuStopReasonText( StopReasonType Reason )
    {
        switch ( Reason )
        {
        case StopReasonType::Running: return "running";
        case StopReasonType::Halted: return "halted";
        case StopReasonType::InvalidOpcode: return "invalid opcode";
        case StopReasonType::MemoryFault: return "memory fault";
        case StopReasonType::DivideByZero: return "divide by zero";
        case StopReasonType::InvalidSyscall: return "invalid syscall";
        case StopReasonType::StackFault: return "stack fault";
        }

        return "unknown";
    }
}

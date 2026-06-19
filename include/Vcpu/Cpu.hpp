#pragma once

#include "Types.hpp"
#include "Memory.hpp"

namespace Vcpu
{
    static const UInt32 GeneralRegisterCount = 16;

    //
    // The stack pointer is exposed to the instruction stream as register
    // index sixteen, so code can address it like any other register.
    //
    static const UInt8 RegisterSp = 16;

    //
    // Status flags, set by cmp / Cmpi and consumed by the conditional
    // jumps, semantics mirror the usual x86 style flags.
    //
    static const UInt64 FlagZero = ( 1ull << 0 );
    static const UInt64 FlagSign = ( 1ull << 1 );
    static const UInt64 FlagCarry = ( 1ull << 2 );
    static const UInt64 FlagOverflow = ( 1ull << 3 );

    enum class StopReasonType : UInt32
    {
        Running,
        Halted,
        InvalidOpcode,
        MemoryFault,
        DivideByZero,
        InvalidSyscall,
        StackFault,
    };

    typedef struct _CpuContext
    {
        UInt64 Registers[ GeneralRegisterCount ];
        UInt64 StackPointer;
        UInt64 ProgramCounter;
        UInt64 Flags;

        MemoryContext* Memory;

        Boolean Running;
        StopReasonType StopReason;
        UInt64 ExitCode;

        Boolean Trace;
        UInt64 InstructionsRetired;
        UInt64 StartTicks;
    } CpuContext;

    //
    // Wire a zeroed core to a memory image and prime PC and SP. The stack
    // grows downward from the top of the image.
    //
    void CpuInitialize( CpuContext* Cpu, MemoryContext* Memory, UInt64 EntryPoint );

    //
    // Execute one instruction. Returns False once the core has stopped.
    //
    Boolean CpuStep( CpuContext* Cpu );

    UInt64 CpuRun( CpuContext* Cpu );

    const char* CpuStopReasonText( StopReasonType Reason );
}

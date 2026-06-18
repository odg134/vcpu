#pragma once

#include "Types.hpp"

namespace Vcpu
{
    //
    // The guest physical address space: a single flat, zero filled block
    // backed by VirtualAlloc. Base is offset zero; Size is the total
    // number of addressable bytes.
    //
    typedef struct _MemoryContext
    {
        UInt8* Base;
        UInt64 Size;
    } MemoryContext;

    Boolean MemoryCreate( MemoryContext* Memory, UInt64 Size );
    void MemoryDestroy( MemoryContext* Memory );

    Boolean MemoryWriteBlock( MemoryContext* Memory, UInt64 Address, const UInt8* Source, UInt64 Length );

    Boolean MemoryRead8( MemoryContext* Memory, UInt64 Address, UInt8* Value );
    Boolean MemoryRead64( MemoryContext* Memory, UInt64 Address, UInt64* Value );

    Boolean MemoryWrite8( MemoryContext* Memory, UInt64 Address, UInt8 Value );
    Boolean MemoryWrite64( MemoryContext* Memory, UInt64 Address, UInt64 Value );
}

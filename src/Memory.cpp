#include "Vcpu/Memory.hpp"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <cstring>

namespace Vcpu
{
    Boolean MemoryCreate( MemoryContext* Memory, UInt64 Size )
    {
        if ( Memory == nullptr || Size == 0 )
        {
            return False;
        }

        //
        // Commit the whole image up front. VirtualAlloc hands back zeroed,
        // page aligned pages, which is exactly what a freshly powered guest
        // expects to see.
        //
        void* Region = VirtualAlloc( nullptr, (SIZE_T)Size, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE );

        if ( Region == nullptr )
        {
            return False;
        }

        Memory->Base = (UInt8*)Region;
        Memory->Size = Size;

        return True;
    }

    void MemoryDestroy( MemoryContext* Memory )
    {
        if ( Memory == nullptr || Memory->Base == nullptr )
        {
            return;
        }

        VirtualFree( Memory->Base, 0, MEM_RELEASE );

        Memory->Base = nullptr;
        Memory->Size = 0;
    }

    //
    // True when [ Address, Address + Length ) sits entirely inside the
    // image. The subtraction form guards against wraparound.
    //
    static Boolean RangeIsValid( MemoryContext* Memory, UInt64 Address, UInt64 Length )
    {
        if ( Memory == nullptr || Memory->Base == nullptr )
        {
            return False;
        }

        if ( Address > Memory->Size )
        {
            return False;
        }

        if ( Length > Memory->Size - Address )
        {
            return False;
        }

        return True;
    }

    Boolean MemoryWriteBlock( MemoryContext* Memory, UInt64 Address, const UInt8* Source, UInt64 Length )
    {
        if ( !RangeIsValid( Memory, Address, Length ) )
        {
            return False;
        }

        memcpy( &Memory->Base[ Address ], Source, (size_t)Length );

        return True;
    }

    Boolean MemoryRead8( MemoryContext* Memory, UInt64 Address, UInt8* Value )
    {
        if ( !RangeIsValid( Memory, Address, 1 ) )
        {
            return False;
        }

        *Value = Memory->Base[ Address ];

        return True;
    }

    Boolean MemoryRead64( MemoryContext* Memory, UInt64 Address, UInt64* Value )
    {
        if ( !RangeIsValid( Memory, Address, 8 ) )
        {
            return False;
        }

        UInt64 Result = 0;

        for ( UInt32 Index = 0; Index < 8; Index++ )
        {
            Result |= ( (UInt64)Memory->Base[ Address + Index ] ) << ( Index * 8 );
        }

        *Value = Result;

        return True;
    }

    Boolean MemoryWrite8( MemoryContext* Memory, UInt64 Address, UInt8 Value )
    {
        if ( !RangeIsValid( Memory, Address, 1 ) )
        {
            return False;
        }

        Memory->Base[ Address ] = Value;

        return True;
    }

    Boolean MemoryWrite64( MemoryContext* Memory, UInt64 Address, UInt64 Value )
    {
        if ( !RangeIsValid( Memory, Address, 8 ) )
        {
            return False;
        }

        for ( UInt32 Index = 0; Index < 8; Index++ )
        {
            Memory->Base[ Address + Index ] = (UInt8)( ( Value >> ( Index * 8 ) ) & 0xFF );
        }

        return True;
    }
}

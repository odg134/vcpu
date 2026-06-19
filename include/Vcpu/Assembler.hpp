#pragma once

#include "Types.hpp"
#include <string>
#include <vector>

namespace Vcpu
{
    //
    // Result of an assembly run. On success Image holds the finished
    // program; otherwise Error and ErrorLine describe the failure.
    //
    typedef struct _AssemblyResult
    {
        Boolean Success;
        std::vector< UInt8 > Image;
        std::string Error;
        UInt64 ErrorLine;
    } AssemblyResult;

    void Assemble( const std::string& Source, AssemblyResult* Result );

    //
    // Render the encoded instruction at Address into a readable line. Used
    // by the tracer and the disassembler.
    //
    std::string Disassemble( const UInt8* Image, UInt64 ImageSize, UInt64 Address );
}

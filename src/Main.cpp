#include "Vcpu/Types.hpp"
#include "Vcpu/Memory.hpp"
#include "Vcpu/Cpu.hpp"
#include "Vcpu/Assembler.hpp"
#include "Vcpu/Instruction.hpp"

#include <cstdio>
#include <fstream>
#include <string>
#include <vector>

using namespace Vcpu;

static const UInt64 DefaultMemorySize = 1ull << 20;

static Boolean EndsWith( const std::string& Text, const std::string& Suffix )
{
    if ( Text.size() < Suffix.size() )
    {
        return False;
    }

    return Text.compare( Text.size() - Suffix.size(), Suffix.size(), Suffix ) == 0;
}

static Boolean ReadFileText( const std::string& Path, std::string* Out )
{
    std::ifstream Stream( Path, std::ios::binary );

    if ( !Stream )
    {
        return False;
    }

    Out->assign( ( std::istreambuf_iterator< char >( Stream ) ), std::istreambuf_iterator< char >() );
    return True;
}

static Boolean ReadFileBinary( const std::string& Path, std::vector< UInt8 >* Out )
{
    std::ifstream Stream( Path, std::ios::binary );

    if ( !Stream )
    {
        return False;
    }

    Out->assign( ( std::istreambuf_iterator< char >( Stream ) ), std::istreambuf_iterator< char >() );
    return True;
}

static Boolean WriteFileBinary( const std::string& Path, const std::vector< UInt8 >& Data )
{
    std::ofstream Stream( Path, std::ios::binary );

    if ( !Stream )
    {
        return False;
    }

    if ( !Data.empty() )
    {
        Stream.write( (const char*)Data.data(), (std::streamsize)Data.size() );
    }

    return (Boolean)Stream.good();
}

//
// Turn a source path into a flat program image: assemble it when it is a
// .vasm listing, otherwise load it as a raw .vbin image.
//
static Boolean BuildImage( const std::string& Path, std::vector< UInt8 >* Image )
{
    if ( EndsWith( Path, ".vbin" ) )
    {
        if ( !ReadFileBinary( Path, Image ) )
        {
            std::fprintf( stderr, "vcpu: cannot read '%s'\n", Path.c_str() );
            return False;
        }

        return True;
    }

    std::string Source;

    if ( !ReadFileText( Path, &Source ) )
    {
        std::fprintf( stderr, "vcpu: cannot read '%s'\n", Path.c_str() );
        return False;
    }

    AssemblyResult Result;
    Assemble( Source, &Result );

    if ( !Result.Success )
    {
        std::fprintf( stderr, "vcpu: %s:%llu: %s\n", Path.c_str(), (unsigned long long)Result.ErrorLine, Result.Error.c_str() );
        return False;
    }

    *Image = Result.Image;
    return True;
}

static int CommandRun( const std::vector< std::string >& Args )
{
    if ( Args.empty() )
    {
        std::fprintf( stderr, "vcpu: run expects a program path\n" );
        return 2;
    }

    std::string Path;
    UInt64 MemorySize = DefaultMemorySize;
    Boolean Trace = False;
    Boolean Stats = False;

    for ( UInt64 Index = 0; Index < Args.size(); Index++ )
    {
        const std::string& Arg = Args[ Index ];

        if ( Arg == "--trace" )
        {
            Trace = True;
        }
        else if ( Arg == "--stats" )
        {
            Stats = True;
        }
        else if ( Arg == "--mem" )
        {
            if ( Index + 1 >= Args.size() )
            {
                std::fprintf( stderr, "vcpu: --mem expects a size in bytes\n" );
                return 2;
            }

            MemorySize = std::strtoull( Args[ ++Index ].c_str(), nullptr, 0 );
        }
        else if ( !Arg.empty() && Arg[ 0 ] == '-' )
        {
            std::fprintf( stderr, "vcpu: unknown option '%s'\n", Arg.c_str() );
            return 2;
        }
        else
        {
            Path = Arg;
        }
    }

    if ( Path.empty() )
    {
        std::fprintf( stderr, "vcpu: run expects a program path\n" );
        return 2;
    }

    std::vector< UInt8 > Image;

    if ( !BuildImage( Path, &Image ) )
    {
        return 1;
    }

    if ( MemorySize == 0 || Image.size() > MemorySize )
    {
        std::fprintf( stderr, "vcpu: program image (%llu bytes) does not fit in %llu bytes of memory\n",
            (unsigned long long)Image.size(), (unsigned long long)MemorySize );
        return 1;
    }

    MemoryContext Memory;

    if ( !MemoryCreate( &Memory, MemorySize ) )
    {
        std::fprintf( stderr, "vcpu: failed to allocate %llu bytes of guest memory\n", (unsigned long long)MemorySize );
        return 1;
    }

    MemoryWriteBlock( &Memory, 0, Image.data(), Image.size() );

    CpuContext Cpu;
    CpuInitialize( &Cpu, &Memory, 0 );
    Cpu.Trace = Trace;

    UInt64 ExitCode = CpuRun( &Cpu );

    if ( Stats )
    {
        std::fprintf( stderr, "vcpu: retired %llu instructions\n", (unsigned long long)Cpu.InstructionsRetired );
    }

    int Status;

    if ( Cpu.StopReason == StopReasonType::Halted )
    {
        Status = (int)( ExitCode & 0xFF );
    }
    else
    {
        std::fprintf( stderr, "vcpu: stopped (%s) at pc=0x%llx\n",
            CpuStopReasonText( Cpu.StopReason ), (unsigned long long)Cpu.ProgramCounter );
        Status = 1;
    }

    MemoryDestroy( &Memory );
    return Status;
}

static int CommandAsm( const std::vector< std::string >& Args )
{
    std::string Input;
    std::string Output;

    for ( UInt64 Index = 0; Index < Args.size(); Index++ )
    {
        if ( Args[ Index ] == "-o" )
        {
            if ( Index + 1 >= Args.size() )
            {
                std::fprintf( stderr, "vcpu: -o expects a path\n" );
                return 2;
            }

            Output = Args[ ++Index ];
        }
        else
        {
            Input = Args[ Index ];
        }
    }

    if ( Input.empty() || Output.empty() )
    {
        std::fprintf( stderr, "vcpu: asm expects <input.vasm> -o <output.vbin>\n" );
        return 2;
    }

    std::string Source;

    if ( !ReadFileText( Input, &Source ) )
    {
        std::fprintf( stderr, "vcpu: cannot read '%s'\n", Input.c_str() );
        return 1;
    }

    AssemblyResult Result;
    Assemble( Source, &Result );

    if ( !Result.Success )
    {
        std::fprintf( stderr, "vcpu: %s:%llu: %s\n", Input.c_str(), (unsigned long long)Result.ErrorLine, Result.Error.c_str() );
        return 1;
    }

    if ( !WriteFileBinary( Output, Result.Image ) )
    {
        std::fprintf( stderr, "vcpu: cannot write '%s'\n", Output.c_str() );
        return 1;
    }

    std::fprintf( stderr, "vcpu: wrote %llu bytes to %s\n", (unsigned long long)Result.Image.size(), Output.c_str() );
    return 0;
}

static int CommandDisasm( const std::vector< std::string >& Args )
{
    if ( Args.empty() )
    {
        std::fprintf( stderr, "vcpu: disasm expects a program path\n" );
        return 2;
    }

    std::vector< UInt8 > Image;

    if ( !BuildImage( Args[ 0 ], &Image ) )
    {
        return 1;
    }

    UInt64 Address = 0;

    while ( Address + InstructionSize <= Image.size() )
    {
        std::string Line = Disassemble( Image.data(), Image.size(), Address );
        std::printf( "%08llx:  %s\n", (unsigned long long)Address, Line.c_str() );
        Address += InstructionSize;
    }

    return 0;
}

static void PrintUsage( void )
{
    std::printf(
        "vcpu - a lightweight virtual CPU emulator for Windows\n"
        "\n"
        "usage:\n"
        "  vcpu run    <program.vasm|.vbin> [--mem <bytes>] [--trace] [--stats]\n"
        "  vcpu asm    <program.vasm> -o <program.vbin>\n"
        "  vcpu disasm <program.vasm|.vbin>\n" );
}

int main( int Argc, char** Argv )
{
    if ( Argc < 2 )
    {
        PrintUsage();
        return 2;
    }

    std::string Command = Argv[ 1 ];
    std::vector< std::string > Args;

    for ( int Index = 2; Index < Argc; Index++ )
    {
        Args.push_back( Argv[ Index ] );
    }

    if ( Command == "run" )
    {
        return CommandRun( Args );
    }

    if ( Command == "asm" )
    {
        return CommandAsm( Args );
    }

    if ( Command == "disasm" )
    {
        return CommandDisasm( Args );
    }

    if ( Command == "help" || Command == "--help" || Command == "-h" )
    {
        PrintUsage();
        return 0;
    }

    std::fprintf( stderr, "vcpu: unknown command '%s'\n", Command.c_str() );
    PrintUsage();
    return 2;
}

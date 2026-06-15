cmake_minimum_required( VERSION 3.15 )

project( Vcpu LANGUAGES CXX )

set( CMAKE_CXX_STANDARD 17 )
set( CMAKE_CXX_STANDARD_REQUIRED ON )

if ( NOT WIN32 )
    message( FATAL_ERROR "vcpu works on Win only as the host uses the Win32 api. Please fix your installation." )
endif()

if ( MSVC )
    add_compile_options( /W4 /permissive- )
else()
    add_compile_options( -Wall -Wextra )
endif()

add_executable( vcpu
    src/Main.cpp
    src/Cpu.cpp
    src/Memory.cpp
    src/Opcodes.cpp
    src/Instruction.cpp
    src/Assembler.cpp
)

target_include_directories( vcpu PRIVATE include )

#include "Vcpu/Assembler.hpp"
#include "Vcpu/Opcodes.hpp"
#include "Vcpu/Instruction.hpp"
#include "Vcpu/Cpu.hpp"

#include <cstdio>
#include <map>

namespace Vcpu
{
    typedef struct _PendingItem
    {
        Boolean IsInstruction;

        OpcodeType Opcode;
        UInt8 RegDst;
        UInt8 RegSrc;
        UInt64 Immediate;
        Boolean ImmediateIsLabel;
        std::string ImmediateLabel;

        std::vector< UInt8 > Data;

        UInt64 Address;
        UInt64 Line;
    } PendingItem;

    static void Fail( AssemblyResult* Result, UInt64 Line, const std::string& Message )
    {
        Result->Success = False;
        Result->Error = Message;
        Result->ErrorLine = Line;
    }

    static std::string ToLower( const std::string& Text )
    {
        std::string Out = Text;

        for ( char& Character : Out )
        {
            if ( Character >= 'A' && Character <= 'Z' )
            {
                Character = (char)( Character + ( 'a' - 'A' ) );
            }
        }

        return Out;
    }

    static Boolean IsIdentifierStart( char Character )
    {
        return ( Character >= 'A' && Character <= 'Z' )
            || ( Character >= 'a' && Character <= 'z' )
            || Character == '_'
            || Character == '.';
    }

    static Boolean IsIdentifierBody( char Character )
    {
        return IsIdentifierStart( Character ) || ( Character >= '0' && Character <= '9' );
    }

    static Boolean IsIdentifier( const std::string& Text )
    {
        if ( Text.empty() || !IsIdentifierStart( Text[ 0 ] ) )
        {
            return False;
        }

        for ( char Character : Text )
        {
            if ( !IsIdentifierBody( Character ) )
            {
                return False;
            }
        }

        return True;
    }

    static std::vector< std::string > SplitLines( const std::string& Source )
    {
        std::vector< std::string > Lines;
        std::string Current;

        for ( char Character : Source )
        {
            if ( Character == '\n' )
            {
                Lines.push_back( Current );
                Current.clear();
            }
            else if ( Character != '\r' )
            {
                Current.push_back( Character );
            }
        }

        Lines.push_back( Current );

        return Lines;
    }

    //
    // Split a line into tokens. Whitespace and commas separate; double
    // quoted strings are kept whole ( quotes included ); a semicolon
    // outside quotes starts a comment.
    //
    static Boolean Tokenize( const std::string& Line, std::vector< std::string >* Tokens, std::string* Error )
    {
        UInt64 Index = 0;
        UInt64 Length = Line.size();

        while ( Index < Length )
        {
            char Character = Line[ Index ];

            if ( Character == ';' )
            {
                break;
            }

            if ( Character == ' ' || Character == '\t' || Character == ',' )
            {
                Index++;
                continue;
            }

            if ( Character == '"' || Character == '\'' )
            {
                //
                // Quoted strings and character literals are kept whole, so a
                // space or comma inside them is not mistaken for a separator.
                //
                char Quote = Character;
                std::string Token( 1, Quote );
                Index++;

                Boolean Terminated = False;

                while ( Index < Length )
                {
                    char Inner = Line[ Index ];
                    Token.push_back( Inner );
                    Index++;

                    if ( Inner == '\\' && Index < Length )
                    {
                        Token.push_back( Line[ Index ] );
                        Index++;
                    }
                    else if ( Inner == Quote )
                    {
                        Terminated = True;
                        break;
                    }
                }

                if ( !Terminated )
                {
                    *Error = ( Quote == '"' ) ? "unterminated string literal" : "unterminated character literal";
                    return False;
                }

                Tokens->push_back( Token );
                continue;
            }

            std::string Token;

            while ( Index < Length )
            {
                char Inner = Line[ Index ];

                if ( Inner == ' ' || Inner == '\t' || Inner == ',' || Inner == ';' )
                {
                    break;
                }

                Token.push_back( Inner );
                Index++;
            }

            Tokens->push_back( Token );
        }

        return True;
    }

    static Boolean ParseRegister( const std::string& Token, UInt8* Index )
    {
        std::string Lower = ToLower( Token );

        if ( Lower == "sp" )
        {
            *Index = RegisterSp;
            return True;
        }

        if ( Lower.size() < 2 || Lower[ 0 ] != 'r' )
        {
            return False;
        }

        UInt32 Number = 0;

        for ( UInt64 At = 1; At < Lower.size(); At++ )
        {
            char Digit = Lower[ At ];

            if ( Digit < '0' || Digit > '9' )
            {
                return False;
            }

            Number = ( Number * 10 ) + (UInt32)( Digit - '0' );
        }

        if ( Number >= GeneralRegisterCount )
        {
            return False;
        }

        *Index = (UInt8)Number;
        return True;
    }

    static Boolean UnescapeChar( char Escaped, char* Out )
    {
        switch ( Escaped )
        {
        case 'n': *Out = '\n'; return True;
        case 't': *Out = '\t'; return True;
        case 'r': *Out = '\r'; return True;
        case '0': *Out = '\0'; return True;
        case '\\': *Out = '\\'; return True;
        case '"': *Out = '"'; return True;
        case '\'': *Out = '\''; return True;
        }

        *Out = Escaped;
        return True;
    }

    //
    // Parse a numeric or character literal. Label references are reported
    // back through IsLabel so the caller can defer them to pass two.
    //
    static Boolean ParseImmediate( const std::string& Token, UInt64* Value, Boolean* IsLabel, std::string* Label, std::string* Error )
    {
        *IsLabel = False;

        if ( Token.empty() )
        {
            *Error = "missing operand";
            return False;
        }

        if ( Token.front() == '\'' )
        {
            if ( Token.size() < 3 || Token.back() != '\'' )
            {
                *Error = "malformed character literal";
                return False;
            }

            std::string Inner = Token.substr( 1, Token.size() - 2 );
            char Resolved = 0;

            if ( Inner[ 0 ] == '\\' )
            {
                if ( Inner.size() != 2 )
                {
                    *Error = "malformed character literal";
                    return False;
                }

                UnescapeChar( Inner[ 1 ], &Resolved );
            }
            else
            {
                if ( Inner.size() != 1 )
                {
                    *Error = "malformed character literal";
                    return False;
                }

                Resolved = Inner[ 0 ];
            }

            *Value = (UInt64)(UInt8)Resolved;
            return True;
        }

        UInt64 At = 0;
        Boolean Negative = False;

        if ( Token[ At ] == '-' )
        {
            Negative = True;
            At++;
        }
        else if ( Token[ At ] == '+' )
        {
            At++;
        }

        if ( At < Token.size() && Token[ At ] == '0' && At + 1 < Token.size() && ( Token[ At + 1 ] == 'x' || Token[ At + 1 ] == 'X' ) )
        {
            UInt64 Result = 0;
            At += 2;

            if ( At >= Token.size() )
            {
                *Error = "malformed hex literal";
                return False;
            }

            for ( ; At < Token.size(); At++ )
            {
                char Digit = Token[ At ];
                UInt64 Nibble;

                if ( Digit >= '0' && Digit <= '9' )
                {
                    Nibble = (UInt64)( Digit - '0' );
                }
                else if ( Digit >= 'a' && Digit <= 'f' )
                {
                    Nibble = (UInt64)( Digit - 'a' + 10 );
                }
                else if ( Digit >= 'A' && Digit <= 'F' )
                {
                    Nibble = (UInt64)( Digit - 'A' + 10 );
                }
                else
                {
                    *Error = "malformed hex literal";
                    return False;
                }

                Result = ( Result << 4 ) | Nibble;
            }

            *Value = Negative ? (UInt64)( -(Int64)Result ) : Result;
            return True;
        }

        if ( At < Token.size() && Token[ At ] >= '0' && Token[ At ] <= '9' )
        {
            UInt64 Result = 0;

            for ( ; At < Token.size(); At++ )
            {
                char Digit = Token[ At ];

                if ( Digit < '0' || Digit > '9' )
                {
                    *Error = "malformed decimal literal";
                    return False;
                }

                Result = ( Result * 10 ) + (UInt64)( Digit - '0' );
            }

            *Value = Negative ? (UInt64)( -(Int64)Result ) : Result;
            return True;
        }

        //
        // Anything else is treated as a label reference.
        //
        if ( !IsIdentifier( Token ) )
        {
            *Error = "invalid operand '" + Token + "'";
            return False;
        }

        *IsLabel = True;
        *Label = Token;
        *Value = 0;
        return True;
    }

    static Boolean ParseNumber( const std::string& Token, UInt64* Value, std::string* Error )
    {
        Boolean IsLabel = False;
        std::string Label;

        if ( !ParseImmediate( Token, Value, &IsLabel, &Label, Error ) )
        {
            return False;
        }

        if ( IsLabel )
        {
            *Error = "expected a number, found label '" + Token + "'";
            return False;
        }

        return True;
    }

    static void AppendLittleEndian( std::vector< UInt8 >* Data, UInt64 Value, UInt32 ByteCount )
    {
        for ( UInt32 Index = 0; Index < ByteCount; Index++ )
        {
            Data->push_back( (UInt8)( ( Value >> ( Index * 8 ) ) & 0xFF ) );
        }
    }

    static Boolean ParseStringLiteral( const std::string& Token, std::string* Out, std::string* Error )
    {
        if ( Token.size() < 2 || Token.front() != '"' || Token.back() != '"' )
        {
            *Error = "expected a quoted string";
            return False;
        }

        std::string Inner = Token.substr( 1, Token.size() - 2 );
        std::string Result;

        for ( UInt64 Index = 0; Index < Inner.size(); Index++ )
        {
            if ( Inner[ Index ] == '\\' && Index + 1 < Inner.size() )
            {
                char Resolved = 0;
                UnescapeChar( Inner[ Index + 1 ], &Resolved );
                Result.push_back( Resolved );
                Index++;
            }
            else
            {
                Result.push_back( Inner[ Index ] );
            }
        }

        *Out = Result;
        return True;
    }

    static Boolean ParseDirective( const std::vector< std::string >& Tokens, std::vector< UInt8 >* Data, std::string* Error )
    {
        std::string Name = ToLower( Tokens[ 0 ] );

        if ( Name == ".string" || Name == ".asciz" || Name == ".ascii" )
        {
            if ( Tokens.size() != 2 )
            {
                *Error = Name + " expects a single string operand";
                return False;
            }

            std::string Text;

            if ( !ParseStringLiteral( Tokens[ 1 ], &Text, Error ) )
            {
                return False;
            }

            for ( char Character : Text )
            {
                Data->push_back( (UInt8)Character );
            }

            if ( Name != ".ascii" )
            {
                Data->push_back( 0 );
            }

            return True;
        }

        if ( Name == ".byte" || Name == ".word" || Name == ".dword" || Name == ".qword" )
        {
            UInt32 ByteCount = 1;

            if ( Name == ".word" )
            {
                ByteCount = 2;
            }
            else if ( Name == ".dword" )
            {
                ByteCount = 4;
            }
            else if ( Name == ".qword" )
            {
                ByteCount = 8;
            }

            if ( Tokens.size() < 2 )
            {
                *Error = Name + " expects at least one value";
                return False;
            }

            for ( UInt64 Index = 1; Index < Tokens.size(); Index++ )
            {
                UInt64 Value = 0;

                if ( !ParseNumber( Tokens[ Index ], &Value, Error ) )
                {
                    return False;
                }

                AppendLittleEndian( Data, Value, ByteCount );
            }

            return True;
        }

        if ( Name == ".space" )
        {
            if ( Tokens.size() != 2 )
            {
                *Error = ".space expects a single length";
                return False;
            }

            UInt64 Length = 0;

            if ( !ParseNumber( Tokens[ 1 ], &Length, Error ) )
            {
                return False;
            }

            for ( UInt64 Index = 0; Index < Length; Index++ )
            {
                Data->push_back( 0 );
            }

            return True;
        }

        *Error = "unknown directive '" + Tokens[ 0 ] + "'";
        return False;
    }

    static Boolean ParseOperands( const OpcodeInfo* Info, const std::vector< std::string >& Tokens, PendingItem* Item, std::string* Error )
    {
        UInt64 Count = Tokens.size() - 1;

        auto ReadRegister = [ & ]( UInt64 TokenIndex, UInt8* Out ) -> Boolean
        {
            if ( !ParseRegister( Tokens[ TokenIndex ], Out ) )
            {
                *Error = "invalid register '" + Tokens[ TokenIndex ] + "'";
                return False;
            }

            return True;
        };

        auto ReadImmediate = [ & ]( UInt64 TokenIndex ) -> Boolean
        {
            return ParseImmediate( Tokens[ TokenIndex ], &Item->Immediate, &Item->ImmediateIsLabel, &Item->ImmediateLabel, Error );
        };

        switch ( Info->Form )
        {
        case OperandFormType::None:
            if ( Count != 0 )
            {
                *Error = std::string( Info->Mnemonic ) + " takes no operands";
                return False;
            }
            return True;

        case OperandFormType::R:
            if ( Count != 1 )
            {
                *Error = std::string( Info->Mnemonic ) + " expects one register";
                return False;
            }
            return ReadRegister( 1, &Item->RegDst );

        case OperandFormType::Rr:
            if ( Count != 2 )
            {
                *Error = std::string( Info->Mnemonic ) + " expects two registers";
                return False;
            }
            return ReadRegister( 1, &Item->RegDst ) && ReadRegister( 2, &Item->RegSrc );

        case OperandFormType::Ri:
            if ( Count != 2 )
            {
                *Error = std::string( Info->Mnemonic ) + " expects a register and an immediate";
                return False;
            }
            return ReadRegister( 1, &Item->RegDst ) && ReadImmediate( 2 );

        case OperandFormType::Rri:
            if ( Count != 2 && Count != 3 )
            {
                *Error = std::string( Info->Mnemonic ) + " expects two registers and an optional immediate";
                return False;
            }

            if ( !ReadRegister( 1, &Item->RegDst ) || !ReadRegister( 2, &Item->RegSrc ) )
            {
                return False;
            }

            if ( Count == 3 )
            {
                return ReadImmediate( 3 );
            }

            Item->Immediate = 0;
            return True;

        case OperandFormType::Imm:
            if ( Count != 1 )
            {
                *Error = std::string( Info->Mnemonic ) + " expects one immediate";
                return False;
            }
            return ReadImmediate( 1 );
        }

        *Error = "internal: unhandled operand form";
        return False;
    }

    void Assemble( const std::string& Source, AssemblyResult* Result )
    {
        Result->Success = True;
        Result->Error.clear();
        Result->ErrorLine = 0;
        Result->Image.clear();

        std::vector< std::string > Lines = SplitLines( Source );
        std::vector< PendingItem > Items;
        std::map< std::string, UInt64 > Labels;

        UInt64 Address = 0;

        for ( UInt64 LineIndex = 0; LineIndex < Lines.size(); LineIndex++ )
        {
            UInt64 LineNumber = LineIndex + 1;

            std::vector< std::string > Tokens;
            std::string Error;

            if ( !Tokenize( Lines[ LineIndex ], &Tokens, &Error ) )
            {
                Fail( Result, LineNumber, Error );
                return;
            }

            //
            // Consume any leading labels; each binds to the current address.
            //
            while ( !Tokens.empty() && Tokens[ 0 ].size() > 1 && Tokens[ 0 ].back() == ':' )
            {
                std::string Name = Tokens[ 0 ].substr( 0, Tokens[ 0 ].size() - 1 );

                if ( !IsIdentifier( Name ) )
                {
                    Fail( Result, LineNumber, "invalid label '" + Name + "'" );
                    return;
                }

                if ( Labels.count( Name ) != 0 )
                {
                    Fail( Result, LineNumber, "duplicate label '" + Name + "'" );
                    return;
                }

                Labels[ Name ] = Address;
                Tokens.erase( Tokens.begin() );
            }

            if ( Tokens.empty() )
            {
                continue;
            }

            PendingItem Item;
            Item.IsInstruction = False;
            Item.Opcode = OpcodeType::Nop;
            Item.RegDst = 0;
            Item.RegSrc = 0;
            Item.Immediate = 0;
            Item.ImmediateIsLabel = False;
            Item.Address = Address;
            Item.Line = LineNumber;

            if ( Tokens[ 0 ][ 0 ] == '.' )
            {
                if ( !ParseDirective( Tokens, &Item.Data, &Error ) )
                {
                    Fail( Result, LineNumber, Error );
                    return;
                }

                Address += Item.Data.size();
            }
            else
            {
                const OpcodeInfo* Info = OpcodeLookupByMnemonic( Tokens[ 0 ].c_str() );

                if ( Info == nullptr )
                {
                    Fail( Result, LineNumber, "unknown mnemonic '" + Tokens[ 0 ] + "'" );
                    return;
                }

                Item.IsInstruction = True;
                Item.Opcode = Info->Opcode;

                if ( !ParseOperands( Info, Tokens, &Item, &Error ) )
                {
                    Fail( Result, LineNumber, Error );
                    return;
                }

                Address += InstructionSize;
            }

            Items.push_back( Item );
        }

        Result->Image.resize( (size_t)Address, 0 );

        for ( const PendingItem& Item : Items )
        {
            if ( Item.IsInstruction )
            {
                UInt64 Immediate = Item.Immediate;

                if ( Item.ImmediateIsLabel )
                {
                    auto Found = Labels.find( Item.ImmediateLabel );

                    if ( Found == Labels.end() )
                    {
                        Fail( Result, Item.Line, "undefined lbael '" + Item.ImmediateLabel + "'" );
                        return;
                    }

                    Immediate = Found->second;
                }

                DecodedInstruction Decoded;
                Decoded.Opcode = Item.Opcode;
                Decoded.RegDst = Item.RegDst;
                Decoded.RegSrc = Item.RegSrc;
                Decoded.Immediate = Immediate;

                InstructionEncode( &Decoded, &Result->Image[ (size_t)Item.Address ] );
            }
            else
            {
                for ( UInt64 Index = 0; Index < Item.Data.size(); Index++ )
                {
                    Result->Image[ (size_t)( Item.Address + Index ) ] = Item.Data[ Index ];
                }
            }
        }

        Result->Success = True;
    }

    static std::string RegisterName( UInt8 Index )
    {
        if ( Index == RegisterSp )
        {
            return "sp";
        }

        char Buffer[ 8 ];
        std::snprintf( Buffer, sizeof( Buffer ), "r%u", (unsigned)Index );
        return Buffer;
    }

    static std::string FormatHex( UInt64 Value )
    {
        char Buffer[ 32 ];
        std::snprintf( Buffer, sizeof( Buffer ), "0x%llx", (unsigned long long)Value );
        return Buffer;
    }

    std::string Disassemble( const UInt8* Image, UInt64 ImageSize, UInt64 Address )
    {
        if ( Address > ImageSize || InstructionSize > ImageSize - Address )
        {
            return "<truncated>";
        }

        DecodedInstruction Decoded;
        InstructionDecode( &Image[ Address ], &Decoded );

        const OpcodeInfo* Info = OpcodeLookupByOpcode( Decoded.Opcode );

        if ( Info == nullptr )
        {
            return ".byte " + FormatHex( (UInt64)Image[ Address ] );
        }

        std::string Text = Info->Mnemonic;

        switch ( Info->Form )
        {
        case OperandFormType::None:
            break;

        case OperandFormType::R:
            Text += " " + RegisterName( Decoded.RegDst );
            break;

        case OperandFormType::Rr:
            Text += " " + RegisterName( Decoded.RegDst ) + ", " + RegisterName( Decoded.RegSrc );
            break;

        case OperandFormType::Ri:
            Text += " " + RegisterName( Decoded.RegDst ) + ", " + FormatHex( Decoded.Immediate );
            break;

        case OperandFormType::Rri:
            Text += " " + RegisterName( Decoded.RegDst ) + ", " + RegisterName( Decoded.RegSrc ) + ", " + FormatHex( Decoded.Immediate );
            break;

        case OperandFormType::Imm:
            Text += " " + FormatHex( Decoded.Immediate );
            break;
        }

        return Text;
    }
}

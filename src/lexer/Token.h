#pragma once
#include <string>

namespace pgt
{

    enum class Tok
    {
        Eof,
        Ident,
        Int,
        Float,
        Char,
        String,
        KwPackage,
        KwFn,
        KwVar,
        KwVarManual,
        KwMap,
        KwReturn,
        KwUse,
        KwIf,
        KwElse,
        KwWhile,
        KwFor,
        KwIn,
        KwBreak,
        KwContinue,
        KwTrue,
        KwFalse,
        KwStruct,
        KwInterface,
        KwConst,
        KwSwitch,
        KwCase,
        KwDefault,
        KwNil,
        KwExtern,
        KwAsm,
        LParen,
        RParen,
        LBrace,
        RBrace,
        LBracket,
        RBracket,
        Comma,
        Colon,
        ColonAssign,
        Semi,
        Dot,
        DotDot,
        Ellipsis,
        Assign,
        PlusAssign,
        MinusAssign,
        StarAssign,
        SlashAssign,
        PercentAssign,
        PlusPlus,
        MinusMinus,
        Plus,
        Minus,
        Star,
        Slash,
        Percent,
        Eq,
        Ne,
        Lt,
        Gt,
        Le,
        Ge,
        AndAnd,
        OrOr,
        Bang,
        Amp,
        Pipe,
        Caret,
        Tilde,
        Shl,
        Shr,
        Question,
        KwDefer,
    };

    struct Token
    {
        Tok kind = Tok::Eof;
        std::string text;
        int line = 1;
        int col = 1;
        int endLine = 0;
    };

    const char *tokName(Tok kind);
    const char *tokKindName(Tok kind);

}

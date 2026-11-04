#pragma once
#include <string>

namespace pgt {

enum class Tok {
    Eof,
    Ident,
    Int,
    String,
    KwPackage,
    KwFn,
    KwVar,
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
    LParen,
    RParen,
    LBrace,
    RBrace,
    Comma,
    Semi,
    Dot,
    DotDot,
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
};

struct Token {
    Tok kind = Tok::Eof;
    std::string text;
    int line = 1;
    int col = 1;
};

const char* tokName(Tok kind);

}

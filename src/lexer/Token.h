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

    LParen,
    RParen,
    LBrace,
    RBrace,
    Comma,

    Assign,
    Plus,
    Minus,
    Star,
    Slash,
};

struct Token {
    Tok kind = Tok::Eof;
    std::string text;
    int line = 1;
    int col = 1;
};

const char* tokName(Tok kind);

}

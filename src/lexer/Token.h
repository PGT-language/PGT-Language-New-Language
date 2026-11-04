#pragma once
#include <string>

namespace pgt {

enum class Tok {
    Eof,
    // literals / names
    Ident,
    Int,
    String,
    // keywords
    KwPackage,
    KwFn,
    KwVar, // `var*`
    KwReturn,
    // punctuation
    LParen,
    RParen,
    LBrace,
    RBrace,
    Comma,
    // operators
    Assign,
    Plus,
    Minus,
    Star,
    Slash,
};

struct Token {
    Tok kind = Tok::Eof;
    std::string text; // identifier name, string contents (unescaped) or number digits
    int line = 1;
    int col = 1;
};

const char* tokName(Tok kind);

} // namespace pgt

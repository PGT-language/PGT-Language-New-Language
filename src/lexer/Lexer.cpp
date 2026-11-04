#include "lexer/Lexer.h"

#include "common/Error.h"

#include <cctype>

namespace pgt {

const char* tokName(Tok kind) {
    switch (kind) {
    case Tok::Eof: return "end of file";
    case Tok::Ident: return "identifier";
    case Tok::Int: return "integer literal";
    case Tok::String: return "string literal";
    case Tok::KwPackage: return "'package'";
    case Tok::KwFn: return "'fn'";
    case Tok::KwVar: return "'var*'";
    case Tok::KwReturn: return "'return'";
    case Tok::LParen: return "'('";
    case Tok::RParen: return "')'";
    case Tok::LBrace: return "'{'";
    case Tok::RBrace: return "'}'";
    case Tok::Comma: return "','";
    case Tok::Assign: return "'='";
    case Tok::Plus: return "'+'";
    case Tok::Minus: return "'-'";
    case Tok::Star: return "'*'";
    case Tok::Slash: return "'/'";
    }
    return "?";
}

char Lexer::peek(size_t ahead) const {
    return pos_ + ahead < src_.size() ? src_[pos_ + ahead] : '\0';
}

char Lexer::advance() {
    char c = src_[pos_++];
    if (c == '\n') {
        ++line_;
        col_ = 1;
    } else {
        ++col_;
    }
    return c;
}

Token Lexer::make(Tok kind, std::string text, int line, int col) const {
    return Token{kind, std::move(text), line, col};
}

void Lexer::skipTrivia() {
    while (!atEnd()) {
        char c = peek();
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n') {
            advance();
        } else if (c == '/' && peek(1) == '/') {
            while (!atEnd() && peek() != '\n') advance();
        } else {
            break;
        }
    }
}

Token Lexer::lexNumber() {
    int line = line_, col = col_;
    std::string digits;
    while (std::isdigit(static_cast<unsigned char>(peek()))) digits += advance();
    return make(Tok::Int, std::move(digits), line, col);
}

Token Lexer::lexString() {
    int line = line_, col = col_;
    advance();
    std::string out;
    while (true) {
        if (atEnd() || peek() == '\n')
            throw CompileError("unterminated string literal", line, col);
        char c = advance();
        if (c == '"') break;
        if (c != '\\') {
            out += c;
            continue;
        }
        if (atEnd()) throw CompileError("unterminated string literal", line, col);
        int escLine = line_, escCol = col_ - 1;
        char e = advance();
        switch (e) {
        case 'n': out += '\n'; break;
        case 't': out += '\t'; break;
        case 'r': out += '\r'; break;
        case '0': out += '\0'; break;
        case '\\': out += '\\'; break;
        case '"': out += '"'; break;
        default:
            throw CompileError(std::string("unknown escape sequence '\\") + e + "'",
                               escLine, escCol);
        }
    }
    return make(Tok::String, std::move(out), line, col);
}

Token Lexer::lexWord() {
    int line = line_, col = col_;
    std::string word;
    while (std::isalnum(static_cast<unsigned char>(peek())) || peek() == '_') word += advance();

    if (word == "var" && peek() == '*') {
        advance();
        return make(Tok::KwVar, "var*", line, col);
    }
    if (word == "package") return make(Tok::KwPackage, word, line, col);
    if (word == "fn") return make(Tok::KwFn, word, line, col);
    if (word == "return") return make(Tok::KwReturn, word, line, col);
    return make(Tok::Ident, std::move(word), line, col);
}

std::vector<Token> Lexer::tokenize() {
    std::vector<Token> out;
    while (true) {
        skipTrivia();
        if (atEnd()) break;

        char c = peek();
        int line = line_, col = col_;

        if (std::isdigit(static_cast<unsigned char>(c))) {
            out.push_back(lexNumber());
        } else if (std::isalpha(static_cast<unsigned char>(c)) || c == '_') {
            out.push_back(lexWord());
        } else if (c == '"') {
            out.push_back(lexString());
        } else {
            advance();
            Tok kind;
            switch (c) {
            case '(': kind = Tok::LParen; break;
            case ')': kind = Tok::RParen; break;
            case '{': kind = Tok::LBrace; break;
            case '}': kind = Tok::RBrace; break;
            case ',': kind = Tok::Comma; break;
            case '=': kind = Tok::Assign; break;
            case '+': kind = Tok::Plus; break;
            case '-': kind = Tok::Minus; break;
            case '*': kind = Tok::Star; break;
            case '/': kind = Tok::Slash; break;
            default:
                throw CompileError(std::string("unexpected character '") + c + "'", line, col);
            }
            out.push_back(make(kind, std::string(1, c), line, col));
        }
    }
    out.push_back(make(Tok::Eof, "", line_, col_));
    return out;
}

}

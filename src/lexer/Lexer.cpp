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
    case Tok::KwUse: return "'#use'";
    case Tok::KwIf: return "'if'";
    case Tok::KwElse: return "'else'";
    case Tok::KwWhile: return "'while'";
    case Tok::KwFor: return "'for'";
    case Tok::KwIn: return "'in'";
    case Tok::KwBreak: return "'break'";
    case Tok::KwContinue: return "'continue'";
    case Tok::KwTrue: return "'true'";
    case Tok::KwFalse: return "'false'";
    case Tok::LParen: return "'('";
    case Tok::RParen: return "')'";
    case Tok::LBrace: return "'{'";
    case Tok::RBrace: return "'}'";
    case Tok::Comma: return "','";
    case Tok::Semi: return "';'";
    case Tok::Dot: return "'.'";
    case Tok::DotDot: return "'..'";
    case Tok::Assign: return "'='";
    case Tok::PlusAssign: return "'+='";
    case Tok::MinusAssign: return "'-='";
    case Tok::StarAssign: return "'*='";
    case Tok::SlashAssign: return "'/='";
    case Tok::PercentAssign: return "'%='";
    case Tok::PlusPlus: return "'++'";
    case Tok::MinusMinus: return "'--'";
    case Tok::Plus: return "'+'";
    case Tok::Minus: return "'-'";
    case Tok::Star: return "'*'";
    case Tok::Slash: return "'/'";
    case Tok::Percent: return "'%'";
    case Tok::Eq: return "'=='";
    case Tok::Ne: return "'!='";
    case Tok::Lt: return "'<'";
    case Tok::Gt: return "'>'";
    case Tok::Le: return "'<='";
    case Tok::Ge: return "'>='";
    case Tok::AndAnd: return "'&&'";
    case Tok::OrOr: return "'||'";
    case Tok::Bang: return "'!'";
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
    if (word == "if") return make(Tok::KwIf, word, line, col);
    if (word == "else") return make(Tok::KwElse, word, line, col);
    if (word == "while") return make(Tok::KwWhile, word, line, col);
    if (word == "for") return make(Tok::KwFor, word, line, col);
    if (word == "in") return make(Tok::KwIn, word, line, col);
    if (word == "break") return make(Tok::KwBreak, word, line, col);
    if (word == "continue") return make(Tok::KwContinue, word, line, col);
    if (word == "true") return make(Tok::KwTrue, word, line, col);
    if (word == "false") return make(Tok::KwFalse, word, line, col);
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
        } else if (c == '#') {
            advance();
            std::string word;
            while (std::isalpha(static_cast<unsigned char>(peek()))) word += advance();
            if (word != "use") throw CompileError("unknown directive '#" + word + "'", line, col);
            out.push_back(make(Tok::KwUse, "#use", line, col));
        } else {
            size_t start = pos_;
            advance();
            auto match = [&](char next) {
                if (peek() != next) return false;
                advance();
                return true;
            };
            Tok kind;
            switch (c) {
            case '(': kind = Tok::LParen; break;
            case ')': kind = Tok::RParen; break;
            case '{': kind = Tok::LBrace; break;
            case '}': kind = Tok::RBrace; break;
            case ',': kind = Tok::Comma; break;
            case ';': kind = Tok::Semi; break;
            case '.': kind = match('.') ? Tok::DotDot : Tok::Dot; break;
            case '=': kind = match('=') ? Tok::Eq : Tok::Assign; break;
            case '!': kind = match('=') ? Tok::Ne : Tok::Bang; break;
            case '<': kind = match('=') ? Tok::Le : Tok::Lt; break;
            case '>': kind = match('=') ? Tok::Ge : Tok::Gt; break;
            case '&':
                if (!match('&')) throw CompileError("unexpected character '&' (did you mean '&&'?)", line, col);
                kind = Tok::AndAnd;
                break;
            case '|':
                if (!match('|')) throw CompileError("unexpected character '|' (did you mean '||'?)", line, col);
                kind = Tok::OrOr;
                break;
            case '+': kind = match('+') ? Tok::PlusPlus : match('=') ? Tok::PlusAssign : Tok::Plus; break;
            case '-': kind = match('-') ? Tok::MinusMinus : match('=') ? Tok::MinusAssign : Tok::Minus; break;
            case '*': kind = match('=') ? Tok::StarAssign : Tok::Star; break;
            case '/': kind = match('=') ? Tok::SlashAssign : Tok::Slash; break;
            case '%': kind = match('=') ? Tok::PercentAssign : Tok::Percent; break;
            default:
                throw CompileError(std::string("unexpected character '") + c + "'", line, col);
            }
            out.push_back(make(kind, src_.substr(start, pos_ - start), line, col));
        }
    }
    out.push_back(make(Tok::Eof, "", line_, col_));
    return out;
}

}

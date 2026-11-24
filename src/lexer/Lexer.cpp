#include "lexer/Lexer.h"

#include "common/Error.h"

#include <cctype>

namespace pgt
{

    const char *tokName(Tok kind)
    {
        switch (kind)
        {
        case Tok::Eof:
            return "end of file";
        case Tok::Ident:
            return "identifier";
        case Tok::Int:
            return "integer literal";
        case Tok::Float:
            return "floating-point literal";
        case Tok::Char:
            return "char literal";
        case Tok::String:
            return "string literal";
        case Tok::KwPackage:
            return "'package'";
        case Tok::KwFn:
            return "'fn'";
        case Tok::KwVar:
            return "'var*'";
        case Tok::KwVarManual:
            return "'var&'";
        case Tok::KwMap:
            return "'map'";
        case Tok::KwAsm:
            return "'asm'";
        case Tok::KwReturn:
            return "'return'";
        case Tok::KwUse:
            return "'#use'";
        case Tok::KwIf:
            return "'if'";
        case Tok::KwElse:
            return "'else'";
        case Tok::KwWhile:
            return "'while'";
        case Tok::KwFor:
            return "'for'";
        case Tok::KwIn:
            return "'in'";
        case Tok::KwBreak:
            return "'break'";
        case Tok::KwContinue:
            return "'continue'";
        case Tok::KwTrue:
            return "'true'";
        case Tok::KwFalse:
            return "'false'";
        case Tok::KwStruct:
            return "'struct'";
        case Tok::KwInterface:
            return "'interface'";
        case Tok::KwConst:
            return "'const'";
        case Tok::KwSwitch:
            return "'switch'";
        case Tok::KwCase:
            return "'case'";
        case Tok::KwDefault:
            return "'default'";
        case Tok::KwNil:
            return "'nil'";
        case Tok::KwExtern:
            return "'extern'";
        case Tok::LBracket:
            return "'['";
        case Tok::RBracket:
            return "']'";
        case Tok::Colon:
            return "':'";
        case Tok::ColonAssign:
            return "':='";
        case Tok::Amp:
            return "'&'";
        case Tok::Pipe:
            return "'|'";
        case Tok::Caret:
            return "'^'";
        case Tok::Tilde:
            return "'~'";
        case Tok::Shl:
            return "'<<'";
        case Tok::Shr:
            return "'>>'";
        case Tok::LParen:
            return "'('";
        case Tok::RParen:
            return "')'";
        case Tok::LBrace:
            return "'{'";
        case Tok::RBrace:
            return "'}'";
        case Tok::Comma:
            return "','";
        case Tok::Semi:
            return "';'";
        case Tok::Dot:
            return "'.'";
        case Tok::DotDot:
            return "'..'";
        case Tok::Ellipsis:
            return "'...'";
        case Tok::Assign:
            return "'='";
        case Tok::PlusAssign:
            return "'+='";
        case Tok::MinusAssign:
            return "'-='";
        case Tok::StarAssign:
            return "'*='";
        case Tok::SlashAssign:
            return "'/='";
        case Tok::PercentAssign:
            return "'%='";
        case Tok::PlusPlus:
            return "'++'";
        case Tok::MinusMinus:
            return "'--'";
        case Tok::Plus:
            return "'+'";
        case Tok::Minus:
            return "'-'";
        case Tok::Star:
            return "'*'";
        case Tok::Slash:
            return "'/'";
        case Tok::Percent:
            return "'%'";
        case Tok::Eq:
            return "'=='";
        case Tok::Ne:
            return "'!='";
        case Tok::Lt:
            return "'<'";
        case Tok::Gt:
            return "'>'";
        case Tok::Le:
            return "'<='";
        case Tok::Ge:
            return "'>='";
        case Tok::AndAnd:
            return "'&&'";
        case Tok::OrOr:
            return "'||'";
        case Tok::Bang:
            return "'!'";
        }
        return "?";
    }

    const char *tokKindName(Tok kind)
    {
        switch (kind)
        {
        case Tok::Eof:
            return "Eof";
        case Tok::Ident:
            return "Ident";
        case Tok::Int:
            return "Int";
        case Tok::Float:
            return "Float";
        case Tok::Char:
            return "Char";
        case Tok::String:
            return "String";
        case Tok::KwPackage:
            return "KwPackage";
        case Tok::KwFn:
            return "KwFn";
        case Tok::KwVar:
            return "KwVar";
        case Tok::KwVarManual:
            return "KwVarManual";
        case Tok::KwMap:
            return "KwMap";
        case Tok::KwAsm:
            return "KwAsm";
        case Tok::KwReturn:
            return "KwReturn";
        case Tok::KwUse:
            return "KwUse";
        case Tok::KwIf:
            return "KwIf";
        case Tok::KwElse:
            return "KwElse";
        case Tok::KwWhile:
            return "KwWhile";
        case Tok::KwFor:
            return "KwFor";
        case Tok::KwIn:
            return "KwIn";
        case Tok::KwBreak:
            return "KwBreak";
        case Tok::KwContinue:
            return "KwContinue";
        case Tok::KwTrue:
            return "KwTrue";
        case Tok::KwFalse:
            return "KwFalse";
        case Tok::KwStruct:
            return "KwStruct";
        case Tok::KwInterface:
            return "KwInterface";
        case Tok::KwConst:
            return "KwConst";
        case Tok::KwSwitch:
            return "KwSwitch";
        case Tok::KwCase:
            return "KwCase";
        case Tok::KwDefault:
            return "KwDefault";
        case Tok::KwNil:
            return "KwNil";
        case Tok::KwExtern:
            return "KwExtern";
        case Tok::LParen:
            return "LParen";
        case Tok::RParen:
            return "RParen";
        case Tok::LBrace:
            return "LBrace";
        case Tok::RBrace:
            return "RBrace";
        case Tok::LBracket:
            return "LBracket";
        case Tok::RBracket:
            return "RBracket";
        case Tok::Comma:
            return "Comma";
        case Tok::Colon:
            return "Colon";
        case Tok::ColonAssign:
            return "ColonAssign";
        case Tok::Semi:
            return "Semi";
        case Tok::Dot:
            return "Dot";
        case Tok::DotDot:
            return "DotDot";
        case Tok::Ellipsis:
            return "Ellipsis";
        case Tok::Assign:
            return "Assign";
        case Tok::PlusAssign:
            return "PlusAssign";
        case Tok::MinusAssign:
            return "MinusAssign";
        case Tok::StarAssign:
            return "StarAssign";
        case Tok::SlashAssign:
            return "SlashAssign";
        case Tok::PercentAssign:
            return "PercentAssign";
        case Tok::PlusPlus:
            return "PlusPlus";
        case Tok::MinusMinus:
            return "MinusMinus";
        case Tok::Plus:
            return "Plus";
        case Tok::Minus:
            return "Minus";
        case Tok::Star:
            return "Star";
        case Tok::Slash:
            return "Slash";
        case Tok::Percent:
            return "Percent";
        case Tok::Eq:
            return "Eq";
        case Tok::Ne:
            return "Ne";
        case Tok::Lt:
            return "Lt";
        case Tok::Gt:
            return "Gt";
        case Tok::Le:
            return "Le";
        case Tok::Ge:
            return "Ge";
        case Tok::AndAnd:
            return "AndAnd";
        case Tok::OrOr:
            return "OrOr";
        case Tok::Bang:
            return "Bang";
        case Tok::Amp:
            return "Amp";
        case Tok::Pipe:
            return "Pipe";
        case Tok::Caret:
            return "Caret";
        case Tok::Tilde:
            return "Tilde";
        case Tok::Shl:
            return "Shl";
        case Tok::Shr:
            return "Shr";
        }
        return "?";
    }

    char Lexer::peek(size_t ahead) const
    {
        return pos_ + ahead < src_.size() ? src_[pos_ + ahead] : '\0';
    }

    char Lexer::advance()
    {
        char c = src_[pos_++];
        if (c == '\n')
        {
            ++line_;
            col_ = 1;
        }
        else
        {
            ++col_;
        }
        return c;
    }

    Token Lexer::make(Tok kind, std::string text, int line, int col) const
    {
        return Token{kind, std::move(text), line, col};
    }

    void Lexer::skipTrivia()
    {
        while (!atEnd())
        {
            char c = peek();
            if (c == ' ' || c == '\t' || c == '\r' || c == '\n')
            {
                advance();
            }
            else if (c == '/' && peek(1) == '/')
            {
                while (!atEnd() && peek() != '\n')
                    advance();
            }
            else if (c == '/' && peek(1) == '*')
            {
                int line = line_, col = col_;
                advance();
                advance();
                while (!(peek() == '*' && peek(1) == '/'))
                {
                    if (atEnd())
                        throw CompileError("unterminated block comment", line, col);
                    advance();
                }
                advance();
                advance();
            }
            else
            {
                break;
            }
        }
    }

    Token Lexer::lexNumber()
    {
        int line = line_, col = col_;
        std::string digits;
        if (peek() == '0' && (peek(1) == 'x' || peek(1) == 'X' || peek(1) == 'b' || peek(1) == 'B'))
        {
            bool hex = peek(1) == 'x' || peek(1) == 'X';
            advance();
            advance();
            std::string body;
            while (hex ? std::isxdigit(static_cast<unsigned char>(peek()))
                       : (peek() == '0' || peek() == '1'))
                body += advance();
            if (body.empty())
                throw CompileError(hex ? "expected hex digits after '0x'" : "expected binary digits after '0b'",
                                   line, col);
            unsigned long long v = 0;
            try
            {
                v = std::stoull(body, nullptr, hex ? 16 : 2);
            }
            catch (const std::out_of_range &)
            {
                throw CompileError("integer literal is too large", line, col);
            }
            return make(Tok::Int, std::to_string(v), line, col);
        }
        while (std::isdigit(static_cast<unsigned char>(peek())))
            digits += advance();
        if (peek() == '.' && std::isdigit(static_cast<unsigned char>(peek(1))))
        {
            digits += advance();
            while (std::isdigit(static_cast<unsigned char>(peek())))
                digits += advance();
            return make(Tok::Float, std::move(digits), line, col);
        }
        return make(Tok::Int, std::move(digits), line, col);
    }

    Token Lexer::lexChar()
    {
        int line = line_, col = col_;
        advance();
        if (atEnd() || peek() == '\n')
            throw CompileError("unterminated char literal", line, col);
        char c = advance();
        if (c == '\'')
            throw CompileError("empty char literal", line, col);
        if (c == '\\')
        {
            if (atEnd())
                throw CompileError("unterminated char literal", line, col);
            char e = advance();
            switch (e)
            {
            case 'n':
                c = '\n';
                break;
            case 't':
                c = '\t';
                break;
            case 'r':
                c = '\r';
                break;
            case '0':
                c = '\0';
                break;
            case '\\':
                c = '\\';
                break;
            case '\'':
                c = '\'';
                break;
            default:
                throw CompileError(std::string("unknown escape sequence '\\") + e + "'", line, col);
            }
        }
        else if (static_cast<unsigned char>(c) >= 0x80)
        {
            throw CompileError("non-ASCII char literals are not supported (char is one byte)", line,
                               col);
        }
        if (peek() != '\'')
            throw CompileError("a char literal must contain exactly one character", line, col);
        advance();
        return make(Tok::Char, std::string(1, c), line, col);
    }

    Token Lexer::lexString()
    {
        int line = line_, col = col_;
        advance();
        std::string out;
        while (true)
        {
            if (atEnd() || peek() == '\n')
                throw CompileError("unterminated string literal", line, col);
            char c = advance();
            if (c == '"')
                break;
            if (c != '\\')
            {
                out += c;
                continue;
            }
            if (atEnd())
                throw CompileError("unterminated string literal", line, col);
            int escLine = line_, escCol = col_ - 1;
            char e = advance();
            switch (e)
            {
            case 'n':
                out += '\n';
                break;
            case 't':
                out += '\t';
                break;
            case 'r':
                out += '\r';
                break;
            case '0':
                out += '\0';
                break;
            case '\\':
                out += '\\';
                break;
            case '"':
                out += '"';
                break;
            default:
                throw CompileError(std::string("unknown escape sequence '\\") + e + "'",
                                   escLine, escCol);
            }
        }
        return make(Tok::String, std::move(out), line, col);
    }

    Token Lexer::lexAsmBody(int line, int col)
    {
        std::string out;
        while (true)
        {
            if (atEnd())
                throw CompileError("unterminated asm block", line, col);
            int cl = line_, cc = col_;
            char c = advance();
            if (c == '}')
                break;
            if (c == ';')
            {
                out += c;
                while (!atEnd() && peek() != '\n')
                    out += advance();
                continue;
            }
            if (c == '{')
            {
                size_t p = pos_;
                if (p < src_.size() && (std::isalpha(static_cast<unsigned char>(src_[p])) || src_[p] == '_'))
                {
                    while (p < src_.size() && (std::isalnum(static_cast<unsigned char>(src_[p])) || src_[p] == '_'))
                        ++p;
                    if (p < src_.size() && src_[p] == '}')
                    {
                        out += c;
                        while (pos_ <= p)
                            out += advance();
                        continue;
                    }
                }
                throw CompileError("invalid placeholder in asm block: expected {name}", cl, cc);
            }
            out += c;
        }
        return make(Tok::KwAsm, std::move(out), line, col);
    }

    std::vector<std::string> asmPlaceholders(const std::string &text)
    {
        std::vector<std::string> names;
        size_t i = 0;
        while (i < text.size())
        {
            if (text[i] == ';')
            {
                while (i < text.size() && text[i] != '\n')
                    ++i;
                continue;
            }
            if (text[i] == '{')
            {
                size_t e = text.find('}', i);
                std::string name = text.substr(i + 1, e - i - 1);
                bool seen = false;
                for (const std::string &n : names)
                    seen = seen || n == name;
                if (!seen)
                    names.push_back(name);
                i = e;
            }
            ++i;
        }
        return names;
    }

    Token Lexer::lexWord()
    {
        int line = line_, col = col_;
        std::string word;
        while (std::isalnum(static_cast<unsigned char>(peek())) || peek() == '_')
            word += advance();

        if (word == "var" && peek() == '*')
        {
            advance();
            return make(Tok::KwVar, "var*", line, col);
        }
        if (word == "var" && peek() == '&')
        {
            advance();
            return make(Tok::KwVarManual, "var&", line, col);
        }
        if (word == "package")
            return make(Tok::KwPackage, word, line, col);
        if (word == "fn")
            return make(Tok::KwFn, word, line, col);
        if (word == "return")
            return make(Tok::KwReturn, word, line, col);
        if (word == "if")
            return make(Tok::KwIf, word, line, col);
        if (word == "else")
            return make(Tok::KwElse, word, line, col);
        if (word == "while")
            return make(Tok::KwWhile, word, line, col);
        if (word == "for")
            return make(Tok::KwFor, word, line, col);
        if (word == "in")
            return make(Tok::KwIn, word, line, col);
        if (word == "break")
            return make(Tok::KwBreak, word, line, col);
        if (word == "continue")
            return make(Tok::KwContinue, word, line, col);
        if (word == "true")
            return make(Tok::KwTrue, word, line, col);
        if (word == "false")
            return make(Tok::KwFalse, word, line, col);
        if (word == "asm")
        {
            size_t p = pos_;
            while (p < src_.size() && (src_[p] == ' ' || src_[p] == '\t'))
                ++p;
            if (p < src_.size() && src_[p] == '{')
            {
                while (pos_ <= p)
                    advance();
                return lexAsmBody(line, col);
            }
        }
        static const std::pair<const char *, Tok> more[] = {
            {"struct", Tok::KwStruct}, {"interface", Tok::KwInterface}, {"const", Tok::KwConst},
            {"switch", Tok::KwSwitch}, {"case", Tok::KwCase},           {"default", Tok::KwDefault},
            {"nil", Tok::KwNil},       {"extern", Tok::KwExtern}, {"map", Tok::KwMap},
        };
        for (const auto &[text, kind] : more)
            if (word == text)
                return make(kind, word, line, col);
        return make(Tok::Ident, std::move(word), line, col);
    }

    std::vector<Token> Lexer::tokenize()
    {
        std::vector<Token> out;
        while (true)
        {
            skipTrivia();
            if (atEnd())
                break;

            char c = peek();
            int line = line_, col = col_;

            if (std::isdigit(static_cast<unsigned char>(c)))
            {
                out.push_back(lexNumber());
            }
            else if (std::isalpha(static_cast<unsigned char>(c)) || c == '_')
            {
                out.push_back(lexWord());
            }
            else if (c == '\'')
            {
                out.push_back(lexChar());
            }
            else if (c == '"')
            {
                out.push_back(lexString());
            }
            else if (c == '#')
            {
                advance();
                std::string word;
                while (std::isalpha(static_cast<unsigned char>(peek())))
                    word += advance();
                if (word != "use")
                    throw CompileError("unknown directive '#" + word + "'", line, col);
                out.push_back(make(Tok::KwUse, "#use", line, col));
            }
            else
            {
                size_t start = pos_;
                advance();
                auto match = [&](char next)
                {
                    if (peek() != next)
                        return false;
                    advance();
                    return true;
                };
                Tok kind;
                switch (c)
                {
                case '(':
                    kind = Tok::LParen;
                    break;
                case ')':
                    kind = Tok::RParen;
                    break;
                case '{':
                    kind = Tok::LBrace;
                    break;
                case '}':
                    kind = Tok::RBrace;
                    break;
                case ',':
                    kind = Tok::Comma;
                    break;
                case '[':
                    kind = Tok::LBracket;
                    break;
                case ']':
                    kind = Tok::RBracket;
                    break;
                case ':':
                    kind = match('=') ? Tok::ColonAssign : Tok::Colon;
                    break;
                case '^':
                    kind = Tok::Caret;
                    break;
                case '~':
                    kind = Tok::Tilde;
                    break;
                case ';':
                    kind = Tok::Semi;
                    break;
                case '.':
                    if (match('.'))
                        kind = match('.') ? Tok::Ellipsis : Tok::DotDot;
                    else
                        kind = Tok::Dot;
                    break;
                case '=':
                    kind = match('=') ? Tok::Eq : Tok::Assign;
                    break;
                case '!':
                    kind = match('=') ? Tok::Ne : Tok::Bang;
                    break;
                case '<':
                    kind = match('<') ? Tok::Shl : match('=') ? Tok::Le
                                                              : Tok::Lt;
                    break;
                case '>':
                    kind = match('>') ? Tok::Shr : match('=') ? Tok::Ge
                                                              : Tok::Gt;
                    break;
                case '&':
                    kind = match('&') ? Tok::AndAnd : Tok::Amp;
                    break;
                case '|':
                    kind = match('|') ? Tok::OrOr : Tok::Pipe;
                    break;
                case '+':
                    kind = match('+') ? Tok::PlusPlus : match('=') ? Tok::PlusAssign
                                                                   : Tok::Plus;
                    break;
                case '-':
                    kind = match('-') ? Tok::MinusMinus : match('=') ? Tok::MinusAssign
                                                                     : Tok::Minus;
                    break;
                case '*':
                    kind = match('=') ? Tok::StarAssign : Tok::Star;
                    break;
                case '/':
                    kind = match('=') ? Tok::SlashAssign : Tok::Slash;
                    break;
                case '%':
                    kind = match('=') ? Tok::PercentAssign : Tok::Percent;
                    break;
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

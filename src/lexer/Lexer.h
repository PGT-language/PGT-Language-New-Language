#pragma once
#include "lexer/Token.h"

#include <functional>
#include <string>
#include <vector>

namespace pgt
{

    std::vector<std::string> asmPlaceholders(const std::string &text);
    std::vector<std::string> cPlaceholders(const std::string &text);
    void scanC(const std::string &text, const std::function<void(size_t, size_t)> &onBrace);

    class Lexer
    {
    public:
        explicit Lexer(std::string source) : src_(std::move(source))
        {
        }
        std::vector<Token> tokenize();

    private:
        char peek(size_t ahead = 0) const;
        char advance();
        bool atEnd() const
        {
            return pos_ >= src_.size();
        }
        void skipTrivia();
        Token make(Tok kind, std::string text, int line, int col) const;
        Token lexNumber();
        Token lexString();
        Token lexRawString();
        Token lexChar();
        Token lexWord();
        int hexEscape(int line, int col);
        Token lexAsmBody(int line, int col);
        Token lexCBody(int line, int col);
        Tok prevKind_ = Tok::Eof;
        int prevEnd_ = 0;
        bool hasPrev_ = false;

        std::string src_;
        size_t pos_ = 0;
        int line_ = 1;
        int col_ = 1;
    };

}

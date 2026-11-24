#pragma once
#include "lexer/Token.h"

#include <string>
#include <vector>

namespace pgt
{

    std::vector<std::string> asmPlaceholders(const std::string &text);

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
        Token lexChar();
        Token lexWord();
        Token lexAsmBody(int line, int col);

        std::string src_;
        size_t pos_ = 0;
        int line_ = 1;
        int col_ = 1;
    };

}

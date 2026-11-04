#pragma once
#include "lexer/Token.h"

#include <string>
#include <vector>

namespace pgt {

class Lexer {
public:
    explicit Lexer(std::string source) : src_(std::move(source)) {}
    std::vector<Token> tokenize();

private:
    char peek(size_t ahead = 0) const;
    char advance();
    bool atEnd() const { return pos_ >= src_.size(); }
    void skipTrivia();
    Token make(Tok kind, std::string text, int line, int col) const;
    Token lexNumber();
    Token lexString();
    Token lexWord();

    std::string src_;
    size_t pos_ = 0;
    int line_ = 1;
    int col_ = 1;
};

} // namespace pgt

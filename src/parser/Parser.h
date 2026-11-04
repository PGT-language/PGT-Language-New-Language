#pragma once
#include "ast/AST.h"
#include "lexer/Token.h"

#include <vector>

namespace pgt {

class Parser {
public:
    explicit Parser(std::vector<Token> tokens) : toks_(std::move(tokens)) {}
    Program parseProgram();

private:
    const Token& cur() const { return toks_[pos_]; }
    const Token& peekTok(size_t ahead = 1) const;
    bool check(Tok k) const { return cur().kind == k; }
    const Token& advance();
    const Token& expect(Tok k, const char* context);
    [[noreturn]] void errorHere(const std::string& msg) const;

    Function parseFunction();
    StmtPtr parseStatement();
    ExprPtr parseExpr();
    ExprPtr parseTerm();
    ExprPtr parseFactor();
    ExprPtr parsePrimary();

    std::vector<Token> toks_;
    size_t pos_ = 0;
};

} // namespace pgt

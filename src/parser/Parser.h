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

    void parseImports(Program& prog);
    Function parseFunction();
    Block parseBlock(const char* context);
    StmtPtr parseStatement();
    StmtPtr parseVarDecl();
    StmtPtr parseSimpleStatement();
    bool atAssignment() const;
    StmtPtr parseIf();
    StmtPtr parseWhile();
    StmtPtr parseFor();

    ExprPtr parseExpr();
    ExprPtr parseOr();
    ExprPtr parseAnd();
    ExprPtr parseEquality();
    ExprPtr parseRelational();
    ExprPtr parseAdditive();
    ExprPtr parseTerm();
    ExprPtr parseUnary();
    ExprPtr parsePrimary();

    template <typename T>
    std::unique_ptr<T> make(const Token& at) const {
        auto node = std::make_unique<T>();
        node->line = at.line;
        node->col = at.col;
        return node;
    }

    std::vector<Token> toks_;
    size_t pos_ = 0;
};

}

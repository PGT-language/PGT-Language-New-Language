#pragma once
#include "ast/AST.h"
#include "lexer/Token.h"

#include <vector>

namespace pgt
{

    class Parser
    {
    public:
        explicit Parser(std::vector<Token> tokens) : toks_(std::move(tokens))
        {
        }
        Program parseProgram();

    private:
        const Token &cur() const
        {
            return toks_[pos_];
        }
        const Token &peekTok(size_t ahead = 1) const;
        bool check(Tok k) const
        {
            return cur().kind == k;
        }
        bool accept(Tok k)
        {
            if (!check(k))
                return false;
            advance();
            return true;
        }
        const Token &advance();
        // True when the current token starts a new line: it cannot continue the expression.
        bool atLineStart() const
        {
            return pos_ > 0 && toks_[pos_ - 1].line < cur().line;
        }
        const Token &expect(Tok k, const char *context);
        [[noreturn]] void errorHere(const std::string &msg) const;

        void parseImports(Program &prog);
        bool atTypeStart() const;
        TypeExpr parseType(const char *context);
        std::vector<Param> parseParams();
        Function parseFunction();
        Function parseExtern();
        StructDecl parseStruct();
        InterfaceDecl parseInterface();
        void parseConsts(std::vector<std::unique_ptr<VarDecl>> &out);
        std::unique_ptr<VarDecl> parseConstSpec();
        Block parseBlock(const char *context);
        StmtPtr parseStatement();
        std::unique_ptr<VarDecl> parseVarDecl();
        std::unique_ptr<VarDecl> parseManualVarDecl();
        ExprPtr parseMalloc();
        StmtPtr parseSimpleStatement();
        StmtPtr parseIf();
        StmtPtr parseWhile();
        StmtPtr parseFor();
        StmtPtr parseSwitch();
        ExprPtr parseHeaderExpr();

        ExprPtr parseExpr();
        ExprPtr parseBinary(int level);
        ExprPtr parseUnary();
        ExprPtr parsePostfix();
        ExprPtr parsePrimary();
        ExprPtr parseStructLit(TypeExpr type, const Token &at);
        bool atStructLitBody() const;

        template <typename T>
        std::unique_ptr<T> make(const Token &at) const
        {
            auto node = std::make_unique<T>();
            node->line = at.line;
            node->col = at.col;
            return node;
        }

        std::vector<Token> toks_;
        size_t pos_ = 0;
        // Inside `if`/`for`/`switch` headers `Name {` starts the body, not a struct literal.
        bool noStructLit_ = false;
        // `malloc(...)` is only legal as the initializer of `var&`.
        bool allowMalloc_ = false;
    };

}

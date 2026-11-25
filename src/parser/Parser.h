#pragma once
#include "ast/AST.h"
#include "lexer/Token.h"

#include <algorithm>
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
        bool atLineStart() const
        {
            if (pos_ == 0)
                return false;
            const Token &prev = toks_[pos_ - 1];
            return std::max(prev.line, prev.endLine) < cur().line;
        }
        const Token &expect(Tok k, const char *context);
        [[noreturn]] void errorHere(const std::string &msg) const;

        void parseImports(Program &prog);
        bool atTypeStart() const;
        TypeExpr parseType(const char *context);
        std::vector<Param> parseParams(bool allowVariadic);
        Function parseFunction();
        Function parseExtern();
        StructDecl parseStruct();
        InterfaceDecl parseInterface();
        struct ConstTemplate
        {
            bool valid = false;
            bool hasType = false;
            size_t typePos = 0;
            size_t exprPos = 0;
        };
        TypeExpr parseResult();
        StmtPtr parseVarStmt();
        void parseConsts(std::vector<std::unique_ptr<VarDecl>> &out);
        std::unique_ptr<VarDecl> parseConstSpec(int iota, ConstTemplate &tmpl);
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
        ExprPtr parseTernary();
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
        bool noStructLit_ = false;
        bool allowMalloc_ = false;
        long long iota_ = -1;
    };

}

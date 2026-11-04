#pragma once
#include <memory>
#include <string>
#include <vector>

namespace pgt {

struct Expr {
    int line = 0, col = 0;
    virtual ~Expr() = default;
};
using ExprPtr = std::unique_ptr<Expr>;

struct IntLit : Expr {
    long long value = 0;
};

struct StringLit : Expr {
    std::string value;
};

struct VarRef : Expr {
    std::string name;
};

struct Unary : Expr {
    ExprPtr operand;
};

struct Binary : Expr {
    char op = '+';
    ExprPtr lhs, rhs;
};

struct Call : Expr {
    std::string callee;
    std::vector<ExprPtr> args;
};

struct Stmt {
    int line = 0, col = 0;
    virtual ~Stmt() = default;
};
using StmtPtr = std::unique_ptr<Stmt>;

struct VarDecl : Stmt {
    std::string name;
    ExprPtr init;
};

struct ExprStmt : Stmt {
    ExprPtr expr;
};

struct Function {
    std::string name;
    std::vector<StmtPtr> body;
    int line = 0, col = 0;
};

struct Program {
    std::string file;
    std::string package;
    std::vector<Function> functions;
    ExprPtr exitCode;
};

}

#pragma once
#include <memory>
#include <string>
#include <vector>

namespace pgt
{

    enum class BinOp
    {
        Add,
        Sub,
        Mul,
        Div,
        Mod,
        Eq,
        Ne,
        Lt,
        Gt,
        Le,
        Ge,
        And,
        Or
    };
    enum class UnOp
    {
        Neg,
        Not
    };
    enum class AssignOp
    {
        Set,
        Add,
        Sub,
        Mul,
        Div,
        Mod
    };

    inline const char *opText(BinOp op)
    {
        switch (op)
        {
        case BinOp::Add:
            return "+";
        case BinOp::Sub:
            return "-";
        case BinOp::Mul:
            return "*";
        case BinOp::Div:
            return "/";
        case BinOp::Mod:
            return "%";
        case BinOp::Eq:
            return "==";
        case BinOp::Ne:
            return "!=";
        case BinOp::Lt:
            return "<";
        case BinOp::Gt:
            return ">";
        case BinOp::Le:
            return "<=";
        case BinOp::Ge:
            return ">=";
        case BinOp::And:
            return "&&";
        case BinOp::Or:
            return "||";
        }
        return "?";
    }

    inline const char *opText(UnOp op) { return op == UnOp::Neg ? "-" : "!"; }

    struct Expr
    {
        int line = 0, col = 0;
        virtual ~Expr() = default;
    };
    using ExprPtr = std::unique_ptr<Expr>;

    struct IntLit : Expr
    {
        long long value = 0;
    };

    struct BoolLit : Expr
    {
        bool value = false;
    };

    struct StringLit : Expr
    {
        std::string value;
    };

    struct VarRef : Expr
    {
        std::string name;
    };

    struct Unary : Expr
    {
        UnOp op = UnOp::Neg;
        ExprPtr operand;
    };

    struct Binary : Expr
    {
        BinOp op = BinOp::Add;
        ExprPtr lhs, rhs;
    };

    struct Call : Expr
    {
        std::string qualifier;
        std::string callee;
        std::vector<ExprPtr> args;
    };

    struct Stmt
    {
        int line = 0, col = 0;
        virtual ~Stmt() = default;
    };
    using StmtPtr = std::unique_ptr<Stmt>;
    using Block = std::vector<StmtPtr>;

    struct VarDecl : Stmt
    {
        std::string name;
        ExprPtr init;
    };

    struct Assign : Stmt
    {
        std::string name;
        AssignOp op = AssignOp::Set;
        ExprPtr value;
    };

    struct ExprStmt : Stmt
    {
        ExprPtr expr;
    };

    struct If : Stmt
    {
        ExprPtr cond;
        Block thenBody;
        Block elseBody;
        bool hasElse = false;
    };

    struct Loop : Stmt
    {
        StmtPtr init;
        ExprPtr cond;
        StmtPtr post;
        Block body;
    };

    struct RangeLoop : Stmt
    {
        std::string var;
        ExprPtr from, to;
        Block body;
    };

    struct Break : Stmt
    {
    };
    struct Continue : Stmt
    {
    };

    struct Function
    {
        std::string name;
        Block body;
        int line = 0, col = 0;
    };

    struct Import
    {
        std::string path;
        int line = 0, col = 0;
    };

    struct Program
    {
        std::string file;
        std::string package;
        std::vector<Import> imports;
        std::vector<Function> functions;
        ExprPtr exitCode;
    };

}

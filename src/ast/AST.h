#pragma once
#include <memory>
#include <optional>
#include <set>
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
        Or,
        BitAnd,
        BitOr,
        BitXor,
        Shl,
        Shr
    };
    enum class UnOp
    {
        Neg,
        Not,
        BitNot,
        Deref,
        AddrOf
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
        case BinOp::BitAnd:
            return "&";
        case BinOp::BitOr:
            return "|";
        case BinOp::BitXor:
            return "^";
        case BinOp::Shl:
            return "<<";
        case BinOp::Shr:
            return ">>";
        }
        return "?";
    }

    inline const char *opText(UnOp op)
    {
        switch (op)
        {
        case UnOp::Neg:
            return "-";
        case UnOp::Not:
            return "!";
        case UnOp::BitNot:
            return "~";
        case UnOp::Deref:
            return "*";
        case UnOp::AddrOf:
            return "&";
        }
        return "?";
    }

    struct TypeExpr
    {
        enum class Kind
        {
            Named,
            Array,
            Pointer,
            Map,
            Tuple,
            Func
        };
        Kind kind = Kind::Named;
        std::string qualifier;
        std::string name;
        std::shared_ptr<TypeExpr> elem;
        std::shared_ptr<TypeExpr> key;
        std::vector<TypeExpr> elems;
        int line = 0, col = 0;

        static TypeExpr named(std::string name, int line, int col)
        {
            TypeExpr t;
            t.name = std::move(name);
            t.line = line;
            t.col = col;
            return t;
        }
        std::string text() const
        {
            switch (kind)
            {
            case Kind::Array:
                return "[]" + elem->text();
            case Kind::Pointer:
                return "*" + elem->text();
            case Kind::Map:
                return "map[" + key->text() + "]" + elem->text();
            case Kind::Tuple:
            {
                std::string out = "(";
                for (size_t i = 0; i < elems.size(); ++i)
                    out += (i ? ", " : "") + elems[i].text();
                return out + ")";
            }
            case Kind::Func:
            {
                std::string out = "fn(";
                for (size_t i = 0; i < elems.size(); ++i)
                    out += (i ? ", " : "") + elems[i].text();
                out += ")";
                if (elem)
                    out += " " + elem->text();
                return out;
            }
            case Kind::Named:
                break;
            }
            return qualifier.empty() ? name : qualifier + "." + name;
        }
    };

    struct Expr
    {
        int line = 0, col = 0;
        virtual ~Expr() = default;
    };
    using ExprPtr = std::unique_ptr<Expr>;

    struct IntLit : Expr
    {
        unsigned long long value = 0;
    };

    struct FloatLit : Expr
    {
        double value = 0;
    };

    struct CharLit : Expr
    {
        char value = 0;
    };

    struct BoolLit : Expr
    {
        bool value = false;
    };

    struct StringLit : Expr
    {
        std::string value;
    };

    struct NilLit : Expr
    {
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
        ExprPtr callee;
        std::optional<TypeExpr> typeArg;
        std::vector<ExprPtr> args;
        bool spread = false;
    };

    struct Selector : Expr
    {
        ExprPtr base;
        std::string name;
    };

    struct Index : Expr
    {
        ExprPtr base;
        ExprPtr index;
    };

    struct SliceExpr : Expr
    {
        ExprPtr base;
        ExprPtr lo;
        ExprPtr hi;
    };

    struct ArrayLit : Expr
    {
        std::vector<ExprPtr> elems;
    };

    struct FieldInit
    {
        std::string name;
        ExprPtr value;
        int line = 0, col = 0;
    };

    struct StructLit : Expr
    {
        TypeExpr type;
        std::vector<FieldInit> fields;
    };

    struct MapLit : Expr
    {
        TypeExpr type;
        std::vector<ExprPtr> keys;
        std::vector<ExprPtr> values;
    };

    struct Ternary : Expr
    {
        ExprPtr cond, thenExpr, elseExpr;
    };

    struct MallocExpr : Expr
    {
        TypeExpr elem;
        ExprPtr count;
    };

    struct TypeAssert : Expr
    {
        ExprPtr base;
        TypeExpr type;
        bool isTypeKeyword = false;
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
        std::optional<TypeExpr> type;
        ExprPtr init;
        bool isConst = false;
        bool isManual = false;
    };

    struct MultiVarDecl : Stmt
    {
        std::vector<std::string> names;
        std::vector<int> cols;
        std::vector<ExprPtr> values;
    };

    struct MultiAssign : Stmt
    {
        std::vector<ExprPtr> targets;
        std::vector<ExprPtr> values;
    };

    struct Assign : Stmt
    {
        ExprPtr target;
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

    struct ForIn : Stmt
    {
        std::string key;
        std::string value;
        ExprPtr iterable;
        ExprPtr rangeEnd;
        Block body;
    };

    struct Case
    {
        std::vector<ExprPtr> values;
        std::vector<TypeExpr> types;
        Block body;
        int line = 0, col = 0;
    };

    struct Switch : Stmt
    {
        ExprPtr tag;
        std::vector<Case> cases;
        Block defaultBody;
        bool hasDefault = false;
        bool typeSwitch = false;
        std::string bind;
    };

    struct Break : Stmt
    {
    };
    struct Continue : Stmt
    {
    };

    struct AsmStmt : Stmt
    {
        std::string text;
        std::vector<std::string> names;
    };

    struct CStmt : Stmt
    {
        std::string text;
        std::vector<std::string> names;
    };

    struct Defer : Stmt
    {
        ExprPtr call;
    };

    struct Return : Stmt
    {
        ExprPtr value;
        std::vector<ExprPtr> rest;
    };

    struct Param
    {
        std::string name;
        TypeExpr type;
        bool variadic = false;
        int line = 0, col = 0;
    };

    struct FuncLit : Expr
    {
        std::vector<Param> params;
        std::optional<TypeExpr> ret;
        Block body;
    };

    struct Receiver
    {
        std::string name;
        std::string typeName;
        bool pointer = false;
        int line = 0, col = 0;
    };

    struct Function
    {
        std::string name;
        std::optional<Receiver> receiver;
        std::vector<Param> params;
        std::optional<TypeExpr> ret;
        Block body;
        bool isExtern = false;
        int line = 0, col = 0;
    };

    struct Field
    {
        std::string name;
        TypeExpr type;
        int line = 0, col = 0;
    };

    struct StructDecl
    {
        std::string name;
        std::vector<Field> fields;
        int line = 0, col = 0;
    };

    struct MethodSig
    {
        std::string name;
        std::vector<Param> params;
        std::optional<TypeExpr> ret;
        int line = 0, col = 0;
    };

    struct InterfaceDecl
    {
        std::string name;
        std::vector<MethodSig> methods;
        int line = 0, col = 0;
    };

    struct Import
    {
        std::string path;
        int line = 0, col = 0;
    };

    struct TopBlock
    {
        bool isC = false;
        std::string text;
        int line = 0, col = 0;
    };

    struct Program
    {
        std::vector<TopBlock> blocks;
        std::string file;
        std::string package;
        std::vector<Import> imports;
        std::vector<Function> functions;
        std::vector<StructDecl> structs;
        std::vector<InterfaceDecl> interfaces;
        std::vector<std::unique_ptr<VarDecl>> globals;
        ExprPtr exitCode;
        std::set<std::string> qualifiers;
        bool usesMalloc = false;
        bool usesMaps = false;
        bool usesAsm = false;
        bool usesC = false;
    };

}

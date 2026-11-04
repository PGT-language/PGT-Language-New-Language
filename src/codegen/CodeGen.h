#pragma once
#include "loader/Package.h"

#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Module.h>

#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace pgt
{

    class CodeGen
    {
    public:
        explicit CodeGen(const std::string &moduleName);

        void generate(const Workspace &workspace);
        llvm::Module &module()
        {
            return *module_;
        }

    private:
        enum class Ty
        {
            Void,
            Int,
            Int64,
            Float,
            Char,
            String,
            Bool
        };
        struct Value
        {
            llvm::Value *value;
            Ty type;
        };
        struct Var
        {
            llvm::AllocaInst *slot;
            Ty type;
        };
        struct LoopTargets
        {
            llvm::BasicBlock *continueTo;
            llvm::BasicBlock *breakTo;
        };
        struct FuncInfo
        {
            llvm::Function *fn;
            std::vector<Ty> params;
            Ty ret;
        };

        llvm::Type *llvmType(Ty t);
        static const char *tyName(Ty t);
        static bool isIntegral(Ty t);
        static bool isNumeric(Ty t);
        static std::optional<Ty> builtinType(const std::string &name);
        Ty resolveType(const TypeExpr &type) const;
        llvm::Value *zeroValue(Ty t);
        static bool isPublic(const std::string &name);
        static std::string mangle(const std::string &importPath, const std::string &name);

        llvm::AllocaInst *entryAlloca(llvm::Type *type, const std::string &name);
        void pushScope();
        void popScope();
        Var *lookup(const std::string &name);
        void declare(const std::string &name, Var var, int line, int col);
        void startDeadBlock();

        void checkExitCodes(const Package &pkg) const;
        void declareFunctions(const Package &pkg);
        void bindImports(const Program &program);
        void genFunction(const Function &fn);
        void genEntryPoint(const Package &root, const Expr &exitCode);

        void genBlock(const Block &block);
        void genStmt(const Stmt &stmt);
        void genVarDecl(const VarDecl &decl);
        void genAssign(const Assign &assign);
        void genReturn(const Return &ret);
        void genIf(const If &node);
        void genLoop(const Loop &loop);
        void genRangeLoop(const RangeLoop &loop);
        void genCallStmt(const Call &call);
        void genPrint(const Call &call, bool newline);
        void genInput(const Call &call, bool wholeLine);

        Value genExpr(const Expr &expr, Ty hint = Ty::Void);
        Value genCond(const Expr &expr);
        Value genCall(const Call &call);
        Value genConversion(const Call &call, Ty target);
        Value convert(Value v, Ty to, int line, int col);
        Value genBinary(const Binary &bin, Ty hint);
        Value genLogical(const Binary &bin);
        Value genCompare(const Binary &bin);
        Value genArith(BinOp op, Value l, Value r, int line, int col);
        void genOperands(const Expr &lhs, const Expr &rhs, Ty hint, Value &l, Value &r);

        llvm::LLVMContext ctx_;
        std::unique_ptr<llvm::Module> module_;
        llvm::IRBuilder<> builder_;

        const Workspace *ws_ = nullptr;
        const Package *pkg_ = nullptr;
        std::string currentFile_;
        Ty currentRet_ = Ty::Void;
        std::map<std::string, std::map<std::string, FuncInfo>> functions_;
        std::map<std::string, const Package *> fileImports_;
        std::vector<std::map<std::string, Var>> scopes_;
        std::vector<LoopTargets> loops_;
    };

} // namespace pgt

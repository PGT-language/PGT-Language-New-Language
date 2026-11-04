#pragma once
#include "loader/Package.h"

#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Module.h>

#include <map>
#include <memory>
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
            Int,
            Bool,
            String
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

        llvm::Type *llvmType(Ty t);
        static const char *tyName(Ty t);
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
        void genAssign(const Assign &assign);
        void genIf(const If &node);
        void genLoop(const Loop &loop);
        void genRangeLoop(const RangeLoop &loop);
        void genCallStmt(const Call &call);
        void genPrint(const Call &call, bool newline);
        void genInput(const Call &call, bool wholeLine);

        Value genExpr(const Expr &expr);
        Value genCond(const Expr &expr);
        Value genBinary(const Binary &bin);
        Value genLogical(const Binary &bin);
        Value genCompare(const Binary &bin);

        llvm::LLVMContext ctx_;
        std::unique_ptr<llvm::Module> module_;
        llvm::IRBuilder<> builder_;

        const Workspace *ws_ = nullptr;
        const Package *pkg_ = nullptr;
        std::string currentFile_;
        std::map<std::string, std::map<std::string, llvm::Function *>> functions_;
        std::map<std::string, const Package *> fileImports_;
        std::vector<std::map<std::string, Var>> scopes_;
        std::vector<LoopTargets> loops_;
    };

} // namespace pgt

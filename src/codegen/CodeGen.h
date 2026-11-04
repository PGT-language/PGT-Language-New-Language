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
        llvm::Module &module() { return *module_; }

    private:
        enum class Ty
        {
            Int,
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

        llvm::Type *llvmType(Ty t);
        static bool isPublic(const std::string &name);
        static std::string mangle(const std::string &importPath, const std::string &name);

        void checkExitCodes(const Package &pkg) const;
        void declareFunctions(const Package &pkg);
        void bindImports(const Program &program);
        void genFunction(const Function &fn);
        void genEntryPoint(const Package &root, const Expr &exitCode);
        void genStmt(const Stmt &stmt);
        void genCallStmt(const Call &call);
        void genPrint(const Call &call, bool newline);
        Value genExpr(const Expr &expr);
        Value genBinary(const Binary &bin);

        llvm::LLVMContext ctx_;
        std::unique_ptr<llvm::Module> module_;
        llvm::IRBuilder<> builder_;

        const Workspace *ws_ = nullptr;
        const Package *pkg_ = nullptr;
        std::string currentFile_;
        std::map<std::string, std::map<std::string, llvm::Function *>> functions_;
        std::map<std::string, const Package *> fileImports_;
        std::map<std::string, Var> locals_;
    };

}

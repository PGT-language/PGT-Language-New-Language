#pragma once
#include "ast/AST.h"

#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Module.h>

#include <map>
#include <memory>
#include <string>

namespace pgt {

// Translates a Program (AST) into an LLVM module.
class CodeGen {
public:
    explicit CodeGen(const std::string& moduleName);

    void generate(const Program& program);
    llvm::Module& module() { return *module_; }

private:
    enum class Ty { Int, String };
    struct Value {
        llvm::Value* value;
        Ty type;
    };
    struct Var {
        llvm::AllocaInst* slot;
        Ty type;
    };

    llvm::Type* llvmType(Ty t);

    void declareFunctions(const Program& program);
    void genFunction(const Function& fn);
    void genEntryPoint(const Program& program);
    void genStmt(const Stmt& stmt);
    void genCallStmt(const Call& call);
    void genPrint(const Call& call, bool newline);
    Value genExpr(const Expr& expr);
    Value genBinary(const Binary& bin);

    static std::string mangle(const std::string& name) { return "pgt." + name; }

    llvm::LLVMContext ctx_;
    std::unique_ptr<llvm::Module> module_;
    llvm::IRBuilder<> builder_;
    std::map<std::string, llvm::Function*> functions_;
    std::map<std::string, Var> locals_; // variables of the function being generated
};

} // namespace pgt

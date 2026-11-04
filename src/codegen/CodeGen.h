#pragma once
#include "ast/AST.h"

#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Module.h>

#include <map>
#include <vector>
#include <memory>
#include <string>

namespace pgt {

class CodeGen {
public:
    explicit CodeGen(const std::string& moduleName);

    void generate(const std::vector<Program>& programs);
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

    void declareFunctions(const std::vector<Program>& programs);
    void genFunction(const Function& fn);
    void genEntryPoint(const Expr& exitCode);
    void checkExitCodes(const std::vector<Program>& programs) const;
    static bool isPublic(const std::string& name);
    void genStmt(const Stmt& stmt);
    void genCallStmt(const Call& call);
    void genPrint(const Call& call, bool newline);
    Value genExpr(const Expr& expr);
    Value genBinary(const Binary& bin);

    std::string mangle(const std::string& name) const { return "pgt." + package_ + "." + name; }

    llvm::LLVMContext ctx_;
    std::unique_ptr<llvm::Module> module_;
    llvm::IRBuilder<> builder_;
    std::string package_;
    std::string currentFile_;
    std::map<std::string, llvm::Function*> functions_;
    std::map<std::string, Var> locals_;
};

}

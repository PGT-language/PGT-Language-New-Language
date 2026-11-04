#include "codegen/CodeGen.h"

#include "common/Error.h"

#include <llvm/IR/Verifier.h>
#include <llvm/Support/raw_ostream.h>

#include <climits>

namespace pgt {

CodeGen::CodeGen(const std::string& moduleName)
    : module_(std::make_unique<llvm::Module>(moduleName, ctx_)), builder_(ctx_) {}

llvm::Type* CodeGen::llvmType(Ty t) {
    switch (t) {
    case Ty::Int: return builder_.getInt32Ty();
    case Ty::String: return builder_.getPtrTy();
    }
    return nullptr;
}

void CodeGen::generate(const Program& program) {
    declareFunctions(program);
    for (const Function& fn : program.functions) genFunction(fn);
    genEntryPoint(program);

    std::string err;
    llvm::raw_string_ostream os(err);
    if (llvm::verifyModule(*module_, &os))
        throw CompileError("internal error: generated invalid LLVM IR:\n" + err, 0, 0);
}

// Declare every function up front so calls do not depend on definition order.
void CodeGen::declareFunctions(const Program& program) {
    auto* fnTy = llvm::FunctionType::get(builder_.getVoidTy(), false);
    for (const Function& fn : program.functions) {
        if (functions_.count(fn.name))
            throw CompileError("function '" + fn.name + "' is already defined", fn.line, fn.col);
        functions_[fn.name] = llvm::Function::Create(fnTy, llvm::Function::InternalLinkage,
                                                     mangle(fn.name), module_.get());
    }
}

void CodeGen::genFunction(const Function& fn) {
    llvm::Function* f = functions_.at(fn.name);
    builder_.SetInsertPoint(llvm::BasicBlock::Create(ctx_, "entry", f));
    locals_.clear();

    for (const StmtPtr& stmt : fn.body) genStmt(*stmt);
    builder_.CreateRetVoid();
}

// The real `main` calls the user's `main` and returns the file-level `return <expr>`.
void CodeGen::genEntryPoint(const Program& program) {
    auto it = functions_.find("main");
    if (it == functions_.end())
        throw CompileError("function 'main' is not defined", 1, 1);

    auto* mainTy = llvm::FunctionType::get(builder_.getInt32Ty(), false);
    auto* mainFn =
        llvm::Function::Create(mainTy, llvm::Function::ExternalLinkage, "main", module_.get());
    builder_.SetInsertPoint(llvm::BasicBlock::Create(ctx_, "entry", mainFn));
    locals_.clear();

    builder_.CreateCall(it->second);

    Value code = genExpr(*program.exitCode);
    if (code.type != Ty::Int)
        throw CompileError("the final 'return' must be an integer", program.exitCode->line,
                           program.exitCode->col);
    builder_.CreateRet(code.value);
}

void CodeGen::genStmt(const Stmt& stmt) {
    if (auto* decl = dynamic_cast<const VarDecl*>(&stmt)) {
        if (locals_.count(decl->name))
            throw CompileError("variable '" + decl->name + "' is already declared", decl->line,
                               decl->col);
        Value init = genExpr(*decl->init);
        auto* slot = builder_.CreateAlloca(llvmType(init.type), nullptr, decl->name);
        builder_.CreateStore(init.value, slot);
        locals_[decl->name] = Var{slot, init.type};
        return;
    }
    if (auto* es = dynamic_cast<const ExprStmt*>(&stmt)) {
        genCallStmt(*static_cast<const Call*>(es->expr.get()));
        return;
    }
    throw CompileError("internal error: unknown statement", stmt.line, stmt.col);
}

void CodeGen::genCallStmt(const Call& call) {
    if (call.callee == "print") return genPrint(call, false);
    if (call.callee == "println") return genPrint(call, true);

    auto it = functions_.find(call.callee);
    if (it == functions_.end())
        throw CompileError("call to undefined function '" + call.callee + "'", call.line, call.col);
    if (!call.args.empty())
        throw CompileError("function '" + call.callee + "' takes no arguments", call.line, call.col);
    builder_.CreateCall(it->second);
}

// print/println: arguments are written one after another, without separators.
void CodeGen::genPrint(const Call& call, bool newline) {
    std::string fmt;
    std::vector<llvm::Value*> args{nullptr}; // slot 0 is the format string
    for (const ExprPtr& a : call.args) {
        Value v = genExpr(*a);
        fmt += v.type == Ty::Int ? "%d" : "%s";
        args.push_back(v.value);
    }
    if (newline) fmt += "\n";
    args[0] = builder_.CreateGlobalString(fmt, ".fmt");

    auto* printfTy = llvm::FunctionType::get(builder_.getInt32Ty(), {builder_.getPtrTy()}, true);
    llvm::FunctionCallee printfFn = module_->getOrInsertFunction("printf", printfTy);
    builder_.CreateCall(printfFn, args);
}

CodeGen::Value CodeGen::genExpr(const Expr& expr) {
    if (auto* lit = dynamic_cast<const IntLit*>(&expr)) {
        if (lit->value > INT_MAX)
            throw CompileError("integer literal does not fit into 'int' (32 bits)", lit->line,
                               lit->col);
        return {builder_.getInt32(static_cast<uint32_t>(lit->value)), Ty::Int};
    }
    if (auto* lit = dynamic_cast<const StringLit*>(&expr))
        return {builder_.CreateGlobalString(lit->value, ".str"), Ty::String};

    if (auto* ref = dynamic_cast<const VarRef*>(&expr)) {
        auto it = locals_.find(ref->name);
        if (it == locals_.end())
            throw CompileError("undefined variable '" + ref->name + "'", ref->line, ref->col);
        return {builder_.CreateLoad(llvmType(it->second.type), it->second.slot, ref->name),
                it->second.type};
    }
    if (auto* un = dynamic_cast<const Unary*>(&expr)) {
        Value v = genExpr(*un->operand);
        if (v.type != Ty::Int)
            throw CompileError("unary '-' needs an integer operand", un->line, un->col);
        return {builder_.CreateNeg(v.value), Ty::Int};
    }
    if (auto* bin = dynamic_cast<const Binary*>(&expr)) return genBinary(*bin);

    if (auto* call = dynamic_cast<const Call*>(&expr))
        throw CompileError("'" + call->callee + "' does not return a value", call->line,
                           call->col);

    throw CompileError("internal error: unknown expression", expr.line, expr.col);
}

CodeGen::Value CodeGen::genBinary(const Binary& bin) {
    Value l = genExpr(*bin.lhs);
    Value r = genExpr(*bin.rhs);
    if (l.type != Ty::Int || r.type != Ty::Int)
        throw CompileError(std::string("operator '") + bin.op + "' needs integer operands",
                           bin.line, bin.col);
    switch (bin.op) {
    case '+': return {builder_.CreateAdd(l.value, r.value), Ty::Int};
    case '-': return {builder_.CreateSub(l.value, r.value), Ty::Int};
    case '*': return {builder_.CreateMul(l.value, r.value), Ty::Int};
    case '/': return {builder_.CreateSDiv(l.value, r.value), Ty::Int};
    }
    throw CompileError("internal error: unknown operator", bin.line, bin.col);
}

} // namespace pgt

#include "codegen/CodeGen.h"

#include "common/Error.h"

#include <llvm/IR/Verifier.h>
#include <llvm/Support/raw_ostream.h>

#include <climits>

namespace pgt
{

    CodeGen::CodeGen(const std::string &moduleName)
        : module_(std::make_unique<llvm::Module>(moduleName, ctx_)), builder_(ctx_) {}

    llvm::Type *CodeGen::llvmType(Ty t)
    {
        switch (t)
        {
        case Ty::Int:
            return builder_.getInt32Ty();
        case Ty::String:
            return builder_.getPtrTy();
        }
        return nullptr;
    }

    bool CodeGen::isPublic(const std::string &name)
    {
        return !name.empty() && name[0] >= 'A' && name[0] <= 'Z';
    }

    std::string CodeGen::mangle(const std::string &importPath, const std::string &name)
    {
        return "pgt." + importPath + "." + name;
    }

    void CodeGen::checkExitCodes(const Package &pkg) const
    {
        const Program *withExit = nullptr;
        for (const Program &p : pkg.programs)
        {
            if (!p.exitCode)
                continue;
            if (pkg.name != "main")
            {
                CompileError err("only package 'main' may end with 'return'", p.exitCode->line,
                                 p.exitCode->col);
                err.file = p.file;
                throw err;
            }
            if (withExit)
            {
                CompileError err("package 'main' has more than one final 'return'", p.exitCode->line,
                                 p.exitCode->col);
                err.file = p.file;
                throw err;
            }
            withExit = &p;
        }
        if (pkg.name == "main" && !withExit)
            throw CompileError("package 'main' needs exactly one file ending with 'return'", 0, 0);
    }

    void CodeGen::generate(const Workspace &workspace)
    {
        ws_ = &workspace;
        try
        {
            for (const auto &pkg : workspace.packages)
            {
                checkExitCodes(*pkg);
                declareFunctions(*pkg);
            }
            for (const auto &pkg : workspace.packages)
            {
                pkg_ = pkg.get();
                for (const Program &p : pkg->programs)
                {
                    currentFile_ = p.file;
                    bindImports(p);
                    for (const Function &fn : p.functions)
                        genFunction(fn);
                }
            }
            const Package &root = *workspace.root;
            pkg_ = &root;
            fileImports_.clear();
            if (root.name == "main")
            {
                for (const Program &p : root.programs)
                {
                    if (!p.exitCode)
                        continue;
                    currentFile_ = p.file;
                    genEntryPoint(root, *p.exitCode);
                }
            }
        }
        catch (CompileError &e)
        {
            if (e.file.empty())
                e.file = currentFile_;
            throw;
        }

        std::string err;
        llvm::raw_string_ostream os(err);
        if (llvm::verifyModule(*module_, &os))
            throw CompileError("internal error: generated invalid LLVM IR:\n" + err, 0, 0);
    }

    void CodeGen::declareFunctions(const Package &pkg)
    {
        auto *fnTy = llvm::FunctionType::get(builder_.getVoidTy(), false);
        auto &table = functions_[pkg.importPath];
        for (const Program &p : pkg.programs)
        {
            currentFile_ = p.file;
            for (const Function &fn : p.functions)
            {
                if (table.count(fn.name))
                    throw CompileError("function '" + fn.name + "' is already defined in package '" +
                                           pkg.name + "'",
                                       fn.line, fn.col);
                auto linkage = isPublic(fn.name) ? llvm::Function::ExternalLinkage
                                                 : llvm::Function::InternalLinkage;
                table[fn.name] = llvm::Function::Create(fnTy, linkage,
                                                        mangle(pkg.importPath, fn.name), module_.get());
            }
        }
    }

    void CodeGen::bindImports(const Program &program)
    {
        fileImports_.clear();
        for (const Import &imp : program.imports)
        {
            const Package *target = ws_->byPath.at(imp.path);
            if (fileImports_.count(target->name))
                throw CompileError("package name '" + target->name + "' is imported more than once",
                                   imp.line, imp.col);
            fileImports_[target->name] = target;
        }
    }

    void CodeGen::genFunction(const Function &fn)
    {
        llvm::Function *f = functions_.at(pkg_->importPath).at(fn.name);
        builder_.SetInsertPoint(llvm::BasicBlock::Create(ctx_, "entry", f));
        locals_.clear();

        for (const StmtPtr &stmt : fn.body)
            genStmt(*stmt);
        builder_.CreateRetVoid();
    }

    void CodeGen::genEntryPoint(const Package &root, const Expr &exitCode)
    {
        const auto &table = functions_.at(root.importPath);
        auto it = table.find("main");
        if (it == table.end())
            throw CompileError("function 'main' is not defined", 1, 1);

        auto *mainTy = llvm::FunctionType::get(builder_.getInt32Ty(), false);
        auto *mainFn =
            llvm::Function::Create(mainTy, llvm::Function::ExternalLinkage, "main", module_.get());
        builder_.SetInsertPoint(llvm::BasicBlock::Create(ctx_, "entry", mainFn));
        locals_.clear();

        builder_.CreateCall(it->second);

        Value code = genExpr(exitCode);
        if (code.type != Ty::Int)
            throw CompileError("the final 'return' must be an integer", exitCode.line, exitCode.col);
        builder_.CreateRet(code.value);
    }

    void CodeGen::genStmt(const Stmt &stmt)
    {
        if (auto *decl = dynamic_cast<const VarDecl *>(&stmt))
        {
            if (locals_.count(decl->name))
                throw CompileError("variable '" + decl->name + "' is already declared", decl->line,
                                   decl->col);
            Value init = genExpr(*decl->init);
            auto *slot = builder_.CreateAlloca(llvmType(init.type), nullptr, decl->name);
            builder_.CreateStore(init.value, slot);
            locals_[decl->name] = Var{slot, init.type};
            return;
        }
        if (auto *es = dynamic_cast<const ExprStmt *>(&stmt))
        {
            genCallStmt(*static_cast<const Call *>(es->expr.get()));
            return;
        }
        throw CompileError("internal error: unknown statement", stmt.line, stmt.col);
    }

    void CodeGen::genCallStmt(const Call &call)
    {
        llvm::Function *target = nullptr;

        if (call.qualifier.empty())
        {
            if (call.callee == "print")
                return genPrint(call, false);
            if (call.callee == "println")
                return genPrint(call, true);

            const auto &table = functions_.at(pkg_->importPath);
            auto it = table.find(call.callee);
            if (it == table.end())
                throw CompileError("call to undefined function '" + call.callee + "'", call.line,
                                   call.col);
            target = it->second;
        }
        else
        {
            auto imp = fileImports_.find(call.qualifier);
            if (imp == fileImports_.end())
                throw CompileError("undefined package '" + call.qualifier + "'", call.line, call.col);
            std::string full = call.qualifier + "." + call.callee;
            if (!isPublic(call.callee))
                throw CompileError("'" + full + "' is not exported (lowercase names are private)",
                                   call.line, call.col);
            const auto &table = functions_.at(imp->second->importPath);
            auto it = table.find(call.callee);
            if (it == table.end())
                throw CompileError("package '" + call.qualifier + "' has no function '" +
                                       call.callee + "'",
                                   call.line, call.col);
            target = it->second;
        }

        if (!call.args.empty())
            throw CompileError("function '" + call.callee + "' takes no arguments", call.line,
                               call.col);
        builder_.CreateCall(target);
    }

    void CodeGen::genPrint(const Call &call, bool newline)
    {
        std::string fmt;
        std::vector<llvm::Value *> args{nullptr};
        for (const ExprPtr &a : call.args)
        {
            Value v = genExpr(*a);
            fmt += v.type == Ty::Int ? "%d" : "%s";
            args.push_back(v.value);
        }
        if (newline)
            fmt += "\n";
        args[0] = builder_.CreateGlobalString(fmt, ".fmt");

        auto *printfTy = llvm::FunctionType::get(builder_.getInt32Ty(), {builder_.getPtrTy()}, true);
        llvm::FunctionCallee printfFn = module_->getOrInsertFunction("printf", printfTy);
        builder_.CreateCall(printfFn, args);
    }

    CodeGen::Value CodeGen::genExpr(const Expr &expr)
    {
        if (auto *lit = dynamic_cast<const IntLit *>(&expr))
        {
            if (lit->value > INT_MAX)
                throw CompileError("integer literal does not fit into 'int' (32 bits)", lit->line,
                                   lit->col);
            return {builder_.getInt32(static_cast<uint32_t>(lit->value)), Ty::Int};
        }
        if (auto *lit = dynamic_cast<const StringLit *>(&expr))
            return {builder_.CreateGlobalString(lit->value, ".str"), Ty::String};

        if (auto *ref = dynamic_cast<const VarRef *>(&expr))
        {
            auto it = locals_.find(ref->name);
            if (it == locals_.end())
                throw CompileError("undefined variable '" + ref->name + "'", ref->line, ref->col);
            return {builder_.CreateLoad(llvmType(it->second.type), it->second.slot, ref->name),
                    it->second.type};
        }
        if (auto *un = dynamic_cast<const Unary *>(&expr))
        {
            Value v = genExpr(*un->operand);
            if (v.type != Ty::Int)
                throw CompileError("unary '-' needs an integer operand", un->line, un->col);
            return {builder_.CreateNeg(v.value), Ty::Int};
        }
        if (auto *bin = dynamic_cast<const Binary *>(&expr))
            return genBinary(*bin);

        if (auto *call = dynamic_cast<const Call *>(&expr))
            throw CompileError("'" + call->callee + "' does not return a value", call->line,
                               call->col);

        throw CompileError("internal error: unknown expression", expr.line, expr.col);
    }

    CodeGen::Value CodeGen::genBinary(const Binary &bin)
    {
        Value l = genExpr(*bin.lhs);
        Value r = genExpr(*bin.rhs);
        if (l.type != Ty::Int || r.type != Ty::Int)
            throw CompileError(std::string("operator '") + bin.op + "' needs integer operands",
                               bin.line, bin.col);
        switch (bin.op)
        {
        case '+':
            return {builder_.CreateAdd(l.value, r.value), Ty::Int};
        case '-':
            return {builder_.CreateSub(l.value, r.value), Ty::Int};
        case '*':
            return {builder_.CreateMul(l.value, r.value), Ty::Int};
        case '/':
            return {builder_.CreateSDiv(l.value, r.value), Ty::Int};
        }
        throw CompileError("internal error: unknown operator", bin.line, bin.col);
    }

}

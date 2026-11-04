#include "codegen/CodeGen.h"

#include "common/Error.h"

#include <llvm/IR/Verifier.h>
#include <llvm/Support/raw_ostream.h>

#include <climits>

namespace pgt
{

    CodeGen::CodeGen(const std::string &moduleName)
        : module_(std::make_unique<llvm::Module>(moduleName, ctx_)), builder_(ctx_)
    {
    }

    llvm::Type *CodeGen::llvmType(Ty t)
    {
        switch (t)
        {
        case Ty::Int:
            return builder_.getInt32Ty();
        case Ty::Bool:
            return builder_.getInt1Ty();
        case Ty::String:
            return builder_.getPtrTy();
        }
        return nullptr;
    }

    const char *CodeGen::tyName(Ty t)
    {
        switch (t)
        {
        case Ty::Int:
            return "int";
        case Ty::Bool:
            return "bool";
        case Ty::String:
            return "string";
        }
        return "?";
    }

    bool CodeGen::isPublic(const std::string &name)
    {
        return !name.empty() && name[0] >= 'A' && name[0] <= 'Z';
    }

    std::string CodeGen::mangle(const std::string &importPath, const std::string &name)
    {
        return "pgt." + importPath + "." + name;
    }

    llvm::AllocaInst *CodeGen::entryAlloca(llvm::Type *type, const std::string &name)
    {
        llvm::Function *f = builder_.GetInsertBlock()->getParent();
        llvm::IRBuilder<> entry(&f->getEntryBlock(), f->getEntryBlock().begin());
        return entry.CreateAlloca(type, nullptr, name);
    }

    void CodeGen::pushScope()
    {
        scopes_.emplace_back();
    }

    void CodeGen::popScope()
    {
        scopes_.pop_back();
    }

    CodeGen::Var *CodeGen::lookup(const std::string &name)
    {
        for (auto it = scopes_.rbegin(); it != scopes_.rend(); ++it)
        {
            auto found = it->find(name);
            if (found != it->end())
                return &found->second;
        }
        return nullptr;
    }

    void CodeGen::declare(const std::string &name, Var var, int line, int col)
    {
        if (scopes_.back().count(name))
            throw CompileError("variable '" + name + "' is already declared in this scope", line, col);
        scopes_.back()[name] = var;
    }

    void CodeGen::startDeadBlock()
    {
        llvm::Function *f = builder_.GetInsertBlock()->getParent();
        builder_.SetInsertPoint(llvm::BasicBlock::Create(ctx_, "dead", f));
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
                table[fn.name] = llvm::Function::Create(fnTy, linkage, mangle(pkg.importPath, fn.name),
                                                        module_.get());
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
        scopes_.clear();
        loops_.clear();

        genBlock(fn.body);
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
        scopes_.clear();
        pushScope();
        loops_.clear();

        builder_.CreateCall(it->second);

        Value code = genExpr(exitCode);
        if (code.type != Ty::Int)
            throw CompileError("the final 'return' must be an integer", exitCode.line, exitCode.col);
        builder_.CreateRet(code.value);
    }

    void CodeGen::genBlock(const Block &block)
    {
        pushScope();
        for (const StmtPtr &stmt : block)
            genStmt(*stmt);
        popScope();
    }

    void CodeGen::genStmt(const Stmt &stmt)
    {
        if (auto *decl = dynamic_cast<const VarDecl *>(&stmt))
        {
            Value init = genExpr(*decl->init);
            auto *slot = entryAlloca(llvmType(init.type), decl->name);
            builder_.CreateStore(init.value, slot);
            declare(decl->name, Var{slot, init.type}, decl->line, decl->col);
            return;
        }
        if (auto *assign = dynamic_cast<const Assign *>(&stmt))
            return genAssign(*assign);
        if (auto *es = dynamic_cast<const ExprStmt *>(&stmt))
        {
            genCallStmt(*static_cast<const Call *>(es->expr.get()));
            return;
        }
        if (auto *node = dynamic_cast<const If *>(&stmt))
            return genIf(*node);
        if (auto *loop = dynamic_cast<const Loop *>(&stmt))
            return genLoop(*loop);
        if (auto *range = dynamic_cast<const RangeLoop *>(&stmt))
            return genRangeLoop(*range);
        if (dynamic_cast<const Break *>(&stmt))
        {
            if (loops_.empty())
                throw CompileError("'break' outside of a loop", stmt.line, stmt.col);
            builder_.CreateBr(loops_.back().breakTo);
            startDeadBlock();
            return;
        }
        if (dynamic_cast<const Continue *>(&stmt))
        {
            if (loops_.empty())
                throw CompileError("'continue' outside of a loop", stmt.line, stmt.col);
            builder_.CreateBr(loops_.back().continueTo);
            startDeadBlock();
            return;
        }
        throw CompileError("internal error: unknown statement", stmt.line, stmt.col);
    }

    void CodeGen::genAssign(const Assign &assign)
    {
        Var *var = lookup(assign.name);
        if (!var)
            throw CompileError("undefined variable '" + assign.name + "'", assign.line, assign.col);

        Value rhs = genExpr(*assign.value);

        if (assign.op == AssignOp::Set)
        {
            if (rhs.type != var->type)
                throw CompileError(std::string("cannot assign ") + tyName(rhs.type) +
                                       " to variable '" + assign.name + "' of type " +
                                       tyName(var->type),
                                   assign.line, assign.col);
            builder_.CreateStore(rhs.value, var->slot);
            return;
        }

        if (var->type != Ty::Int || rhs.type != Ty::Int)
            throw CompileError("compound assignment and '++'/'--' need integer operands", assign.line,
                               assign.col);
        llvm::Value *cur = builder_.CreateLoad(llvmType(Ty::Int), var->slot, assign.name);
        llvm::Value *res = nullptr;
        switch (assign.op)
        {
        case AssignOp::Add:
            res = builder_.CreateAdd(cur, rhs.value);
            break;
        case AssignOp::Sub:
            res = builder_.CreateSub(cur, rhs.value);
            break;
        case AssignOp::Mul:
            res = builder_.CreateMul(cur, rhs.value);
            break;
        case AssignOp::Div:
            res = builder_.CreateSDiv(cur, rhs.value);
            break;
        case AssignOp::Mod:
            res = builder_.CreateSRem(cur, rhs.value);
            break;
        case AssignOp::Set:
            break;
        }
        builder_.CreateStore(res, var->slot);
    }

    CodeGen::Value CodeGen::genCond(const Expr &expr)
    {
        Value v = genExpr(expr);
        if (v.type != Ty::Bool)
            throw CompileError(std::string("condition must be bool, found ") + tyName(v.type),
                               expr.line, expr.col);
        return v;
    }

    void CodeGen::genIf(const If &node)
    {
        llvm::Function *f = builder_.GetInsertBlock()->getParent();
        Value cond = genCond(*node.cond);

        auto *thenBB = llvm::BasicBlock::Create(ctx_, "if.then", f);
        auto *elseBB = node.hasElse ? llvm::BasicBlock::Create(ctx_, "if.else", f) : nullptr;
        auto *endBB = llvm::BasicBlock::Create(ctx_, "if.end", f);

        builder_.CreateCondBr(cond.value, thenBB, elseBB ? elseBB : endBB);

        builder_.SetInsertPoint(thenBB);
        genBlock(node.thenBody);
        builder_.CreateBr(endBB);

        if (elseBB)
        {
            builder_.SetInsertPoint(elseBB);
            genBlock(node.elseBody);
            builder_.CreateBr(endBB);
        }
        builder_.SetInsertPoint(endBB);
    }

    void CodeGen::genLoop(const Loop &loop)
    {
        llvm::Function *f = builder_.GetInsertBlock()->getParent();
        pushScope();
        if (loop.init)
            genStmt(*loop.init);

        auto *condBB = llvm::BasicBlock::Create(ctx_, "loop.cond", f);
        auto *bodyBB = llvm::BasicBlock::Create(ctx_, "loop.body", f);
        auto *postBB = llvm::BasicBlock::Create(ctx_, "loop.post", f);
        auto *endBB = llvm::BasicBlock::Create(ctx_, "loop.end", f);

        builder_.CreateBr(condBB);

        builder_.SetInsertPoint(condBB);
        if (loop.cond)
        {
            Value cond = genCond(*loop.cond);
            builder_.CreateCondBr(cond.value, bodyBB, endBB);
        }
        else
        {
            builder_.CreateBr(bodyBB);
        }

        builder_.SetInsertPoint(bodyBB);
        loops_.push_back({postBB, endBB});
        genBlock(loop.body);
        loops_.pop_back();
        builder_.CreateBr(postBB);

        builder_.SetInsertPoint(postBB);
        if (loop.post)
            genStmt(*loop.post);
        builder_.CreateBr(condBB);

        builder_.SetInsertPoint(endBB);
        popScope();
    }

    void CodeGen::genRangeLoop(const RangeLoop &loop)
    {
        llvm::Function *f = builder_.GetInsertBlock()->getParent();

        Value from = genExpr(*loop.from);
        Value to = genExpr(*loop.to);
        if (from.type != Ty::Int || to.type != Ty::Int)
            throw CompileError("the bounds of a range must be integers", loop.line, loop.col);

        pushScope();
        auto *slot = entryAlloca(llvmType(Ty::Int), loop.var);
        builder_.CreateStore(from.value, slot);
        declare(loop.var, Var{slot, Ty::Int}, loop.line, loop.col);

        auto *condBB = llvm::BasicBlock::Create(ctx_, "range.cond", f);
        auto *bodyBB = llvm::BasicBlock::Create(ctx_, "range.body", f);
        auto *postBB = llvm::BasicBlock::Create(ctx_, "range.post", f);
        auto *endBB = llvm::BasicBlock::Create(ctx_, "range.end", f);

        builder_.CreateBr(condBB);

        builder_.SetInsertPoint(condBB);
        llvm::Value *i = builder_.CreateLoad(llvmType(Ty::Int), slot, loop.var);
        builder_.CreateCondBr(builder_.CreateICmpSLT(i, to.value), bodyBB, endBB);

        builder_.SetInsertPoint(bodyBB);
        loops_.push_back({postBB, endBB});
        genBlock(loop.body);
        loops_.pop_back();
        builder_.CreateBr(postBB);

        builder_.SetInsertPoint(postBB);
        llvm::Value *cur = builder_.CreateLoad(llvmType(Ty::Int), slot, loop.var);
        builder_.CreateStore(builder_.CreateAdd(cur, builder_.getInt32(1)), slot);
        builder_.CreateBr(condBB);

        builder_.SetInsertPoint(endBB);
        popScope();
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
            if (call.callee == "input")
                return genInput(call, false);
            if (call.callee == "inputln")
                return genInput(call, true);

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
            if (v.type == Ty::Int)
            {
                fmt += "%d";
                args.push_back(v.value);
            }
            else if (v.type == Ty::Bool)
            {
                fmt += "%s";
                args.push_back(builder_.CreateSelect(v.value,
                                                     builder_.CreateGlobalString("true", ".true"),
                                                     builder_.CreateGlobalString("false", ".false")));
            }
            else
            {
                fmt += "%s";
                args.push_back(v.value);
            }
        }
        if (newline)
            fmt += "\n";
        args[0] = builder_.CreateGlobalString(fmt, ".fmt");

        auto *printfTy = llvm::FunctionType::get(builder_.getInt32Ty(), {builder_.getPtrTy()}, true);
        llvm::FunctionCallee printfFn = module_->getOrInsertFunction("printf", printfTy);
        builder_.CreateCall(printfFn, args);
    }

    void CodeGen::genInput(const Call &call, bool wholeLine)
    {
        const std::string name = wholeLine ? "inputln" : "input";
        if (call.args.empty() || call.args.size() > 2)
            throw CompileError("'" + name + "' takes an optional prompt and a variable", call.line,
                               call.col);

        auto *target = dynamic_cast<const VarRef *>(call.args.back().get());
        if (!target)
            throw CompileError("the last argument of '" + name + "' must be a variable", call.line,
                               call.col);
        Var *var = lookup(target->name);
        if (!var)
            throw CompileError("undefined variable '" + target->name + "'", target->line, target->col);

        auto *i32 = builder_.getInt32Ty();
        auto *ptr = builder_.getPtrTy();
        llvm::FunctionCallee printfFn =
            module_->getOrInsertFunction("printf", llvm::FunctionType::get(i32, {ptr}, true));
        llvm::FunctionCallee scanfFn =
            module_->getOrInsertFunction("scanf", llvm::FunctionType::get(i32, {ptr}, true));
        llvm::FunctionCallee fflushFn =
            module_->getOrInsertFunction("fflush", llvm::FunctionType::get(i32, {ptr}, false));

        if (call.args.size() == 2)
        {
            Value prompt = genExpr(*call.args.front());
            if (prompt.type != Ty::String)
                throw CompileError("the prompt of '" + name + "' must be a string", call.line,
                                   call.col);
            builder_.CreateCall(printfFn, {builder_.CreateGlobalString("%s", ".fmt"), prompt.value});
            builder_.CreateCall(fflushFn, {llvm::ConstantPointerNull::get(builder_.getPtrTy())});
        }

        if (wholeLine && var->type != Ty::String)
            throw CompileError("'inputln' needs a string variable", call.line, call.col);

        if (var->type == Ty::Int)
        {
            builder_.CreateCall(scanfFn, {builder_.CreateGlobalString("%d", ".fmt"), var->slot});
        }
        else if (var->type == Ty::String)
        {
            llvm::FunctionCallee callocFn = module_->getOrInsertFunction(
                "calloc",
                llvm::FunctionType::get(ptr, {builder_.getInt64Ty(), builder_.getInt64Ty()}, false));
            llvm::Value *buf =
                builder_.CreateCall(callocFn, {builder_.getInt64(4096), builder_.getInt64(1)});
            const char *fmt = wholeLine ? " %4095[^\n]" : " %4095s";
            builder_.CreateCall(scanfFn, {builder_.CreateGlobalString(fmt, ".fmt"), buf});
            builder_.CreateStore(buf, var->slot);
        }
        else
        {
            throw CompileError("'input' does not support bool variables", call.line, call.col);
        }
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
        if (auto *lit = dynamic_cast<const BoolLit *>(&expr))
            return {builder_.getInt1(lit->value), Ty::Bool};
        if (auto *lit = dynamic_cast<const StringLit *>(&expr))
            return {builder_.CreateGlobalString(lit->value, ".str"), Ty::String};

        if (auto *ref = dynamic_cast<const VarRef *>(&expr))
        {
            Var *var = lookup(ref->name);
            if (!var)
                throw CompileError("undefined variable '" + ref->name + "'", ref->line, ref->col);
            return {builder_.CreateLoad(llvmType(var->type), var->slot, ref->name), var->type};
        }
        if (auto *un = dynamic_cast<const Unary *>(&expr))
        {
            Value v = genExpr(*un->operand);
            if (un->op == UnOp::Neg)
            {
                if (v.type != Ty::Int)
                    throw CompileError("unary '-' needs an integer operand", un->line, un->col);
                return {builder_.CreateNeg(v.value), Ty::Int};
            }
            if (v.type != Ty::Bool)
                throw CompileError("'!' needs a bool operand", un->line, un->col);
            return {builder_.CreateNot(v.value), Ty::Bool};
        }
        if (auto *bin = dynamic_cast<const Binary *>(&expr))
            return genBinary(*bin);

        if (auto *call = dynamic_cast<const Call *>(&expr))
            throw CompileError("'" + call->callee + "' does not return a value", call->line,
                               call->col);

        throw CompileError("internal error: unknown expression", expr.line, expr.col);
    }

    CodeGen::Value CodeGen::genLogical(const Binary &bin)
    {
        llvm::Function *f = builder_.GetInsertBlock()->getParent();
        bool isAnd = bin.op == BinOp::And;
        auto needBool = [&]
        {
            return CompileError(std::string("operator '") + opText(bin.op) + "' needs bool operands",
                                bin.line, bin.col);
        };

        Value lhs = genExpr(*bin.lhs);
        if (lhs.type != Ty::Bool)
            throw needBool();
        llvm::BasicBlock *lhsEnd = builder_.GetInsertBlock();
        auto *rhsBB = llvm::BasicBlock::Create(ctx_, isAnd ? "and.rhs" : "or.rhs", f);
        auto *endBB = llvm::BasicBlock::Create(ctx_, isAnd ? "and.end" : "or.end", f);
        if (isAnd)
            builder_.CreateCondBr(lhs.value, rhsBB, endBB);
        else
            builder_.CreateCondBr(lhs.value, endBB, rhsBB);

        builder_.SetInsertPoint(rhsBB);
        Value rhs = genExpr(*bin.rhs);
        if (rhs.type != Ty::Bool)
            throw needBool();
        llvm::BasicBlock *rhsEnd = builder_.GetInsertBlock();
        builder_.CreateBr(endBB);

        builder_.SetInsertPoint(endBB);
        llvm::PHINode *phi = builder_.CreatePHI(builder_.getInt1Ty(), 2);
        phi->addIncoming(builder_.getInt1(!isAnd), lhsEnd);
        phi->addIncoming(rhs.value, rhsEnd);
        return {phi, Ty::Bool};
    }

    CodeGen::Value CodeGen::genCompare(const Binary &bin)
    {
        Value l = genExpr(*bin.lhs);
        Value r = genExpr(*bin.rhs);
        bool equality = bin.op == BinOp::Eq || bin.op == BinOp::Ne;

        if (l.type != r.type)
            throw CompileError(std::string("cannot compare ") + tyName(l.type) + " with " +
                                   tyName(r.type),
                               bin.line, bin.col);
        if (l.type == Ty::String)
            throw CompileError("comparing strings is not supported yet", bin.line, bin.col);
        if (l.type == Ty::Bool && !equality)
            throw CompileError(std::string("operator '") + opText(bin.op) + "' cannot compare bools",
                               bin.line, bin.col);

        llvm::Value *res = nullptr;
        switch (bin.op)
        {
        case BinOp::Eq:
            res = builder_.CreateICmpEQ(l.value, r.value);
            break;
        case BinOp::Ne:
            res = builder_.CreateICmpNE(l.value, r.value);
            break;
        case BinOp::Lt:
            res = builder_.CreateICmpSLT(l.value, r.value);
            break;
        case BinOp::Gt:
            res = builder_.CreateICmpSGT(l.value, r.value);
            break;
        case BinOp::Le:
            res = builder_.CreateICmpSLE(l.value, r.value);
            break;
        default:
            res = builder_.CreateICmpSGE(l.value, r.value);
            break;
        }
        return {res, Ty::Bool};
    }

    CodeGen::Value CodeGen::genBinary(const Binary &bin)
    {
        switch (bin.op)
        {
        case BinOp::And:
        case BinOp::Or:
            return genLogical(bin);
        case BinOp::Eq:
        case BinOp::Ne:
        case BinOp::Lt:
        case BinOp::Gt:
        case BinOp::Le:
        case BinOp::Ge:
            return genCompare(bin);
        default:
            break;
        }

        Value l = genExpr(*bin.lhs);
        Value r = genExpr(*bin.rhs);
        if (l.type != Ty::Int || r.type != Ty::Int)
            throw CompileError(std::string("operator '") + opText(bin.op) + "' needs integer operands",
                               bin.line, bin.col);
        switch (bin.op)
        {
        case BinOp::Add:
            return {builder_.CreateAdd(l.value, r.value), Ty::Int};
        case BinOp::Sub:
            return {builder_.CreateSub(l.value, r.value), Ty::Int};
        case BinOp::Mul:
            return {builder_.CreateMul(l.value, r.value), Ty::Int};
        case BinOp::Div:
            return {builder_.CreateSDiv(l.value, r.value), Ty::Int};
        case BinOp::Mod:
            return {builder_.CreateSRem(l.value, r.value), Ty::Int};
        default:
            break;
        }
        throw CompileError("internal error: unknown operator", bin.line, bin.col);
    }

} // namespace pgt

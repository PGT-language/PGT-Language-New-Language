#include "codegen/CodeGen.h"

#include "ast/Analysis.h"
#include "common/Error.h"

#include <llvm/IR/Verifier.h>
#include <llvm/Support/raw_ostream.h>

#include <climits>

namespace pgt
{

    namespace
    {

        bool isConstExpr(const Expr &e)
        {
            if (dynamic_cast<const IntLit *>(&e) || dynamic_cast<const FloatLit *>(&e))
                return true;
            if (auto *un = dynamic_cast<const Unary *>(&e))
                return un->op == UnOp::Neg && isConstExpr(*un->operand);
            if (auto *bin = dynamic_cast<const Binary *>(&e))
            {
                switch (bin->op)
                {
                case BinOp::Add:
                case BinOp::Sub:
                case BinOp::Mul:
                case BinOp::Div:
                case BinOp::Mod:
                    return isConstExpr(*bin->lhs) && isConstExpr(*bin->rhs);
                default:
                    return false;
                }
            }
            return false;
        }

        bool hasFloatLit(const Expr &e)
        {
            if (dynamic_cast<const FloatLit *>(&e))
                return true;
            if (auto *un = dynamic_cast<const Unary *>(&e))
                return hasFloatLit(*un->operand);
            if (auto *bin = dynamic_cast<const Binary *>(&e))
                return hasFloatLit(*bin->lhs) || hasFloatLit(*bin->rhs);
            return false;
        }

    } // namespace

    CodeGen::CodeGen(const std::string &moduleName)
        : module_(std::make_unique<llvm::Module>(moduleName, ctx_)), builder_(ctx_)
    {
    }

    llvm::Type *CodeGen::llvmType(Ty t)
    {
        switch (t)
        {
        case Ty::Void:
            return builder_.getVoidTy();
        case Ty::Int:
            return builder_.getInt32Ty();
        case Ty::Int64:
            return builder_.getInt64Ty();
        case Ty::Float:
            return builder_.getDoubleTy();
        case Ty::Char:
            return builder_.getInt8Ty();
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
        case Ty::Void:
            return "void";
        case Ty::Int:
            return "int";
        case Ty::Int64:
            return "int64";
        case Ty::Float:
            return "float";
        case Ty::Char:
            return "char";
        case Ty::Bool:
            return "bool";
        case Ty::String:
            return "string";
        }
        return "?";
    }

    bool CodeGen::isIntegral(Ty t)
    {
        return t == Ty::Int || t == Ty::Int64 || t == Ty::Char;
    }

    bool CodeGen::isNumeric(Ty t)
    {
        return isIntegral(t) || t == Ty::Float;
    }

    std::optional<CodeGen::Ty> CodeGen::builtinType(const std::string &name)
    {
        if (name == "int")
            return Ty::Int;
        if (name == "int64")
            return Ty::Int64;
        if (name == "float")
            return Ty::Float;
        if (name == "char")
            return Ty::Char;
        if (name == "string")
            return Ty::String;
        if (name == "bool")
            return Ty::Bool;
        if (name == "void")
            return Ty::Void;
        return std::nullopt;
    }

    CodeGen::Ty CodeGen::resolveType(const TypeExpr &type) const
    {
        if (auto t = builtinType(type.name))
            return *t;
        throw CompileError("unknown type '" + type.name + "'", type.line, type.col);
    }

    llvm::Value *CodeGen::zeroValue(Ty t)
    {
        switch (t)
        {
        case Ty::Int:
            return builder_.getInt32(0);
        case Ty::Int64:
            return builder_.getInt64(0);
        case Ty::Float:
            return llvm::ConstantFP::get(builder_.getDoubleTy(), 0.0);
        case Ty::Char:
            return builder_.getInt8(0);
        case Ty::Bool:
            return builder_.getInt1(false);
        case Ty::String:
            return builder_.CreateGlobalString("", ".empty");
        case Ty::Void:
            break;
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

                FuncInfo info;
                std::vector<llvm::Type *> llvmParams;
                for (const Param &param : fn.params)
                {
                    Ty t = resolveType(param.type);
                    if (t == Ty::Void)
                        throw CompileError("parameter '" + param.name + "' cannot have type void",
                                           param.type.line, param.type.col);
                    info.params.push_back(t);
                    llvmParams.push_back(llvmType(t));
                }
                info.ret = fn.ret ? resolveType(*fn.ret) : Ty::Void;

                auto *fnTy = llvm::FunctionType::get(llvmType(info.ret), llvmParams, false);
                auto linkage = isPublic(fn.name) ? llvm::Function::ExternalLinkage
                                                 : llvm::Function::InternalLinkage;
                info.fn = llvm::Function::Create(fnTy, linkage, mangle(pkg.importPath, fn.name),
                                                 module_.get());
                table[fn.name] = info;
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
        const FuncInfo &info = functions_.at(pkg_->importPath).at(fn.name);
        builder_.SetInsertPoint(llvm::BasicBlock::Create(ctx_, "entry", info.fn));
        scopes_.clear();
        loops_.clear();
        currentRet_ = info.ret;

        pushScope();
        size_t i = 0;
        for (llvm::Argument &arg : info.fn->args())
        {
            const Param &param = fn.params[i];
            arg.setName(param.name);
            auto *slot = entryAlloca(llvmType(info.params[i]), param.name);
            builder_.CreateStore(&arg, slot);
            declare(param.name, Var{slot, info.params[i]}, param.line, param.col);
            ++i;
        }

        if (info.ret != Ty::Void && !terminates(fn.body))
            throw CompileError("missing return at the end of function '" + fn.name + "'", fn.line,
                               fn.col);

        genBlock(fn.body);
        popScope();

        if (info.ret == Ty::Void)
            builder_.CreateRetVoid();
        else
            builder_.CreateUnreachable();
    }

    void CodeGen::genEntryPoint(const Package &root, const Expr &exitCode)
    {
        const auto &table = functions_.at(root.importPath);
        auto it = table.find("main");
        if (it == table.end())
            throw CompileError("function 'main' is not defined", 1, 1);
        if (!it->second.params.empty())
            throw CompileError("function 'main' must not take parameters", 1, 1);

        auto *mainTy = llvm::FunctionType::get(builder_.getInt32Ty(), false);
        auto *mainFn =
            llvm::Function::Create(mainTy, llvm::Function::ExternalLinkage, "main", module_.get());
        builder_.SetInsertPoint(llvm::BasicBlock::Create(ctx_, "entry", mainFn));
        scopes_.clear();
        pushScope();
        loops_.clear();

        builder_.CreateCall(it->second.fn);

        Value code = genExpr(exitCode, Ty::Int);
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
            return genVarDecl(*decl);
        if (auto *assign = dynamic_cast<const Assign *>(&stmt))
            return genAssign(*assign);
        if (auto *ret = dynamic_cast<const Return *>(&stmt))
            return genReturn(*ret);
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

    void CodeGen::genVarDecl(const VarDecl &decl)
    {
        Ty ty;
        llvm::Value *init;
        if (decl.type)
        {
            ty = resolveType(*decl.type);
            if (ty == Ty::Void)
                throw CompileError("a variable cannot have type void", decl.type->line,
                                   decl.type->col);
            if (decl.init)
            {
                Value v = genExpr(*decl.init, ty);
                if (v.type != ty)
                    throw CompileError(std::string("cannot initialize ") + tyName(ty) +
                                           " variable '" + decl.name + "' with " + tyName(v.type),
                                       decl.line, decl.col);
                init = v.value;
            }
            else
            {
                init = zeroValue(ty);
            }
        }
        else
        {
            Value v = genExpr(*decl.init);
            ty = v.type;
            init = v.value;
        }
        auto *slot = entryAlloca(llvmType(ty), decl.name);
        builder_.CreateStore(init, slot);
        declare(decl.name, Var{slot, ty}, decl.line, decl.col);
    }

    void CodeGen::genAssign(const Assign &assign)
    {
        Var *var = lookup(assign.name);
        if (!var)
            throw CompileError("undefined variable '" + assign.name + "'", assign.line, assign.col);

        Value rhs = genExpr(*assign.value, var->type);

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

        BinOp op = BinOp::Add;
        switch (assign.op)
        {
        case AssignOp::Add:
            op = BinOp::Add;
            break;
        case AssignOp::Sub:
            op = BinOp::Sub;
            break;
        case AssignOp::Mul:
            op = BinOp::Mul;
            break;
        case AssignOp::Div:
            op = BinOp::Div;
            break;
        case AssignOp::Mod:
            op = BinOp::Mod;
            break;
        case AssignOp::Set:
            break;
        }
        Value cur{builder_.CreateLoad(llvmType(var->type), var->slot, assign.name), var->type};
        Value res = genArith(op, cur, rhs, assign.line, assign.col);
        builder_.CreateStore(res.value, var->slot);
    }

    void CodeGen::genReturn(const Return &ret)
    {
        if (currentRet_ == Ty::Void)
        {
            if (ret.value)
                throw CompileError("this function does not return a value", ret.line, ret.col);
            builder_.CreateRetVoid();
        }
        else
        {
            if (!ret.value)
                throw CompileError(std::string("missing return value of type ") + tyName(currentRet_),
                                   ret.line, ret.col);
            Value v = genExpr(*ret.value, currentRet_);
            if (v.type != currentRet_)
                throw CompileError(std::string("cannot return ") + tyName(v.type) +
                                       " from a function returning " + tyName(currentRet_),
                                   ret.line, ret.col);
            builder_.CreateRet(v.value);
        }
        startDeadBlock();
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

        Value from = genExpr(*loop.from, Ty::Int);
        Value to = genExpr(*loop.to, from.type);
        if (!isIntegral(from.type) || from.type == Ty::Char || to.type != from.type)
            throw CompileError("the bounds of a range must be integers of the same type", loop.line,
                               loop.col);
        Ty ty = from.type;

        pushScope();
        auto *slot = entryAlloca(llvmType(ty), loop.var);
        builder_.CreateStore(from.value, slot);
        declare(loop.var, Var{slot, ty}, loop.line, loop.col);

        auto *condBB = llvm::BasicBlock::Create(ctx_, "range.cond", f);
        auto *bodyBB = llvm::BasicBlock::Create(ctx_, "range.body", f);
        auto *postBB = llvm::BasicBlock::Create(ctx_, "range.post", f);
        auto *endBB = llvm::BasicBlock::Create(ctx_, "range.end", f);

        builder_.CreateBr(condBB);

        builder_.SetInsertPoint(condBB);
        llvm::Value *i = builder_.CreateLoad(llvmType(ty), slot, loop.var);
        builder_.CreateCondBr(builder_.CreateICmpSLT(i, to.value), bodyBB, endBB);

        builder_.SetInsertPoint(bodyBB);
        loops_.push_back({postBB, endBB});
        genBlock(loop.body);
        loops_.pop_back();
        builder_.CreateBr(postBB);

        builder_.SetInsertPoint(postBB);
        llvm::Value *cur = builder_.CreateLoad(llvmType(ty), slot, loop.var);
        builder_.CreateStore(builder_.CreateAdd(cur, llvm::ConstantInt::get(llvmType(ty), 1)), slot);
        builder_.CreateBr(condBB);

        builder_.SetInsertPoint(endBB);
        popScope();
    }

    void CodeGen::genCallStmt(const Call &call)
    {
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
            if (builtinType(call.callee))
                throw CompileError("the result of the conversion '" + call.callee + "' is unused",
                                   call.line, call.col);
        }
        genCall(call);
    }

    CodeGen::Value CodeGen::genCall(const Call &call)
    {
        const FuncInfo *info = nullptr;

        if (call.qualifier.empty())
        {
            if (call.callee == "print" || call.callee == "println" || call.callee == "input" ||
                call.callee == "inputln")
                throw CompileError("'" + call.callee + "' does not return a value", call.line,
                                   call.col);
            if (auto t = builtinType(call.callee))
                return genConversion(call, *t);

            const auto &table = functions_.at(pkg_->importPath);
            auto it = table.find(call.callee);
            if (it == table.end())
                throw CompileError("call to undefined function '" + call.callee + "'", call.line,
                                   call.col);
            info = &it->second;
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
            info = &it->second;
        }

        const std::string name =
            call.qualifier.empty() ? call.callee : call.qualifier + "." + call.callee;
        if (call.args.size() != info->params.size())
            throw CompileError("function '" + name + "' takes " + std::to_string(info->params.size()) +
                                   " argument(s), got " + std::to_string(call.args.size()),
                               call.line, call.col);

        std::vector<llvm::Value *> args;
        for (size_t i = 0; i < call.args.size(); ++i)
        {
            Value a = genExpr(*call.args[i], info->params[i]);
            if (a.type != info->params[i])
                throw CompileError("argument " + std::to_string(i + 1) + " of '" + name +
                                       "': expected " + tyName(info->params[i]) + ", found " +
                                       tyName(a.type),
                                   call.args[i]->line, call.args[i]->col);
            args.push_back(a.value);
        }

        llvm::Value *result = builder_.CreateCall(info->fn, args);
        return {info->ret == Ty::Void ? nullptr : result, info->ret};
    }

    CodeGen::Value CodeGen::convert(Value v, Ty to, int line, int col)
    {
        if (v.type == to)
            return v;

        auto fail = [&]()
        {
            return CompileError(std::string("cannot convert ") + tyName(v.type) + " to " +
                                    tyName(to),
                                line, col);
        };

        if (isIntegral(v.type) && isIntegral(to))
        {
            bool isSigned = v.type != Ty::Char;
            return {builder_.CreateIntCast(v.value, llvmType(to), isSigned), to};
        }
        if (isIntegral(v.type) && to == Ty::Float)
        {
            llvm::Value *r = v.type == Ty::Char ? builder_.CreateUIToFP(v.value, llvmType(to))
                                                : builder_.CreateSIToFP(v.value, llvmType(to));
            return {r, to};
        }
        if (v.type == Ty::Float && isIntegral(to))
        {
            llvm::Value *r = to == Ty::Char ? builder_.CreateFPToUI(v.value, llvmType(to))
                                            : builder_.CreateFPToSI(v.value, llvmType(to));
            return {r, to};
        }
        if (v.type == Ty::Bool && isIntegral(to))
            return {builder_.CreateZExt(v.value, llvmType(to)), to};
        if (isIntegral(v.type) && to == Ty::Bool)
            return {builder_.CreateICmpNE(v.value, llvm::ConstantInt::get(llvmType(v.type), 0)), to};
        if (v.type == Ty::Float && to == Ty::Bool)
            return {builder_.CreateFCmpUNE(v.value, llvm::ConstantFP::get(llvmType(v.type), 0.0)), to};
        throw fail();
    }

    CodeGen::Value CodeGen::genConversion(const Call &call, Ty target)
    {
        if (call.args.size() != 1)
            throw CompileError("the conversion '" + call.callee + "' takes exactly one argument",
                               call.line, call.col);
        if (target == Ty::Void)
            throw CompileError("cannot convert to void", call.line, call.col);
        Value v = genExpr(*call.args[0], target);
        return convert(v, target, call.line, call.col);
    }

    void CodeGen::genPrint(const Call &call, bool newline)
    {
        std::string fmt;
        std::vector<llvm::Value *> args{nullptr};
        for (const ExprPtr &a : call.args)
        {
            Value v = genExpr(*a);
            switch (v.type)
            {
            case Ty::Int:
                fmt += "%d";
                args.push_back(v.value);
                break;
            case Ty::Int64:
                fmt += "%lld";
                args.push_back(v.value);
                break;
            case Ty::Float:
                fmt += "%g";
                args.push_back(v.value);
                break;
            case Ty::Char:
                fmt += "%c";
                args.push_back(builder_.CreateZExt(v.value, builder_.getInt32Ty()));
                break;
            case Ty::Bool:
                fmt += "%s";
                args.push_back(builder_.CreateSelect(v.value,
                                                     builder_.CreateGlobalString("true", ".true"),
                                                     builder_.CreateGlobalString("false", ".false")));
                break;
            case Ty::String:
                fmt += "%s";
                args.push_back(v.value);
                break;
            case Ty::Void:
                break;
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

        const char *scalarFmt = nullptr;
        switch (var->type)
        {
        case Ty::Int:
            scalarFmt = "%d";
            break;
        case Ty::Int64:
            scalarFmt = "%lld";
            break;
        case Ty::Float:
            scalarFmt = "%lf";
            break;
        case Ty::Char:
            scalarFmt = " %c";
            break;
        default:
            break;
        }

        if (scalarFmt)
        {
            builder_.CreateCall(scanfFn, {builder_.CreateGlobalString(scalarFmt, ".fmt"), var->slot});
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

    CodeGen::Value CodeGen::genExpr(const Expr &expr, Ty hint)
    {
        if (auto *lit = dynamic_cast<const IntLit *>(&expr))
        {
            Ty t = (hint == Ty::Int64 || hint == Ty::Float || hint == Ty::Char) ? hint : Ty::Int;
            switch (t)
            {
            case Ty::Int64:
                return {builder_.getInt64(static_cast<uint64_t>(lit->value)), t};
            case Ty::Float:
                return {llvm::ConstantFP::get(builder_.getDoubleTy(), static_cast<double>(lit->value)),
                        t};
            case Ty::Char:
                if (lit->value > 255)
                    throw CompileError("constant " + std::to_string(lit->value) + " overflows char",
                                       lit->line, lit->col);
                return {builder_.getInt8(static_cast<uint8_t>(lit->value)), t};
            default:
                if (lit->value > INT_MAX)
                    throw CompileError("integer literal does not fit into 'int' (32 bits)", lit->line,
                                       lit->col);
                return {builder_.getInt32(static_cast<uint32_t>(lit->value)), Ty::Int};
            }
        }
        if (auto *lit = dynamic_cast<const FloatLit *>(&expr))
            return {llvm::ConstantFP::get(builder_.getDoubleTy(), lit->value), Ty::Float};
        if (auto *lit = dynamic_cast<const CharLit *>(&expr))
            return {builder_.getInt8(static_cast<uint8_t>(lit->value)), Ty::Char};
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
            Value v = genExpr(*un->operand, un->op == UnOp::Neg ? hint : Ty::Void);
            if (un->op == UnOp::Neg)
            {
                if (v.type == Ty::Float)
                    return {builder_.CreateFNeg(v.value), Ty::Float};
                if (v.type != Ty::Int && v.type != Ty::Int64)
                    throw CompileError(std::string("unary '-' is not defined for ") + tyName(v.type),
                                       un->line, un->col);
                return {builder_.CreateNeg(v.value), v.type};
            }
            if (v.type != Ty::Bool)
                throw CompileError("'!' needs a bool operand", un->line, un->col);
            return {builder_.CreateNot(v.value), Ty::Bool};
        }
        if (auto *bin = dynamic_cast<const Binary *>(&expr))
            return genBinary(*bin, hint);

        if (auto *call = dynamic_cast<const Call *>(&expr))
        {
            Value v = genCall(*call);
            if (v.type == Ty::Void)
                throw CompileError("'" + call->callee + "' does not return a value", call->line,
                                   call->col);
            return v;
        }

        throw CompileError("internal error: unknown expression", expr.line, expr.col);
    }

    void CodeGen::genOperands(const Expr &lhs, const Expr &rhs, Ty hint, Value &l, Value &r)
    {
        bool lc = isConstExpr(lhs), rc = isConstExpr(rhs);
        if (lc && rc)
        {
            Ty h = hint;
            if (h == Ty::Void && (hasFloatLit(lhs) || hasFloatLit(rhs)))
                h = Ty::Float;
            l = genExpr(lhs, h);
            r = genExpr(rhs, h);
        }
        else if (lc)
        {
            r = genExpr(rhs);
            l = genExpr(lhs, r.type);
        }
        else if (rc)
        {
            l = genExpr(lhs);
            r = genExpr(rhs, l.type);
        }
        else
        {
            l = genExpr(lhs);
            r = genExpr(rhs);
        }
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
        Value l, r;
        genOperands(*bin.lhs, *bin.rhs, Ty::Void, l, r);
        bool equality = bin.op == BinOp::Eq || bin.op == BinOp::Ne;

        if (l.type != r.type)
            throw CompileError(std::string("cannot compare ") + tyName(l.type) + " with " +
                                   tyName(r.type) + " (use an explicit conversion)",
                               bin.line, bin.col);
        if (l.type == Ty::String)
            throw CompileError("comparing strings is not supported yet", bin.line, bin.col);
        if (l.type == Ty::Bool && !equality)
            throw CompileError(std::string("operator '") + opText(bin.op) + "' cannot compare bools",
                               bin.line, bin.col);

        llvm::Value *res = nullptr;
        if (l.type == Ty::Float)
        {
            switch (bin.op)
            {
            case BinOp::Eq:
                res = builder_.CreateFCmpOEQ(l.value, r.value);
                break;
            case BinOp::Ne:
                res = builder_.CreateFCmpUNE(l.value, r.value);
                break;
            case BinOp::Lt:
                res = builder_.CreateFCmpOLT(l.value, r.value);
                break;
            case BinOp::Gt:
                res = builder_.CreateFCmpOGT(l.value, r.value);
                break;
            case BinOp::Le:
                res = builder_.CreateFCmpOLE(l.value, r.value);
                break;
            default:
                res = builder_.CreateFCmpOGE(l.value, r.value);
                break;
            }
        }
        else
        {
            bool isSigned = l.type != Ty::Char;
            switch (bin.op)
            {
            case BinOp::Eq:
                res = builder_.CreateICmpEQ(l.value, r.value);
                break;
            case BinOp::Ne:
                res = builder_.CreateICmpNE(l.value, r.value);
                break;
            case BinOp::Lt:
                res = isSigned ? builder_.CreateICmpSLT(l.value, r.value)
                               : builder_.CreateICmpULT(l.value, r.value);
                break;
            case BinOp::Gt:
                res = isSigned ? builder_.CreateICmpSGT(l.value, r.value)
                               : builder_.CreateICmpUGT(l.value, r.value);
                break;
            case BinOp::Le:
                res = isSigned ? builder_.CreateICmpSLE(l.value, r.value)
                               : builder_.CreateICmpULE(l.value, r.value);
                break;
            default:
                res = isSigned ? builder_.CreateICmpSGE(l.value, r.value)
                               : builder_.CreateICmpUGE(l.value, r.value);
                break;
            }
        }
        return {res, Ty::Bool};
    }

    CodeGen::Value CodeGen::genArith(BinOp op, Value l, Value r, int line, int col)
    {
        if (l.type != r.type)
            throw CompileError(std::string("mismatched types ") + tyName(l.type) + " and " +
                                   tyName(r.type) + " for operator '" + opText(op) +
                                   "' (use an explicit conversion)",
                               line, col);
        Ty t = l.type;
        if (!isNumeric(t))
            throw CompileError(std::string("operator '") + opText(op) + "' is not defined for " +
                                   tyName(t),
                               line, col);

        if (t == Ty::Float)
        {
            switch (op)
            {
            case BinOp::Add:
                return {builder_.CreateFAdd(l.value, r.value), t};
            case BinOp::Sub:
                return {builder_.CreateFSub(l.value, r.value), t};
            case BinOp::Mul:
                return {builder_.CreateFMul(l.value, r.value), t};
            case BinOp::Div:
                return {builder_.CreateFDiv(l.value, r.value), t};
            default:
                throw CompileError("operator '%' needs integer operands", line, col);
            }
        }

        bool isSigned = t != Ty::Char;
        switch (op)
        {
        case BinOp::Add:
            return {builder_.CreateAdd(l.value, r.value), t};
        case BinOp::Sub:
            return {builder_.CreateSub(l.value, r.value), t};
        case BinOp::Mul:
            return {builder_.CreateMul(l.value, r.value), t};
        case BinOp::Div:
            return {isSigned ? builder_.CreateSDiv(l.value, r.value)
                             : builder_.CreateUDiv(l.value, r.value),
                    t};
        case BinOp::Mod:
            return {isSigned ? builder_.CreateSRem(l.value, r.value)
                             : builder_.CreateURem(l.value, r.value),
                    t};
        default:
            break;
        }
        throw CompileError("internal error: unknown operator", line, col);
    }

    CodeGen::Value CodeGen::genBinary(const Binary &bin, Ty hint)
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

        Value l, r;
        genOperands(*bin.lhs, *bin.rhs, hint, l, r);
        return genArith(bin.op, l, r, bin.line, bin.col);
    }

} // namespace pgt

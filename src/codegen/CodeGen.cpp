#include "codegen/CodeGen.h"

#include "ast/Analysis.h"
#include "common/Error.h"
#include "common/Paths.h"

#include <filesystem>

#include <llvm/IR/Verifier.h>
#include <llvm/Support/raw_ostream.h>

#include <climits>
#include <functional>

namespace pgt
{

    namespace
    {

        void collectExpr(const Expr *e, std::set<std::string> &out);

        void collectBlock(const Block &b, std::set<std::string> &out);

        void collectStmt(const Stmt *s, std::set<std::string> &out)
        {
            if (!s)
                return;
            if (auto *d = dynamic_cast<const VarDecl *>(s))
                collectExpr(d->init.get(), out);
            else if (auto *a = dynamic_cast<const Assign *>(s))
            {
                collectExpr(a->target.get(), out);
                collectExpr(a->value.get(), out);
            }
            else if (auto *es = dynamic_cast<const ExprStmt *>(s))
                collectExpr(es->expr.get(), out);
            else if (auto *i = dynamic_cast<const If *>(s))
            {
                collectExpr(i->cond.get(), out);
                collectBlock(i->thenBody, out);
                collectBlock(i->elseBody, out);
            }
            else if (auto *l = dynamic_cast<const Loop *>(s))
            {
                collectStmt(l->init.get(), out);
                collectExpr(l->cond.get(), out);
                collectStmt(l->post.get(), out);
                collectBlock(l->body, out);
            }
            else if (auto *f = dynamic_cast<const ForIn *>(s))
            {
                collectExpr(f->iterable.get(), out);
                collectExpr(f->rangeEnd.get(), out);
                collectBlock(f->body, out);
            }
            else if (auto *sw = dynamic_cast<const Switch *>(s))
            {
                collectExpr(sw->tag.get(), out);
                for (const Case &c : sw->cases)
                {
                    for (const ExprPtr &v : c.values)
                        collectExpr(v.get(), out);
                    collectBlock(c.body, out);
                }
                collectBlock(sw->defaultBody, out);
            }
            else if (auto *r = dynamic_cast<const Return *>(s))
            {
                collectExpr(r->value.get(), out);
                for (const ExprPtr &x : r->rest)
                    collectExpr(x.get(), out);
            }
            else if (auto *d = dynamic_cast<const Defer *>(s))
                collectExpr(d->call.get(), out);
            else if (auto *m = dynamic_cast<const MultiVarDecl *>(s))
            {
                for (const ExprPtr &x : m->values)
                    collectExpr(x.get(), out);
            }
            else if (auto *m = dynamic_cast<const MultiAssign *>(s))
            {
                for (const ExprPtr &x : m->targets)
                    collectExpr(x.get(), out);
                for (const ExprPtr &x : m->values)
                    collectExpr(x.get(), out);
            }
        }

        void collectBlock(const Block &b, std::set<std::string> &out)
        {
            for (const StmtPtr &s : b)
                collectStmt(s.get(), out);
        }

        void collectExpr(const Expr *e, std::set<std::string> &out)
        {
            if (!e)
                return;
            if (auto *u = dynamic_cast<const Unary *>(e))
            {
                if (u->op == UnOp::AddrOf)
                {
                    const Expr *root = u->operand.get();
                    while (auto *sel = dynamic_cast<const Selector *>(root))
                        root = sel->base.get();
                    if (auto *ref = dynamic_cast<const VarRef *>(root))
                        out.insert(ref->name);
                }
                collectExpr(u->operand.get(), out);
            }
            else if (auto *b = dynamic_cast<const Binary *>(e))
            {
                collectExpr(b->lhs.get(), out);
                collectExpr(b->rhs.get(), out);
            }
            else if (auto *t = dynamic_cast<const Ternary *>(e))
            {
                collectExpr(t->cond.get(), out);
                collectExpr(t->thenExpr.get(), out);
                collectExpr(t->elseExpr.get(), out);
            }
            else if (auto *c = dynamic_cast<const Call *>(e))
            {
                collectExpr(c->callee.get(), out);
                for (const ExprPtr &a : c->args)
                    collectExpr(a.get(), out);
            }
            else if (auto *s = dynamic_cast<const Selector *>(e))
                collectExpr(s->base.get(), out);
            else if (auto *i = dynamic_cast<const Index *>(e))
            {
                collectExpr(i->base.get(), out);
                collectExpr(i->index.get(), out);
            }
            else if (auto *sl = dynamic_cast<const SliceExpr *>(e))
            {
                collectExpr(sl->base.get(), out);
                collectExpr(sl->lo.get(), out);
                collectExpr(sl->hi.get(), out);
            }
            else if (auto *a = dynamic_cast<const ArrayLit *>(e))
            {
                for (const ExprPtr &x : a->elems)
                    collectExpr(x.get(), out);
            }
            else if (auto *ml = dynamic_cast<const MapLit *>(e))
            {
                for (size_t i = 0; i < ml->keys.size(); ++i)
                {
                    collectExpr(ml->keys[i].get(), out);
                    collectExpr(ml->values[i].get(), out);
                }
            }
            else if (auto *sl = dynamic_cast<const StructLit *>(e))
            {
                for (const FieldInit &f : sl->fields)
                    collectExpr(f.value.get(), out);
            }
        }

        template <typename T>
        [[noreturn]] void failIn(const std::string &file, const std::string &msg, const T &at)
        {
            CompileError e(msg, at.line, at.col);
            e.file = file;
            throw e;
        }

    }

    CodeGen::CodeGen(const std::string &moduleName)
        : module_(std::make_unique<llvm::Module>(moduleName, ctx_)), builder_(ctx_)
    {
        arrayTy_ = llvm::StructType::create(
            ctx_, {builder_.getPtrTy(), builder_.getInt64Ty(), builder_.getInt64Ty(), builder_.getPtrTy()}, "pgt.array");
        ifaceTy_ =
            llvm::StructType::create(ctx_, {builder_.getPtrTy(), builder_.getPtrTy()}, "pgt.iface");
        fieldDescTy_ = llvm::StructType::create(
            ctx_, {builder_.getPtrTy(), builder_.getPtrTy(), builder_.getInt64Ty()}, "pgt.fielddesc");
        descTy_ = llvm::StructType::create(
            ctx_,
            {builder_.getInt32Ty(), builder_.getInt32Ty(), builder_.getPtrTy(), builder_.getPtrTy(),
             builder_.getInt64Ty(), builder_.getPtrTy(), builder_.getPtrTy(), builder_.getPtrTy()},
            "pgt.typedesc");
    }

    void CodeGen::fail(const std::string &msg, int line, int col) const
    {
        throw CompileError(msg, line, col);
    }

    llvm::Type *CodeGen::llvmType(TyP t)
    {
        switch (t->kind)
        {
        case Type::Kind::Void:
            return builder_.getVoidTy();
        case Type::Kind::Int:
            return builder_.getInt32Ty();
        case Type::Kind::Int64:
        case Type::Kind::Uint64:
            return builder_.getInt64Ty();
        case Type::Kind::Float:
            return builder_.getDoubleTy();
        case Type::Kind::Char:
        case Type::Kind::Byte:
            return builder_.getInt8Ty();
        case Type::Kind::Bool:
            return builder_.getInt1Ty();
        case Type::Kind::String:
        case Type::Kind::Nil:
        case Type::Kind::Pointer:
        case Type::Kind::Map:
        case Type::Kind::Func:
            return builder_.getPtrTy();
        case Type::Kind::Array:
            return arrayTy_;
        case Type::Kind::Interface:
        case Type::Kind::Any:
            return ifaceTy_;
        case Type::Kind::Struct:
            return t->st->llvm;
        case Type::Kind::Tuple:
        {
            std::vector<llvm::Type *> tys;
            for (TyP e : t->elems)
                tys.push_back(llvmType(e));
            return llvm::StructType::get(ctx_, tys);
        }
        }
        return nullptr;
    }

    llvm::Constant *CodeGen::sizeOf(TyP t)
    {
        return llvm::ConstantExpr::getPtrToInt(
            llvm::ConstantExpr::getGetElementPtr(
                llvmType(t), llvm::ConstantPointerNull::get(builder_.getPtrTy()),
                llvm::ArrayRef<llvm::Constant *>{builder_.getInt64(1)}),
            builder_.getInt64Ty());
    }

    TyP CodeGen::resolveType(const TypeExpr &type, bool allowVoid)
    {
        switch (type.kind)
        {
        case TypeExpr::Kind::Array:
            return types_.arrayOf(resolveType(*type.elem));
        case TypeExpr::Kind::Pointer:
            return types_.pointerTo(resolveType(*type.elem));
        case TypeExpr::Kind::Map:
        {
            TyP k = resolveType(*type.key);
            if (!k->isIntegral() && !k->is(Type::Kind::Bool) && !k->is(Type::Kind::String))
                fail("invalid map key type " + k->name() +
                         " (keys can be int, int64, char, byte, bool or string)",
                     type.key->line, type.key->col);
            return types_.mapOf(k, resolveType(*type.elem));
        }
        case TypeExpr::Kind::Tuple:
        {
            std::vector<TyP> elems;
            for (const TypeExpr &e : type.elems)
                elems.push_back(resolveType(e));
            return types_.tupleOf(elems);
        }
        case TypeExpr::Kind::Func:
        {
            std::vector<TyP> params;
            for (const TypeExpr &e : type.elems)
                params.push_back(resolveType(e));
            TyP ret = type.elem ? resolveType(*type.elem, true) : types_.voidT;
            return types_.funcOf(params, ret);
        }
        case TypeExpr::Kind::Named:
            break;
        }

        if (!type.qualifier.empty())
        {
            auto imp = fileImports_.find(type.qualifier);
            if (imp == fileImports_.end())
                fail("undefined package '" + type.qualifier + "'", type.line, type.col);
            PackageSyms *p = imp->second;
            if (!isPublic(type.name))
                fail("'" + type.text() + "' is not exported (lowercase names are private)", type.line,
                     type.col);
            if (auto it = p->structs.find(type.name); it != p->structs.end())
                return types_.structT(it->second);
            if (auto it = p->ifaces.find(type.name); it != p->ifaces.end())
                return types_.ifaceT(it->second);
            fail("package '" + type.qualifier + "' has no type '" + type.name + "'", type.line, type.col);
        }

        const std::string &n = type.name;
        TyP t = nullptr;
        if (n == "any")
            t = types_.anyT;
        else if (n == "int")
            t = types_.intT;
        else if (n == "int64")
            t = types_.int64T;
        else if (n == "uint64")
            t = types_.uint64T;
        else if (n == "float")
        {
            if (freestanding_ && !inStdFile())
                fail("'float' is not supported in freestanding builds", type.line, type.col);
            t = types_.floatT;
        }
        else if (n == "char")
            t = types_.charT;
        else if (n == "byte")
            t = types_.byteT;
        else if (n == "string")
            t = types_.stringT;
        else if (n == "bool")
            t = types_.boolT;
        else if (n == "void")
        {
            if (!allowVoid)
                fail("'void' can only be used as a result type", type.line, type.col);
            t = types_.voidT;
        }
        else if (auto it = pkg_->structs.find(n); it != pkg_->structs.end())
            t = types_.structT(it->second);
        else if (auto it = pkg_->ifaces.find(n); it != pkg_->ifaces.end())
            t = types_.ifaceT(it->second);
        if (!t)
            fail("unknown type '" + n + "'", type.line, type.col);
        return t;
    }

    llvm::Value *CodeGen::zeroValue(TyP t)
    {
        return llvm::Constant::getNullValue(llvmType(t));
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

    llvm::Value *CodeGen::newVarSlot(TyP type, const std::string &name)
    {
        if (heapVars_.count(name))
            return rt("pgt_alloc", builder_.getPtrTy(), {sizeOf(type), dropFn(type)});
        return entryAlloca(llvmType(type), name);
    }

    void CodeGen::pushScope()
    {
        scopes_.emplace_back();
    }

    void CodeGen::popScope()
    {
        emitScopeReleases(scopes_.size() - 1);
        scopes_.pop_back();
    }

    CodeGen::Var *CodeGen::lookupLocal(const std::string &name)
    {
        for (auto it = scopes_.rbegin(); it != scopes_.rend(); ++it)
        {
            auto found = it->vars.find(name);
            if (found != it->vars.end())
                return &found->second;
        }
        return nullptr;
    }

    std::optional<CodeGen::Var> CodeGen::lookupVar(const std::string &name)
    {
        if (Var *v = lookupLocal(name))
        {
            if (!v->indirect)
                return *v;
            llvm::Value *p = builder_.CreateLoad(builder_.getPtrTy(), v->addr);
            nilCheck(p);
            Var direct = *v;
            direct.addr = p;
            direct.indirect = false;
            return direct;
        }
        auto g = pkg_->globals.find(name);
        if (g != pkg_->globals.end())
            return Var{g->second.gv, g->second.type, g->second.isConst};
        return std::nullopt;
    }

    CodeGen::PackageSyms *CodeGen::lookupPackage(const std::string &name)
    {
        auto it = fileImports_.find(name);
        return it == fileImports_.end() ? nullptr : it->second;
    }

    void CodeGen::declare(const std::string &name, Var var, int line, int col)
    {
        if (scopes_.back().vars.count(name))
            fail("variable '" + name + "' is already declared in this scope", line, col);
        scopes_.back().vars[name] = var;
        if (var.isManual)
            return;
        bool boxed = heapVars_.count(name) > 0;
        if (boxed || isManaged(var.type))
            scopes_.back().owned.push_back({var.addr, var.type, boxed});
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
                failIn(p.file, "only package 'main' may end with 'return'", *p.exitCode);
            if (withExit)
                failIn(p.file, "package 'main' has more than one final 'return'", *p.exitCode);
            withExit = &p;
        }
        if (pkg.name == "main" && !withExit && !freestanding_)
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
                syms_[pkg->importPath].pkg = pkg.get();
            }
            for (const auto &pkg : workspace.packages)
                declareTypes(*pkg);
            for (const auto &pkg : workspace.packages)
                resolveTypeBodies(*pkg);
            checkStructCycles();
            for (const auto &pkg : workspace.packages)
                declareFunctions(*pkg);
            for (const char *name : {"panicString", "panicIndex", "panicSlice", "panicSliceCap", "panicNil", "panicDiv"})
                runtimeFn(name).fn->addFnAttr(llvm::Attribute::NoReturn);
            runtimeFn("panicString").fn->setLinkage(llvm::Function::ExternalLinkage);
            for (const auto &pkg : workspace.packages)
                genGlobals(*pkg);

            for (const auto &pkg : workspace.packages)
            {
                pkg_ = &syms_.at(pkg->importPath);
                for (const Program &p : pkg->programs)
                {
                    currentFile_ = p.file;
                    bindImports(p);
                    genTopBlocks(p);
                    for (const Function &fn : p.functions)
                    {
                        if (fn.isExtern)
                            continue;
                        if (fn.receiver)
                        {
                            StructInfo *st = pkg_->structs.at(fn.receiver->typeName);
                            genFunction(fn, st->methods.at(fn.name));
                        }
                        else
                        {
                            genFunction(fn, pkg_->functions.at(fn.name));
                        }
                    }
                }
            }

            const Package &root = *workspace.root;
            pkg_ = &syms_.at(root.importPath);
            if (root.name == "main" && freestanding_)
                genKernelEntry();
            else if (root.name == "main")
            {
                for (const Program &p : root.programs)
                {
                    if (!p.exitCode)
                        continue;
                    currentFile_ = p.file;
                    bindImports(p);
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

        finishAssertFns();

        std::string err;
        llvm::raw_string_ostream os(err);
        if (llvm::verifyModule(*module_, &os))
            throw CompileError("internal error: generated invalid LLVM IR:\n" + err, 0, 0);
    }

    void CodeGen::bindImports(const Program &program)
    {
        fileImports_.clear();
        for (const Import &imp : program.imports)
        {
            const Package *target = ws_->byPath.at(imp.path);
            if (fileImports_.count(target->name))
                fail("package name '" + target->name + "' is imported more than once", imp.line,
                     imp.col);
            fileImports_[target->name] = &syms_.at(target->importPath);
        }
    }

    void CodeGen::declareTypes(const Package &pkg)
    {
        PackageSyms &ps = syms_.at(pkg.importPath);
        auto taken = [&](const std::string &name)
        { return ps.structs.count(name) || ps.ifaces.count(name); };

        for (const Program &p : pkg.programs)
        {
            currentFile_ = p.file;
            for (const StructDecl &d : p.structs)
            {
                if (taken(d.name))
                    fail("type '" + d.name + "' is already defined in package '" + pkg.name + "'",
                         d.line, d.col);
                StructInfo &st = structStore_.emplace_back();
                st.name = d.name;
                st.pkgPath = pkg.importPath;
                st.pkgName = pkg.name;
                st.decl = &d;
                st.program = &p;
                st.llvm = llvm::StructType::create(ctx_, mangle(pkg.importPath, d.name));
                ps.structs[d.name] = &st;
            }
            for (const InterfaceDecl &d : p.interfaces)
            {
                if (taken(d.name))
                    fail("type '" + d.name + "' is already defined in package '" + pkg.name + "'",
                         d.line, d.col);
                IfaceInfo &ii = ifaceStore_.emplace_back();
                ii.name = d.name;
                ii.pkgPath = pkg.importPath;
                ii.pkgName = pkg.name;
                ii.decl = &d;
                ii.program = &p;
                ps.ifaces[d.name] = &ii;
            }
        }
    }

    void CodeGen::resolveTypeBodies(const Package &pkg)
    {
        pkg_ = &syms_.at(pkg.importPath);
        for (const Program &p : pkg.programs)
        {
            currentFile_ = p.file;
            bindImports(p);
            for (const StructDecl &d : p.structs)
            {
                StructInfo *st = pkg_->structs.at(d.name);
                std::vector<llvm::Type *> body;
                for (const Field &f : d.fields)
                {
                    if (st->fieldIndex(f.name) >= 0)
                        fail("duplicate field '" + f.name + "' in struct '" + d.name + "'", f.line,
                             f.col);
                    TyP t = resolveType(f.type);
                    st->fields.push_back({f.name, t});
                    body.push_back(llvmType(t));
                }
                st->llvm->setBody(body);
            }
            for (const InterfaceDecl &d : p.interfaces)
            {
                IfaceInfo *ii = pkg_->ifaces.at(d.name);
                for (const MethodSig &m : d.methods)
                {
                    if (ii->methodIndex(m.name) >= 0)
                        fail("duplicate method '" + m.name + "' in interface '" + d.name + "'", m.line,
                             m.col);
                    IfaceMethod im;
                    im.name = m.name;
                    for (const Param &prm : m.params)
                        im.params.push_back(resolveType(prm.type));
                    im.ret = m.ret ? resolveType(*m.ret, true) : types_.voidT;
                    ii->methods.push_back(std::move(im));
                }
            }
        }
    }

    void CodeGen::checkStructCycles()
    {
        std::map<StructInfo *, int> state;
        std::function<void(StructInfo *)> visit = [&](StructInfo *st)
        {
            int &s = state[st];
            if (s == 2)
                return;
            if (s == 1)
                failIn(st->program->file,
                       "struct '" + st->name +
                           "' contains itself; use a pointer (*" + st->name + ") instead",
                       *st->decl);
            s = 1;
            for (const FieldInfo &f : st->fields)
                if (f.type->is(Type::Kind::Struct))
                    visit(f.type->st);
            state[st] = 2;
        };
        for (StructInfo &st : structStore_)
            visit(&st);
    }

    FuncInfo CodeGen::declareSignature(const Function &fn, TyP recvType, const std::string &symbol)
    {
        FuncInfo info;
        info.name = fn.name;
        info.isExtern = fn.isExtern;
        info.line = fn.line;
        info.col = fn.col;
        std::vector<llvm::Type *> llvmParams;
        bool intrinsic = fn.isExtern && symbol.rfind("pgt_i_", 0) == 0;
        if (intrinsic)
        {
            std::string rest = symbol.substr(6);
            info.intrinsic = rest.substr(0, rest.find('_'));
        }
        if (recvType)
        {
            info.params.push_back(recvType);
            llvmParams.push_back(llvmType(recvType));
        }
        for (const Param &param : fn.params)
        {
            TyP t = resolveType(param.type);
            if (fn.isExtern && !intrinsic && (t->is(Type::Kind::Struct) || t->is(Type::Kind::Interface) ||
                                t->is(Type::Kind::Any)))
                fail("external functions cannot take struct or interface parameters", param.type.line,
                     param.type.col);
            info.params.push_back(t);
            llvmParams.push_back(fn.isExtern && t->is(Type::Kind::Array) ? builder_.getPtrTy()
                                                                         : llvmType(t));
        }
        info.ret = fn.ret ? resolveType(*fn.ret, true) : types_.voidT;
        if (fn.isExtern && info.ret->is(Type::Kind::Tuple))
            fail("external functions cannot return multiple values", fn.line, fn.col);
        info.variadic = !fn.params.empty() && fn.params.back().variadic;
        if (fn.isExtern && !intrinsic && (info.ret->is(Type::Kind::Struct) || info.ret->is(Type::Kind::Interface) ||
                            info.ret->is(Type::Kind::Any) || info.ret->is(Type::Kind::Array)))
            fail("external functions can only return scalars, strings and pointers", fn.line, fn.col);

        if (intrinsic)
            return info;
        auto *fnTy = llvm::FunctionType::get(llvmType(info.ret), llvmParams, false);
        if (fn.isExtern)
        {
            if (llvm::Function *existing = module_->getFunction(symbol))
            {
                if (existing->getFunctionType() != fnTy)
                    fail("conflicting declarations of the external function '" + symbol + "'", fn.line,
                         fn.col);
                info.fn = existing;
            }
            else
            {
                info.fn = llvm::Function::Create(fnTy, llvm::Function::ExternalLinkage, symbol,
                                                 module_.get());
                for (unsigned i = 0; i < llvmParams.size(); ++i)
                    if (llvmParams[i]->isIntegerTy(1) || llvmParams[i]->isIntegerTy(8))
                        info.fn->addParamAttr(i, llvm::Attribute::ZExt);
                if (info.ret->is(Type::Kind::Bool) || info.ret->is(Type::Kind::Char) ||
                    info.ret->is(Type::Kind::Byte))
                    info.fn->addRetAttr(llvm::Attribute::ZExt);
            }
            return info;
        }
        bool exported = isPublic(fn.name) && (!recvType || isPublic(fn.receiver->typeName));
        info.fn = llvm::Function::Create(
            fnTy, exported ? llvm::Function::ExternalLinkage : llvm::Function::InternalLinkage, symbol,
            module_.get());
        return info;
    }

    void CodeGen::declareFunctions(const Package &pkg)
    {
        pkg_ = &syms_.at(pkg.importPath);
        auto &table = pkg_->functions;
        for (const Program &p : pkg.programs)
        {
            currentFile_ = p.file;
            bindImports(p);
            for (const Function &fn : p.functions)
            {
                if (fn.receiver)
                {
                    const Receiver &r = *fn.receiver;
                    auto it = pkg_->structs.find(r.typeName);
                    if (it == pkg_->structs.end())
                        fail(pkg_->ifaces.count(r.typeName)
                                 ? "methods cannot be declared on interface '" + r.typeName + "'"
                                 : "methods can only be declared on a struct of this package; '" +
                                       r.typeName + "' is not one",
                             r.line, r.col);
                    StructInfo *st = it->second;
                    if (st->methods.count(fn.name))
                        fail("method '" + r.typeName + "." + fn.name + "' is already defined", fn.line,
                             fn.col);
                    if (st->fieldIndex(fn.name) >= 0)
                        fail("struct '" + r.typeName + "' has both a field and a method named '" +
                                 fn.name + "'",
                             fn.line, fn.col);
                    TyP structTy = types_.structT(st);
                    TyP recvTy = r.pointer ? types_.pointerTo(structTy) : structTy;
                    FuncInfo info =
                        declareSignature(fn, recvTy, mangle(pkg.importPath, r.typeName + "." + fn.name));
                    info.ptrRecv = r.pointer;
                    st->methods[fn.name] = info;
                    continue;
                }

                if (table.count(fn.name))
                    fail("function '" + fn.name + "' is already defined in package '" + pkg.name + "'",
                         fn.line, fn.col);
                if (pkg_->structs.count(fn.name) || pkg_->ifaces.count(fn.name))
                    fail("'" + fn.name + "' is already declared as a type", fn.line, fn.col);
                if (fn.isExtern && fn.name == "main")
                    fail("an external function cannot be called 'main'", fn.line, fn.col);
                std::string symbol = fn.isExtern ? fn.name : mangle(pkg.importPath, fn.name);
                table[fn.name] = declareSignature(fn, nullptr, symbol);
            }
        }
    }

    void CodeGen::genGlobals(const Package &pkg)
    {
        pkg_ = &syms_.at(pkg.importPath);
        auto *initTy = llvm::FunctionType::get(builder_.getVoidTy(), false);
        pkg_->init = llvm::Function::Create(initTy, llvm::Function::InternalLinkage,
                                            mangle(pkg.importPath, "init"), module_.get());
        builder_.SetInsertPoint(llvm::BasicBlock::Create(ctx_, "entry", pkg_->init));
        currentRet_ = types_.voidT;
        heapVars_.clear();
        loops_.clear();

        for (const Program &p : pkg.programs)
        {
            currentFile_ = p.file;
            bindImports(p);
            for (const auto &decl : p.globals)
            {
                const VarDecl &d = *decl;
                if (pkg_->globals.count(d.name))
                    fail("'" + d.name + "' is already declared in package '" + pkg.name + "'", d.line,
                         d.col);
                if (pkg_->functions.count(d.name) || pkg_->structs.count(d.name) ||
                    pkg_->ifaces.count(d.name))
                    fail("'" + d.name + "' is already declared in package '" + pkg.name + "'", d.line,
                         d.col);

                scopes_.clear();
                pushScope();
                TyP ty = d.type ? resolveType(*d.type) : nullptr;
                Value init;
                std::optional<Wide> folded;
                if (d.isConst && !d.type && d.init)
                    if (auto cv = constValue(*d.init); cv && *cv >= INT64_MIN && *cv <= static_cast<Wide>(UINT64_MAX))
                        folded = cv;
                if (folded)
                {
                    ty = *folded >= INT_MIN && *folded <= INT_MAX ? types_.intT
                         : *folded <= INT64_MAX                   ? types_.int64T
                                                                  : types_.uint64T;
                    init = untypedConst(*folded, ty, d.line, d.col);
                }
                else if (d.init)
                {
                    init = genExpr(*d.init, ty);
                    if (ty)
                        init = coerce(init, ty, d.line, d.col, "the initializer of '" + d.name + "'");
                    else
                        ty = init.type;
                }
                if (ty->is(Type::Kind::Nil))
                    fail("cannot infer the type of '" + d.name + "' from nil", d.line, d.col);
                if (ty->is(Type::Kind::Void))
                    fail("'" + d.name + "' cannot be initialized with a call that returns nothing",
                         d.line, d.col);
                llvm::Value *initValue = init.value ? take(init) : nullptr;
                flushTemps();
                popScope();

                auto *gv = new llvm::GlobalVariable(
                    *module_, llvmType(ty), false,
                    isPublic(d.name) ? llvm::GlobalValue::ExternalLinkage
                                     : llvm::GlobalValue::InternalLinkage,
                    llvm::Constant::getNullValue(llvmType(ty)), mangle(pkg.importPath, d.name));
                if (initValue)
                    builder_.CreateStore(initValue, gv);
                pkg_->globals[d.name] = Global{gv, ty, d.isConst, folded.has_value(), folded.value_or(0)};
            }
        }
        builder_.CreateRetVoid();
        scopes_.clear();
    }

    void CodeGen::genFunction(const Function &fn, const FuncInfo &info)
    {
        std::string fullName = fn.receiver ? fn.receiver->typeName + "." + fn.name : fn.name;
        genBody(fn.params, fn.receiver ? &*fn.receiver : nullptr, fn.body, fullName, fn.line, fn.col, info);
    }

    void CodeGen::genBody(const std::vector<Param> &params, const Receiver *receiver, const Block &body,
                          const std::string &fullName, int line, int col, const FuncInfo &info)
    {
        builder_.SetInsertPoint(llvm::BasicBlock::Create(ctx_, "entry", info.fn));
        scopes_.clear();
        temps_.clear();
        loops_.clear();
        heapVars_.clear();
        collectBlock(body, heapVars_);
        currentRet_ = info.ret;

        pushScope();
        std::vector<std::pair<std::string, std::pair<int, int>>> names;
        if (receiver)
            names.push_back({receiver->name, {receiver->line, receiver->col}});
        for (const Param &p : params)
            names.push_back({p.name, {p.line, p.col}});

        size_t i = 0;
        for (llvm::Argument &arg : info.fn->args())
        {
            const auto &[name, pos] = names[i];
            arg.setName(name);
            llvm::Value *slot = newVarSlot(info.params[i], name);
            builder_.CreateStore(&arg, slot);
            emitRetain(&arg, info.params[i]);
            declare(name, Var{slot, info.params[i], false}, pos.first, pos.second);
            ++i;
        }

        if (!info.ret->is(Type::Kind::Void) && !terminates(body))
            fail("missing return at the end of function '" + fullName + "'", line, col);

        defers_.clear();
        overrides_.clear();
        pushScope();
        for (const StmtPtr &stmt : body)
            genStmt(*stmt);
        if (info.ret->is(Type::Kind::Void))
            emitDefers();
        popScope();
        popScope();
        temps_.clear();

        if (info.ret->is(Type::Kind::Void))
            builder_.CreateRetVoid();
        else
            builder_.CreateUnreachable();
    }

    CodeGen::Value CodeGen::genFuncLit(const FuncLit &lit)
    {
        FuncInfo info;
        info.name = "func literal";
        info.line = lit.line;
        info.col = lit.col;
        std::vector<llvm::Type *> llvmParams;
        for (const Param &param : lit.params)
        {
            TyP t = resolveType(param.type);
            info.params.push_back(t);
            llvmParams.push_back(llvmType(t));
        }
        info.ret = lit.ret ? resolveType(*lit.ret, true) : types_.voidT;
        auto *fnTy = llvm::FunctionType::get(llvmType(info.ret), llvmParams, false);
        info.fn = llvm::Function::Create(fnTy, llvm::Function::InternalLinkage,
                                         mangle(pkg_->pkg->importPath, "func" + std::to_string(litCounter_++)),
                                         module_.get());

        llvm::IRBuilderBase::InsertPointGuard guard(builder_);
        auto savedScopes = std::move(scopes_);
        auto savedTemps = std::move(temps_);
        auto savedLoops = std::move(loops_);
        auto savedDefers = std::move(defers_);
        auto savedOverrides = std::move(overrides_);
        auto savedHeap = std::move(heapVars_);
        TyP savedRet = currentRet_;
        MapTarget savedMap = mapTarget_;
        mapTarget_ = MapTarget{};
        scopes_.clear();
        temps_.clear();
        loops_.clear();
        defers_.clear();
        overrides_.clear();
        heapVars_.clear();

        auto outerHas = [&](const std::string &name)
        {
            for (const Scope &sc : savedScopes)
                if (sc.vars.count(name))
                    return true;
            return false;
        };
        try
        {
            genBody(lit.params, nullptr, lit.body, "func literal", lit.line, lit.col, info);
        }
        catch (const CompileError &e)
        {
            std::string msg = e.what();
            for (const char *prefix : {"undefined variable '", "call to undefined function '"})
            {
                std::string p = prefix;
                if (msg.compare(0, p.size(), p) == 0)
                {
                    size_t end = msg.find('\'', p.size());
                    std::string name = msg.substr(p.size(), end - p.size());
                    if (outerHas(name))
                        throw CompileError("function literal cannot capture '" + name + "' yet", e.line, e.col);
                }
            }
            throw;
        }

        scopes_ = std::move(savedScopes);
        temps_ = std::move(savedTemps);
        loops_ = std::move(savedLoops);
        defers_ = std::move(savedDefers);
        overrides_ = std::move(savedOverrides);
        heapVars_ = std::move(savedHeap);
        currentRet_ = savedRet;
        mapTarget_ = savedMap;
        return {info.fn, types_.funcOf(info.params, info.ret)};
    }

    bool CodeGen::inStdFile() const
    {
        static std::string prefix = [] {
            auto d = findStdDir();
            std::error_code ec;
            return d ? std::filesystem::weakly_canonical(*d, ec).string() + "/" : std::string("\1");
        }();
        std::error_code ec;
        return std::filesystem::weakly_canonical(currentFile_, ec).string().rfind(prefix, 0) == 0;
    }

    void CodeGen::genKernelEntry()
    {
        const auto &table = pkg_->functions;
        auto it = table.find(entryName_);
        if (it == table.end() || it->second.isExtern)
            throw CompileError("entry function '" + entryName_ + "' is not defined in package main", 1, 1);
        if (!it->second.params.empty() || !it->second.ret->is(Type::Kind::Void))
            fail("the entry function '" + entryName_ + "' must take no parameters and return nothing",
                 it->second.line, it->second.col);
        auto *startTy = llvm::FunctionType::get(builder_.getVoidTy(), false);
        auto *start = llvm::Function::Create(startTy, llvm::Function::ExternalLinkage, "_start", module_.get());
        builder_.SetInsertPoint(llvm::BasicBlock::Create(ctx_, "entry", start));
        for (const auto &pkg : ws_->packages)
            builder_.CreateCall(syms_.at(pkg->importPath).init);
        builder_.CreateCall(it->second.fn);
        rt("pgt_exit", builder_.getVoidTy(), {builder_.getInt32(0)});
        builder_.CreateUnreachable();
    }

    void CodeGen::genEntryPoint(const Package &root, const Expr &exitCode)
    {
        const auto &table = pkg_->functions;
        auto it = table.find("main");
        if (it == table.end() || it->second.isExtern)
            fail("function 'main' is not defined", 1, 1);
        if (!it->second.params.empty())
            fail("function 'main' must not take parameters", it->second.line, it->second.col);
        (void)root;

        auto *mainTy = llvm::FunctionType::get(builder_.getInt32Ty(),
                                               {builder_.getInt32Ty(), builder_.getPtrTy()}, false);
        auto *mainFn =
            llvm::Function::Create(mainTy, llvm::Function::ExternalLinkage, "main", module_.get());
        builder_.SetInsertPoint(llvm::BasicBlock::Create(ctx_, "entry", mainFn));
        scopes_.clear();
        pushScope();
        loops_.clear();
        heapVars_.clear();
        currentRet_ = types_.intT;

        rt("pgt_rt_init", builder_.getVoidTy(), {mainFn->getArg(0), mainFn->getArg(1)});
        for (const auto &pkg : ws_->packages)
            builder_.CreateCall(syms_.at(pkg->importPath).init);
        builder_.CreateCall(it->second.fn);

        Value code = genExpr(exitCode, types_.intT);
        if (!code.type->is(Type::Kind::Int))
            fail("the final 'return' must be an integer", exitCode.line, exitCode.col);
        flushTemps();
        rt("fflush", builder_.getInt32Ty(), {llvm::ConstantPointerNull::get(builder_.getPtrTy())});
        builder_.CreateRet(code.value);
    }

}

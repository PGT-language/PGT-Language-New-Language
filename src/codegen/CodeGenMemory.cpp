#include "codegen/CodeGen.h"

namespace pgt
{

    bool CodeGen::isManaged(TyP t)
    {
        switch (t->kind)
        {
        case Type::Kind::String:
        case Type::Kind::Array:
        case Type::Kind::Pointer:
        case Type::Kind::Interface:
            return true;
        case Type::Kind::Struct:
        {
            if (auto it = managed_.find(t); it != managed_.end())
                return it->second;
            bool any = false;
            for (const FieldInfo &f : t->st->fields)
                any = any || isManaged(f.type);
            managed_[t] = any;
            return any;
        }
        default:
            return false;
        }
    }

    namespace
    {
        llvm::Value *blockPtr(llvm::IRBuilder<> &b, llvm::Value *v, const Type &t)
        {
            if (t.kind == Type::Kind::Array || t.kind == Type::Kind::Interface)
                return b.CreateExtractValue(v, 0);
            return v;
        }
    }

    void CodeGen::emitRetain(llvm::Value *v, TyP t)
    {
        if (!isManaged(t) || llvm::isa<llvm::Constant>(v))
            return;
        if (t->is(Type::Kind::Struct))
        {
            builder_.CreateCall(structHelper(t, true), {v});
            return;
        }
        rt("pgt_retain", builder_.getVoidTy(), {blockPtr(builder_, v, *t)});
    }

    void CodeGen::emitRelease(llvm::Value *v, TyP t)
    {
        if (!isManaged(t) || llvm::isa<llvm::Constant>(v))
            return;
        if (t->is(Type::Kind::Struct))
        {
            builder_.CreateCall(structHelper(t, false), {v});
            return;
        }
        rt("pgt_release", builder_.getVoidTy(), {blockPtr(builder_, v, *t)});
    }

    llvm::Function *CodeGen::structHelper(TyP t, bool retain)
    {
        auto key = std::make_pair(t, retain ? 1 : 0);
        if (auto it = memFns_.find(key); it != memFns_.end())
            return it->second;
        auto *fnTy = llvm::FunctionType::get(builder_.getVoidTy(), {llvmType(t)}, false);
        auto *fn = llvm::Function::Create(fnTy, llvm::Function::InternalLinkage,
                                          std::string(retain ? "pgt.retain." : "pgt.release.") + t->name(),
                                          module_.get());
        memFns_[key] = fn;

        llvm::IRBuilderBase::InsertPointGuard guard(builder_);
        builder_.SetInsertPoint(llvm::BasicBlock::Create(ctx_, "entry", fn));
        const auto &fields = t->st->fields;
        for (size_t i = 0; i < fields.size(); ++i)
        {
            if (!isManaged(fields[i].type))
                continue;
            llvm::Value *f = builder_.CreateExtractValue(fn->getArg(0), static_cast<unsigned>(i));
            if (retain)
                emitRetain(f, fields[i].type);
            else
                emitRelease(f, fields[i].type);
        }
        builder_.CreateRetVoid();
        return fn;
    }

    llvm::Constant *CodeGen::dropFn(TyP t)
    {
        if (!isManaged(t))
            return llvm::ConstantPointerNull::get(builder_.getPtrTy());
        auto key = std::make_pair(t, 2);
        if (auto it = memFns_.find(key); it != memFns_.end())
            return it->second;
        auto *fnTy = llvm::FunctionType::get(builder_.getVoidTy(), {builder_.getPtrTy()}, false);
        auto *fn = llvm::Function::Create(fnTy, llvm::Function::InternalLinkage,
                                          "pgt.drop." + t->name(), module_.get());
        memFns_[key] = fn;
        llvm::IRBuilderBase::InsertPointGuard guard(builder_);
        builder_.SetInsertPoint(llvm::BasicBlock::Create(ctx_, "entry", fn));
        emitRelease(builder_.CreateLoad(llvmType(t), fn->getArg(0)), t);
        builder_.CreateRetVoid();
        return fn;
    }

    llvm::Constant *CodeGen::retainFn(TyP t)
    {
        if (!isManaged(t))
            return llvm::ConstantPointerNull::get(builder_.getPtrTy());
        auto key = std::make_pair(t, 3);
        if (auto it = memFns_.find(key); it != memFns_.end())
            return it->second;
        auto *fnTy = llvm::FunctionType::get(builder_.getVoidTy(), {builder_.getPtrTy()}, false);
        auto *fn = llvm::Function::Create(fnTy, llvm::Function::InternalLinkage,
                                          "pgt.retainat." + t->name(), module_.get());
        memFns_[key] = fn;
        llvm::IRBuilderBase::InsertPointGuard guard(builder_);
        builder_.SetInsertPoint(llvm::BasicBlock::Create(ctx_, "entry", fn));
        emitRetain(builder_.CreateLoad(llvmType(t), fn->getArg(0)), t);
        builder_.CreateRetVoid();
        return fn;
    }

    CodeGen::Value CodeGen::owned(Value v)
    {
        if (!v.value || !isManaged(v.type) || llvm::isa<llvm::Constant>(v.value))
            return v;
        v.owned = true;
        temps_.push_back({v.value, v.type});
        return v;
    }

    llvm::Value *CodeGen::take(const Value &v)
    {
        if (!v.value || !isManaged(v.type))
            return v.value;
        if (v.owned)
        {
            for (auto it = temps_.rbegin(); it != temps_.rend(); ++it)
                if (it->value == v.value)
                {
                    temps_.erase(std::next(it).base());
                    return v.value;
                }
        }
        emitRetain(v.value, v.type);
        return v.value;
    }

    void CodeGen::flushTemps(size_t downTo)
    {
        while (temps_.size() > downTo)
        {
            Temp t = temps_.back();
            temps_.pop_back();
            emitRelease(t.value, t.type);
        }
    }

    void CodeGen::storeValue(llvm::Value *addr, TyP t, Value v, bool initial)
    {
        llvm::Value *nv = take(v);
        if (!isManaged(t) || initial)
        {
            builder_.CreateStore(nv, addr);
            return;
        }
        llvm::Value *old = builder_.CreateLoad(llvmType(t), addr);
        builder_.CreateStore(nv, addr);
        emitRelease(old, t);
    }

    void CodeGen::holdValue(Value v)
    {
        if (!isManaged(v.type))
            return;
        llvm::Value *nv = take(v);
        llvm::AllocaInst *slot = entryAlloca(llvmType(v.type), "hold");
        builder_.CreateStore(nv, slot);
        scopes_.back().owned.push_back({slot, v.type, false});
    }

    void CodeGen::emitScopeReleases(size_t downTo)
    {
        for (size_t i = scopes_.size(); i-- > downTo;)
        {
            const auto &owned = scopes_[i].owned;
            for (auto it = owned.rbegin(); it != owned.rend(); ++it)
            {
                if (it->boxed)
                    rt("pgt_release", builder_.getVoidTy(), {it->slot});
                else
                    emitRelease(builder_.CreateLoad(llvmType(it->type), it->slot), it->type);
            }
        }
    }

    bool CodeGen::isManualElement(const Expr &target)
    {
        const Expr *e = &target;
        bool indexed = false;
        for (;;)
        {
            if (auto *idx = dynamic_cast<const Index *>(e))
            {
                indexed = true;
                e = idx->base.get();
            }
            else if (auto *sel = dynamic_cast<const Selector *>(e))
                e = sel->base.get();
            else
                break;
        }
        if (auto *ref = dynamic_cast<const VarRef *>(e))
            if (Var *v = lookupLocal(ref->name))
                return v->isManual && (indexed || v->indirect);
        return false;
    }

}

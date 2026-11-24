#include "codegen/CodeGen.h"

namespace pgt
{

    const FuncInfo &CodeGen::mapsFn(const std::string &name)
    {
        return syms_.at("maps").functions.at(name);
    }

    llvm::Value *CodeGen::mapDropFn()
    {
        if (!mapDrop_)
        {
            auto *fnTy = llvm::FunctionType::get(builder_.getVoidTy(), {builder_.getPtrTy()}, false);
            mapDrop_ = llvm::Function::Create(fnTy, llvm::Function::InternalLinkage, "pgt.mapdrop",
                                              module_.get());
            llvm::IRBuilderBase::InsertPointGuard guard(builder_);
            builder_.SetInsertPoint(llvm::BasicBlock::Create(ctx_, "entry", mapDrop_));
            rt("pgt_retain", builder_.getVoidTy(), {mapDrop_->getArg(0)});
            builder_.CreateCall(mapsFn("Drop").fn, {mapDrop_->getArg(0)});
            builder_.CreateRetVoid();
        }
        return mapDrop_;
    }

    llvm::Value *CodeGen::mapKeySlot(TyP mapType, const Expr &key)
    {
        Value k = coerce(genExpr(key, mapType->key), mapType->key, key.line, key.col, "the map key");
        llvm::Value *slot = entryAlloca(llvmType(mapType->key), "map.key");
        builder_.CreateStore(k.value, slot);
        return slot;
    }

    CodeGen::Value CodeGen::genMapNew(TyP mapType)
    {
        llvm::Value *m = builder_.CreateCall(
            mapsFn("New").fn, {typeDesc(mapType->key), typeDesc(mapType->elem), mapDropFn()});
        return owned(Value{m, mapType});
    }

    CodeGen::Value CodeGen::genMapLit(const MapLit &lit)
    {
        TyP t = resolveType(lit.type);
        if (!t->is(Type::Kind::Map))
            fail("'" + lit.type.text() + "' is not a map type", lit.line, lit.col);
        Value m = genMapNew(t);
        for (size_t i = 0; i < lit.keys.size(); ++i)
        {
            llvm::Value *ks = mapKeySlot(t, *lit.keys[i]);
            const Expr &ve = *lit.values[i];
            Value v = coerce(genExpr(ve, t->elem), t->elem, ve.line, ve.col, "the map value");
            llvm::Value *vs = entryAlloca(llvmType(t->elem), "map.val");
            builder_.CreateStore(v.value, vs);
            builder_.CreateCall(mapsFn("Set").fn, {m.value, ks, vs});
        }
        return m;
    }

    CodeGen::Addr CodeGen::genMapAddr(Value map, const Expr &key, bool forWrite)
    {
        TyP vt = map.type->elem;
        llvm::Value *ks = mapKeySlot(map.type, key);
        llvm::Value *vs = entryAlloca(llvmType(vt), "map.val");
        builder_.CreateStore(zeroValue(vt), vs);
        builder_.CreateCall(mapsFn("Get").fn, {map.value, ks, vs});
        llvm::Value *cur = builder_.CreateLoad(llvmType(vt), vs);
        emitRetain(cur, vt);
        if (forWrite)
            mapTarget_ = MapTarget{true, map.value, ks, vs, vt};
        else if (isManaged(vt))
            temps_.push_back({cur, vt});
        return Addr{vs, vt, false};
    }

    void CodeGen::commitMapTarget(const MapTarget &mt)
    {
        if (!mt.active)
            return;
        builder_.CreateCall(mapsFn("Set").fn, {mt.map, mt.key, mt.val});
        emitRelease(builder_.CreateLoad(llvmType(mt.valType), mt.val), mt.valType);
    }

    CodeGen::Value CodeGen::genMapGet(Value map, const Expr &key)
    {
        TyP vt = map.type->elem;
        llvm::Value *ks = mapKeySlot(map.type, key);
        llvm::Value *vs = entryAlloca(llvmType(vt), "map.val");
        builder_.CreateStore(zeroValue(vt), vs);
        builder_.CreateCall(mapsFn("Get").fn, {map.value, ks, vs});
        llvm::Value *v = builder_.CreateLoad(llvmType(vt), vs);
        emitRetain(v, vt);
        return owned(Value{v, vt});
    }

    void CodeGen::genMapForIn(const ForIn &loop, Value seq)
    {
        llvm::Function *f = builder_.GetInsertBlock()->getParent();
        auto *condBB = llvm::BasicBlock::Create(ctx_, "mfor.cond", f);
        auto *bodyBB = llvm::BasicBlock::Create(ctx_, "mfor.body", f);
        auto *postBB = llvm::BasicBlock::Create(ctx_, "mfor.post", f);
        auto *endBB = llvm::BasicBlock::Create(ctx_, "mfor.end", f);
        TyP kt = seq.type->key;
        TyP vt = seq.type->elem;
        auto *i64 = builder_.getInt64Ty();

        auto *idx = entryAlloca(i64, "mfor.i");
        builder_.CreateStore(builder_.getInt64(0), idx);
        std::string keyName = loop.key.empty() ? loop.value : loop.key;
        llvm::Value *keySlot = newVarSlot(kt, keyName);
        builder_.CreateStore(zeroValue(kt), keySlot);
        declare(keyName, Var{keySlot, kt, false}, loop.line, loop.col);
        llvm::Value *valSlot = nullptr;
        if (!loop.key.empty())
        {
            valSlot = newVarSlot(vt, loop.value);
            builder_.CreateStore(zeroValue(vt), valSlot);
            declare(loop.value, Var{valSlot, vt, false}, loop.line, loop.col);
        }
        builder_.CreateBr(condBB);

        builder_.SetInsertPoint(condBB);
        llvm::Value *n = builder_.CreateCall(mapsFn("Next").fn, {seq.value, builder_.CreateLoad(i64, idx)});
        builder_.CreateStore(n, idx);
        builder_.CreateCondBr(builder_.CreateICmpSGE(n, builder_.getInt64(0)), bodyBB, endBB);

        builder_.SetInsertPoint(bodyBB);
        llvm::Value *at = builder_.CreateLoad(i64, idx);
        llvm::Value *kp = builder_.CreateCall(mapsFn("KeyAt").fn, {seq.value, at});
        storeValue(keySlot, kt, Value{builder_.CreateLoad(llvmType(kt), kp), kt}, false);
        if (valSlot)
        {
            llvm::Value *vp = builder_.CreateCall(mapsFn("ValAt").fn, {seq.value, at});
            storeValue(valSlot, vt, Value{builder_.CreateLoad(llvmType(vt), vp), vt}, false);
        }
        loops_.push_back({postBB, endBB, scopes_.size()});
        genBlock(loop.body);
        loops_.pop_back();
        builder_.CreateBr(postBB);

        builder_.SetInsertPoint(postBB);
        llvm::Value *cur = builder_.CreateLoad(i64, idx);
        builder_.CreateStore(builder_.CreateAdd(cur, builder_.getInt64(1)), idx);
        builder_.CreateBr(condBB);

        builder_.SetInsertPoint(endBB);
        popScope();
    }

    std::optional<CodeGen::Value> CodeGen::genMapBuiltin(const Call &call, const std::string &name)
    {
        if (name != "has" && name != "delete")
            return std::nullopt;
        if (call.typeArg)
            fail("'" + name + "' does not take a type argument", call.line, call.col);
        if (call.args.size() != 2)
            fail("'" + name + "' takes a map and a key", call.line, call.col);
        Value m = genExpr(*call.args[0]);
        if (!m.type->is(Type::Kind::Map))
            fail("'" + name + "' needs a map, found " + m.type->name(), call.args[0]->line,
                 call.args[0]->col);
        llvm::Value *ks = mapKeySlot(m.type, *call.args[1]);
        if (name == "has")
            return Value{builder_.CreateCall(mapsFn("Has").fn, {m.value, ks}), types_.boolT};
        builder_.CreateCall(mapsFn("Delete").fn, {m.value, ks});
        return Value{nullptr, types_.voidT};
    }

}

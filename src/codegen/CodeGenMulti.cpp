#include "codegen/CodeGen.h"

#include "common/Error.h"

#include <algorithm>
#include <set>

namespace pgt
{

    std::vector<CodeGen::Value> CodeGen::expandTuple(Value v)
    {
        std::vector<Value> out;
        for (size_t i = 0; i < v.type->elems.size(); ++i)
        {
            Value e{builder_.CreateExtractValue(v.value, static_cast<unsigned>(i)), v.type->elems[i]};
            out.push_back(owned(e));
        }
        return out;
    }

    std::vector<CodeGen::Value> CodeGen::genValueList(const std::vector<ExprPtr> &values, size_t want,
                                                      const std::vector<TyP> &hints, int line, int col)
    {
        std::vector<Value> out;
        if (values.size() == want)
        {
            for (size_t i = 0; i < want; ++i)
                out.push_back(genExpr(*values[i], i < hints.size() ? hints[i] : nullptr));
            return out;
        }
        if (values.size() != 1)
            fail("assignment mismatch: " + std::to_string(want) + " variables but " +
                     std::to_string(values.size()) + " values",
                 line, col);
        const Expr &e = *values[0];
        if (auto *call = dynamic_cast<const Call *>(&e))
        {
            Value v = genCall(*call, nullptr);
            if (v.type->is(Type::Kind::Void))
                fail("this call does not return a value", call->line, call->col);
            if (!v.type->is(Type::Kind::Tuple))
                fail("assignment mismatch: " + std::to_string(want) +
                         " variables but the call returns 1 value",
                     call->line, call->col);
            if (v.type->elems.size() != want)
                fail("assignment mismatch: " + std::to_string(want) + " variables but the call returns " +
                         std::to_string(v.type->elems.size()) + " values",
                     call->line, call->col);
            return expandTuple(v);
        }
        if (auto *ta = dynamic_cast<const TypeAssert *>(&e))
            if (want == 2 && !ta->isTypeKeyword)
                return genAssertOk(*ta);
        fail("assignment mismatch: " + std::to_string(want) + " variables but 1 value", line, col);
    }

    void CodeGen::genMultiVarDecl(const MultiVarDecl &decl)
    {
        size_t n = decl.names.size();
        std::vector<Value> vals = genValueList(decl.values, n, {}, decl.line, decl.col);
        bool anyNew = false;
        for (const std::string &name : decl.names)
            anyNew = anyNew || name != "_";
        if (!anyNew)
            fail("no new variables on the left side of '='", decl.line, decl.col);
        std::vector<llvm::Value *> taken(n, nullptr);
        for (size_t i = 0; i < n; ++i)
        {
            if (decl.names[i] == "_")
                continue;
            TyP ty = vals[i].type;
            if (ty->is(Type::Kind::Nil))
                fail("cannot infer the type of '" + decl.names[i] + "' from nil", decl.line,
                     decl.cols[i]);
            if (ty->is(Type::Kind::Void) || ty->is(Type::Kind::Tuple))
                fail("'" + decl.names[i] + "' cannot be initialized with this value", decl.line,
                     decl.cols[i]);
            taken[i] = take(vals[i]);
        }
        for (size_t i = 0; i < n; ++i)
        {
            if (!taken[i])
                continue;
            llvm::Value *slot = newVarSlot(vals[i].type, decl.names[i]);
            builder_.CreateStore(taken[i], slot);
            declare(decl.names[i], Var{slot, vals[i].type, false}, decl.line, decl.cols[i]);
        }
    }

    void CodeGen::genMultiAssign(const MultiAssign &assign)
    {
        size_t n = assign.targets.size();
        std::vector<std::optional<Addr>> addrs(n);
        std::vector<TyP> hints(n, nullptr);
        for (size_t i = 0; i < n; ++i)
        {
            const Expr &t = *assign.targets[i];
            if (auto *ref = dynamic_cast<const VarRef *>(&t))
            {
                if (ref->name == "_")
                    continue;
                if (Var *v = lookupLocal(ref->name); v && v->isManual && !v->indirect)
                    fail("cannot assign to '" + ref->name + "': a 'var&' variable is manually managed",
                         t.line, t.col);
            }
            mapTarget_ = MapTarget{};
            addrs[i] = requireAddress(t, true);
            if (mapTarget_.active)
                fail("multiple assignment to map elements is not supported", t.line, t.col);
            mapTarget_ = MapTarget{};
            hints[i] = addrs[i]->type;
        }
        std::vector<Value> vals = genValueList(assign.values, n, hints, assign.line, assign.col);
        std::vector<llvm::Value *> taken(n, nullptr);
        for (size_t i = 0; i < n; ++i)
        {
            if (!addrs[i])
                continue;
            Value v = coerce(vals[i], addrs[i]->type, assign.line, assign.col, "assignment");
            taken[i] = take(v);
        }
        for (size_t i = 0; i < n; ++i)
        {
            if (!addrs[i])
                continue;
            TyP ty = addrs[i]->type;
            bool initial = isManualElement(*assign.targets[i]);
            if (!isManaged(ty) || initial)
            {
                builder_.CreateStore(taken[i], addrs[i]->addr);
                continue;
            }
            llvm::Value *old = builder_.CreateLoad(llvmType(ty), addrs[i]->addr);
            builder_.CreateStore(taken[i], addrs[i]->addr);
            emitRelease(old, ty);
        }
    }

    void CodeGen::genMultiReturn(const Return &ret)
    {
        std::vector<const Expr *> exprs{ret.value.get()};
        for (const ExprPtr &r : ret.rest)
            exprs.push_back(r.get());
        const std::vector<TyP> &elems = currentRet_->elems;
        if (exprs.size() == 1)
        {
            auto *call = dynamic_cast<const Call *>(exprs[0]);
            if (!call)
                fail("wrong number of return values: expected " + std::to_string(elems.size()) + ", got 1",
                     ret.line, ret.col);
            Value v = genCall(*call, nullptr);
            if (v.type != currentRet_)
                fail("cannot return " + v.type->name() + " from a function returning " +
                         currentRet_->name(),
                     ret.line, ret.col);
            flushTemps();
            emitScopeReleases(0);
            builder_.CreateRet(v.value);
            startDeadBlock();
            return;
        }
        if (exprs.size() != elems.size())
            fail("wrong number of return values: expected " + std::to_string(elems.size()) + ", got " +
                     std::to_string(exprs.size()),
                 ret.line, ret.col);
        std::vector<Value> vals;
        for (size_t i = 0; i < exprs.size(); ++i)
            vals.push_back(coerce(genExpr(*exprs[i], elems[i]), elems[i], exprs[i]->line, exprs[i]->col,
                                  "return"));
        llvm::Value *agg = llvm::UndefValue::get(llvmType(currentRet_));
        for (size_t i = 0; i < vals.size(); ++i)
            agg = builder_.CreateInsertValue(agg, take(vals[i]), static_cast<unsigned>(i));
        flushTemps();
        emitScopeReleases(0);
        builder_.CreateRet(agg);
        startDeadBlock();
    }

    llvm::Value *CodeGen::dynTypeDesc(Value v)
    {
        if (v.type->is(Type::Kind::Any))
            return builder_.CreateExtractValue(v.value, 1);
        auto *ptr = builder_.getPtrTy();
        llvm::Value *vt = builder_.CreateExtractValue(v.value, 1);
        llvm::Value *isNil = builder_.CreateIsNull(vt);
        llvm::Function *cur = builder_.GetInsertBlock()->getParent();
        auto *loadBB = llvm::BasicBlock::Create(ctx_, "desc.load", cur);
        auto *doneBB = llvm::BasicBlock::Create(ctx_, "desc.done", cur);
        llvm::BasicBlock *from = builder_.GetInsertBlock();
        builder_.CreateCondBr(isNil, doneBB, loadBB);
        builder_.SetInsertPoint(loadBB);
        llvm::Value *loaded = builder_.CreateLoad(ptr, vt);
        builder_.CreateBr(doneBB);
        builder_.SetInsertPoint(doneBB);
        auto *phi = builder_.CreatePHI(ptr, 2);
        phi->addIncoming(llvm::ConstantPointerNull::get(ptr), from);
        phi->addIncoming(loaded, loadBB);
        return phi;
    }

    llvm::Function *CodeGen::ifaceAssertFn(TyP ifaceType)
    {
        if (auto it = assertFns_.find(ifaceType); it != assertFns_.end())
            return it->second;
        auto *ptr = builder_.getPtrTy();
        auto *fnTy = llvm::FunctionType::get(ptr, {ptr}, false);
        auto *fn = llvm::Function::Create(fnTy, llvm::Function::InternalLinkage,
                                          "pgt.assert." + ifaceType->name(), module_.get());
        assertFns_[ifaceType] = fn;
        return fn;
    }

    void CodeGen::finishAssertFns()
    {
        llvm::IRBuilderBase::InsertPointGuard guard(builder_);
        for (auto &[iface, fn] : assertFns_)
        {
            std::vector<TyP> cands;
            for (const auto &[t, gv] : descs_)
                if ((t->is(Type::Kind::Struct) || t->isPtrToStruct()) &&
                    missingMethod(t, *iface->iface).empty())
                    cands.push_back(t);
            std::sort(cands.begin(), cands.end(), [](TyP a, TyP b) { return a->name() < b->name(); });
            builder_.SetInsertPoint(llvm::BasicBlock::Create(ctx_, "entry", fn));
            for (TyP t : cands)
            {
                auto *hit = llvm::BasicBlock::Create(ctx_, "hit", fn);
                auto *next = llvm::BasicBlock::Create(ctx_, "next", fn);
                builder_.CreateCondBr(builder_.CreateICmpEQ(fn->getArg(0), typeDesc(t)), hit, next);
                builder_.SetInsertPoint(hit);
                builder_.CreateRet(vtableFor(t, iface));
                builder_.SetInsertPoint(next);
            }
            builder_.CreateRet(llvm::ConstantPointerNull::get(builder_.getPtrTy()));
        }
    }

    llvm::Value *CodeGen::assertMatch(llvm::Value *desc, TyP target)
    {
        if (target->is(Type::Kind::Interface))
            return builder_.CreateIsNotNull(builder_.CreateCall(ifaceAssertFn(target), {desc}));
        if (target->is(Type::Kind::Any))
            return builder_.CreateIsNotNull(desc);
        return builder_.CreateICmpEQ(desc, typeDesc(target));
    }

    llvm::Value *CodeGen::assertExtract(llvm::Value *data, llvm::Value *desc, TyP target)
    {
        llvm::Value *res;
        if (target->is(Type::Kind::Interface) || target->is(Type::Kind::Any))
        {
            llvm::Value *second = target->is(Type::Kind::Any)
                                      ? desc
                                      : builder_.CreateCall(ifaceAssertFn(target), {desc});
            res = llvm::UndefValue::get(ifaceTy_);
            res = builder_.CreateInsertValue(res, data, 0);
            res = builder_.CreateInsertValue(res, second, 1);
        }
        else if (target->is(Type::Kind::Pointer) || target->is(Type::Kind::Map))
            res = data;
        else
            res = builder_.CreateLoad(llvmType(target), data);
        emitRetain(res, target);
        return res;
    }

    void CodeGen::checkAssertable(Value src, TyP target, int line, int col)
    {
        if (!src.type->is(Type::Kind::Interface) && !src.type->is(Type::Kind::Any))
            fail("invalid type assertion: the value has type " + src.type->name() +
                     ", which is not an interface",
                 line, col);
        if (src.type->is(Type::Kind::Interface) && !target->is(Type::Kind::Interface) &&
            !target->is(Type::Kind::Any))
        {
            std::string why = missingMethod(target, *src.type->iface);
            if (!why.empty())
                fail("impossible type assertion: " + target->name() + " does not implement " +
                         src.type->name() + " (" + why + ")",
                     line, col);
        }
    }

    CodeGen::Value CodeGen::assertTo(Value src, TyP target, bool commaOk, llvm::Value **okOut)
    {
        llvm::Value *desc = dynTypeDesc(src);
        llvm::Value *data = builder_.CreateExtractValue(src.value, 0);
        llvm::Value *match = assertMatch(desc, target);
        llvm::Function *f = builder_.GetInsertBlock()->getParent();
        auto *okBB = llvm::BasicBlock::Create(ctx_, "assert.ok", f);
        if (!commaOk)
        {
            auto *badBB = llvm::BasicBlock::Create(ctx_, "assert.fail", f);
            builder_.CreateCondBr(match, okBB, badBB);
            builder_.SetInsertPoint(badBB);
            auto *ptr = builder_.getPtrTy();
            llvm::GlobalVariable *nilName = module_->getNamedGlobal("pgt.nilname");
            if (!nilName)
            {
                auto *str = builder_.CreateGlobalString("nil", ".nilstr", 0, module_.get());
                nilName = new llvm::GlobalVariable(*module_, ptr, true, llvm::GlobalValue::InternalLinkage,
                                                   str, "pgt.nilname");
            }
            llvm::Value *namePtr = builder_.CreateSelect(builder_.CreateIsNull(desc),
                                                         static_cast<llvm::Value *>(nilName),
                                                         builder_.CreateStructGEP(descTy_, desc, 2));
            llvm::Value *dyn = builder_.CreateLoad(ptr, namePtr);
            llvm::Value *want = builder_.CreateGlobalString(target->name(), ".want", 0, module_.get());
            rt("pgt_assert_fail", builder_.getVoidTy(), {dyn, want}, true);
            builder_.CreateUnreachable();
            builder_.SetInsertPoint(okBB);
            return owned({assertExtract(data, desc, target), target});
        }
        auto *doneBB = llvm::BasicBlock::Create(ctx_, "assert.done", f);
        llvm::BasicBlock *from = builder_.GetInsertBlock();
        builder_.CreateCondBr(match, okBB, doneBB);
        builder_.SetInsertPoint(okBB);
        llvm::Value *val = assertExtract(data, desc, target);
        llvm::BasicBlock *okEnd = builder_.GetInsertBlock();
        builder_.CreateBr(doneBB);
        builder_.SetInsertPoint(doneBB);
        auto *phi = builder_.CreatePHI(llvmType(target), 2);
        phi->addIncoming(zeroValue(target), from);
        phi->addIncoming(val, okEnd);
        *okOut = match;
        return owned({phi, target});
    }

    CodeGen::Value CodeGen::genTypeAssert(const TypeAssert &ta)
    {
        if (ta.isTypeKeyword)
            fail("'.(type)' can only be used in a type switch: switch v = x.(type)", ta.line, ta.col);
        Value src = genExpr(*ta.base);
        TyP target = resolveType(ta.type);
        checkAssertable(src, target, ta.line, ta.col);
        return assertTo(src, target, false, nullptr);
    }

    std::vector<CodeGen::Value> CodeGen::genAssertOk(const TypeAssert &ta)
    {
        Value src = genExpr(*ta.base);
        TyP target = resolveType(ta.type);
        checkAssertable(src, target, ta.line, ta.col);
        llvm::Value *ok = nullptr;
        Value v = assertTo(src, target, true, &ok);
        return {v, Value{ok, types_.boolT}};
    }

    void CodeGen::genTypeSwitch(const Switch &sw)
    {
        llvm::Function *f = builder_.GetInsertBlock()->getParent();
        auto *endBB = llvm::BasicBlock::Create(ctx_, "switch.end", f);
        pushScope();
        Value tag = genExpr(*sw.tag);
        if (!tag.type->is(Type::Kind::Interface) && !tag.type->is(Type::Kind::Any))
            fail("a type switch needs an interface or any value, found " + tag.type->name(),
                 sw.tag->line, sw.tag->col);
        holdValue(tag);
        flushTemps();
        llvm::Value *desc = dynTypeDesc(tag);
        llvm::Value *data = builder_.CreateExtractValue(tag.value, 0);

        std::vector<std::vector<TyP>> caseTypes(sw.cases.size());
        std::set<TyP> seen;
        bool seenNil = false;
        std::vector<llvm::BasicBlock *> bodies;
        for (size_t i = 0; i < sw.cases.size(); ++i)
            bodies.push_back(llvm::BasicBlock::Create(ctx_, "case.body", f));
        auto *defaultBB = sw.hasDefault ? llvm::BasicBlock::Create(ctx_, "switch.default", f) : endBB;

        for (size_t i = 0; i < sw.cases.size(); ++i)
        {
            for (const TypeExpr &te : sw.cases[i].types)
            {
                llvm::Value *cond;
                if (te.kind == TypeExpr::Kind::Named && te.qualifier.empty() && te.name == "nil")
                {
                    if (seenNil)
                        fail("duplicate case nil in type switch", te.line, te.col);
                    seenNil = true;
                    caseTypes[i].push_back(nullptr);
                    cond = builder_.CreateIsNull(desc);
                }
                else
                {
                    TyP t = resolveType(te);
                    if (!seen.insert(t).second)
                        fail("duplicate case " + t->name() + " in type switch", te.line, te.col);
                    checkAssertable(tag, t, te.line, te.col);
                    caseTypes[i].push_back(t);
                    cond = assertMatch(desc, t);
                }
                auto *next = llvm::BasicBlock::Create(ctx_, "case.next", f);
                builder_.CreateCondBr(cond, bodies[i], next);
                builder_.SetInsertPoint(next);
            }
        }
        builder_.CreateBr(defaultBB);

        auto bindTag = [&](TyP single, const Block &body, int line, int col)
        {
            pushScope();
            if (!sw.bind.empty() && sw.bind != "_")
            {
                TyP ty = single ? single : tag.type;
                llvm::Value *val;
                if (single)
                    val = assertExtract(data, desc, single);
                else
                {
                    val = tag.value;
                    emitRetain(val, tag.type);
                }
                llvm::Value *slot = newVarSlot(ty, sw.bind);
                builder_.CreateStore(val, slot);
                declare(sw.bind, Var{slot, ty, false}, line, col);
            }
            genBlock(body);
            popScope();
        };

        loops_.push_back({nullptr, endBB, scopes_.size()});
        for (size_t i = 0; i < sw.cases.size(); ++i)
        {
            builder_.SetInsertPoint(bodies[i]);
            TyP single = caseTypes[i].size() == 1 ? caseTypes[i][0] : nullptr;
            bindTag(single, sw.cases[i].body, sw.cases[i].line, sw.cases[i].col);
            builder_.CreateBr(endBB);
        }
        if (sw.hasDefault)
        {
            builder_.SetInsertPoint(defaultBB);
            bindTag(nullptr, sw.defaultBody, sw.line, sw.col);
            builder_.CreateBr(endBB);
        }
        loops_.pop_back();
        builder_.SetInsertPoint(endBB);
        popScope();
    }

}

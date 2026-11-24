#include "codegen/CodeGen.h"

#include "common/Error.h"

#include <algorithm>

namespace pgt
{

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
            genVarDecl(*decl);
            flushTemps();
            return;
        }
        if (auto *md = dynamic_cast<const MultiVarDecl *>(&stmt))
        {
            genMultiVarDecl(*md);
            flushTemps();
            return;
        }
        if (auto *ma = dynamic_cast<const MultiAssign *>(&stmt))
        {
            genMultiAssign(*ma);
            flushTemps();
            return;
        }
        if (auto *assign = dynamic_cast<const Assign *>(&stmt))
        {
            genAssign(*assign);
            flushTemps();
            return;
        }
        if (auto *ret = dynamic_cast<const Return *>(&stmt))
            return genReturn(*ret);
        if (auto *es = dynamic_cast<const ExprStmt *>(&stmt))
        {
            Value v = genCall(*static_cast<const Call *>(es->expr.get()), nullptr);
            if (v.type->is(Type::Kind::Tuple))
                expandTuple(v);
            flushTemps();
            return;
        }
        if (auto *node = dynamic_cast<const If *>(&stmt))
            return genIf(*node);
        if (auto *loop = dynamic_cast<const Loop *>(&stmt))
            return genLoop(*loop);
        if (auto *loop = dynamic_cast<const ForIn *>(&stmt))
            return genForIn(*loop);
        if (auto *sw = dynamic_cast<const Switch *>(&stmt))
            return genSwitch(*sw);
        if (auto *as = dynamic_cast<const AsmStmt *>(&stmt))
        {
            genAsm(*as);
            flushTemps();
            return;
        }
        if (dynamic_cast<const Break *>(&stmt))
        {
            if (loops_.empty())
                fail("'break' outside of a loop or switch", stmt.line, stmt.col);
            emitScopeReleases(loops_.back().depth);
            builder_.CreateBr(loops_.back().breakTo);
            startDeadBlock();
            return;
        }
        if (dynamic_cast<const Continue *>(&stmt))
        {
            llvm::BasicBlock *target = nullptr;
            size_t depth = 0;
            for (auto it = loops_.rbegin(); it != loops_.rend() && !target; ++it)
            {
                target = it->continueTo;
                depth = it->depth;
            }
            if (!target)
                fail("'continue' outside of a loop", stmt.line, stmt.col);
            emitScopeReleases(depth);
            builder_.CreateBr(target);
            startDeadBlock();
            return;
        }
        fail("internal error: unknown statement", stmt.line, stmt.col);
    }

    void CodeGen::genAsm(const AsmStmt &stmt)
    {
        const FuncInfo *run = nullptr;
        for (const auto &[name, syms] : fileImports_)
            if (syms->pkg->importPath == "asm")
                run = &syms->functions.at("RunBlock");
        if (!run)
            fail("asm block needs #use \"pgt/std/asm\"", stmt.line, stmt.col);

        struct Slot
        {
            llvm::Value *addr;
            TyP type;
            int size;
        };
        std::vector<Slot> slots;
        for (const std::string &name : stmt.names)
        {
            auto var = lookupVar(name);
            if (!var)
                fail("undefined variable '" + name + "' in asm block", stmt.line, stmt.col);
            if (var->isConst)
                fail("constant '" + name + "' cannot be used in an asm block", stmt.line, stmt.col);
            int size = 0;
            switch (var->type->kind)
            {
            case Type::Kind::Int:
                size = 4;
                break;
            case Type::Kind::Int64:
            case Type::Kind::Pointer:
                size = 8;
                break;
            case Type::Kind::Byte:
            case Type::Kind::Char:
            case Type::Kind::Bool:
                size = 1;
                break;
            default:
                fail("variable '" + name + "' has type " + var->type->name() +
                         "; asm blocks accept only int, int64, byte, char, bool and pointers (*T)",
                     stmt.line, stmt.col);
            }
            slots.push_back({var->addr, var->type, size});
        }

        auto *i64 = builder_.getInt64Ty();
        auto *tableTy = llvm::ArrayType::get(i64, std::max<size_t>(slots.size(), 1));
        llvm::Value *table = entryAlloca(tableTy, "asm.table");
        builder_.CreateStore(llvm::ConstantAggregateZero::get(tableTy), table);
        auto slotPtr = [&](size_t i)
        {
            return builder_.CreateGEP(builder_.getInt8Ty(), table, builder_.getInt64(8 * i));
        };
        for (size_t i = 0; i < slots.size(); ++i)
        {
            llvm::Value *v = builder_.CreateLoad(llvmType(slots[i].type), slots[i].addr);
            if (slots[i].type->is(Type::Kind::Bool))
                v = builder_.CreateZExt(v, builder_.getInt8Ty());
            builder_.CreateStore(v, slotPtr(i));
        }

        auto *cacheTy = llvm::ArrayType::get(i64, 2);
        auto *cache = new llvm::GlobalVariable(*module_, cacheTy, false, llvm::GlobalValue::InternalLinkage,
                                               llvm::ConstantAggregateZero::get(cacheTy), "asm.cache");

        std::vector<Value> nameVals, sizeVals;
        for (size_t i = 0; i < slots.size(); ++i)
        {
            nameVals.push_back({builder_.CreateGlobalString(stmt.names[i], ".str"), types_.stringT});
            sizeVals.push_back({builder_.getInt32(slots[i].size), types_.intT});
        }
        Value names = packArray(types_.stringT, nameVals);
        Value sizes = packArray(types_.intT, sizeVals);
        llvm::Value *text = builder_.CreateGlobalString(stmt.text, ".str");
        llvm::Value *where = builder_.CreateGlobalString(currentFile_ + ":" + std::to_string(stmt.line), ".str");
        builder_.CreateCall(run->fn, {cache, text, names.value, sizes.value, table, where,
                                      builder_.getInt32(stmt.line)});

        for (size_t i = 0; i < slots.size(); ++i)
        {
            llvm::Type *ty = llvmType(slots[i].type);
            llvm::Value *v;
            if (slots[i].type->is(Type::Kind::Bool))
            {
                llvm::Value *b = builder_.CreateLoad(builder_.getInt8Ty(), slotPtr(i));
                v = builder_.CreateICmpNE(b, builder_.getInt8(0));
            }
            else
            {
                v = builder_.CreateLoad(ty, slotPtr(i));
            }
            builder_.CreateStore(v, slots[i].addr);
        }
    }

    const FuncInfo &CodeGen::memoryFn(const std::string &name)
    {
        return syms_.at("memory").functions.at(name);
    }

    const FuncInfo &CodeGen::runtimeFn(const std::string &name)
    {
        return syms_.at("runtime").functions.at(name);
    }

    void CodeGen::genManualVarDecl(const VarDecl &decl)
    {
        const auto &m = static_cast<const MallocExpr &>(*decl.init);
        bool hasMemory = false;
        for (const auto &[name, syms] : fileImports_)
            hasMemory = hasMemory || syms->pkg->importPath == "memory";
        if (!hasMemory)
            fail("malloc needs #use \"pgt/std/memory\"", m.line, m.col);

        for (size_t i = scopes_.size(); i-- > 0;)
        {
            auto found = scopes_[i].vars.find(decl.name);
            if (found == scopes_[i].vars.end())
                continue;
            return handOverToManual(decl, found->second, i);
        }

        TyP elem = resolveType(m.elem);
        Value n = genExpr(*m.count, types_.intT);
        if (!n.type->isIntegral())
            fail("the size in 'malloc' must be an integer, found " + n.type->name(), m.count->line,
                 m.count->col);
        auto *i64 = builder_.getInt64Ty();
        llvm::Value *count = builder_.CreateIntCast(n.value, i64, !n.type->isUnsigned());
        llvm::Value *data = builder_.CreateCall(memoryFn("alloc").fn, {count, sizeOf(elem)});

        TyP ty = types_.arrayOf(elem);
        llvm::Value *arr = zeroValue(ty);
        arr = builder_.CreateInsertValue(arr, data, 0);
        arr = builder_.CreateInsertValue(arr, count, 1);
        arr = builder_.CreateInsertValue(arr, count, 2);
        llvm::Value *slot = newVarSlot(ty, decl.name);
        builder_.CreateStore(arr, slot);
        declare(decl.name, Var{slot, ty, false, true}, decl.line, decl.col);
    }

    void CodeGen::handOverToManual(const VarDecl &decl, Var &var, size_t scopeIndex)
    {
        const auto &m = static_cast<const MallocExpr &>(*decl.init);
        if (var.isManual)
            fail("'" + decl.name + "' is already manually managed", decl.line, decl.col);
        if (var.isConst)
            fail("cannot hand the constant '" + decl.name + "' over to malloc", decl.line, decl.col);
        if (heapVars_.count(decl.name))
            fail("cannot hand '" + decl.name + "' over to malloc: its address is taken with '&'", decl.line,
                 decl.col);
        if (scopeIndex + 1 != scopes_.size())
            fail("'var& := " + decl.name + "' must be in the same block where '" + decl.name +
                     "' is declared",
                 decl.line, decl.col);

        TyP elem = resolveType(m.elem);
        bool isArray = var.type->is(Type::Kind::Array);
        TyP want = isArray ? var.type->elem : var.type;
        bool rawBytes = elem->is(Type::Kind::Byte) && want != elem;
        if (rawBytes)
            elem = want;
        else if (elem != want)
            fail("cannot hand '" + decl.name + "' (" + var.type->name() + ") over to malloc(free[" +
                     elem->name() + "] ...): the element type must be " + want->name(),
                 decl.line, decl.col);

        Value n = genExpr(*m.count, types_.intT);
        if (!n.type->isIntegral())
            fail("the size in 'malloc' must be an integer, found " + n.type->name(), m.count->line,
                 m.count->col);
        auto *i64 = builder_.getInt64Ty();
        llvm::Value *count = builder_.CreateIntCast(n.value, i64, !n.type->isUnsigned());
        llvm::Type *elemLl = llvmType(elem);
        if (rawBytes)
        {
            llvm::Value *size = sizeOf(elem);
            StringLit msg;
            msg.value = "malloc: the block is too small for '" + decl.name + "' (" + elem->name() +
                        " needs " + std::to_string(module_->getDataLayout().getTypeAllocSize(elemLl)) +
                        " bytes)";
            panicIf(builder_.CreateICmpSLT(count, size), "panicString", {genExpr(msg).value});
            count = builder_.CreateSDiv(count, size);
        }

        auto &owned = scopes_[scopeIndex].owned;
        owned.erase(std::remove_if(owned.begin(), owned.end(),
                                   [&](const OwnedLocal &o) { return o.slot == var.addr; }),
                    owned.end());

        if (isArray)
        {
            llvm::Value *data = builder_.CreateCall(memoryFn("alloc").fn, {count, sizeOf(elem)});
            llvm::Value *old = builder_.CreateLoad(arrayTy_, var.addr);
            llvm::Value *oldData = builder_.CreateExtractValue(old, 0);
            llvm::Value *oldLen = builder_.CreateExtractValue(old, 1);
            llvm::Value *copyN = builder_.CreateSelect(builder_.CreateICmpSLT(oldLen, count), oldLen, count);

            llvm::Function *f = builder_.GetInsertBlock()->getParent();
            llvm::BasicBlock *pre = builder_.GetInsertBlock();
            llvm::BasicBlock *loop = llvm::BasicBlock::Create(ctx_, "handover.copy", f);
            llvm::BasicBlock *done = llvm::BasicBlock::Create(ctx_, "handover.done", f);
            builder_.CreateCondBr(builder_.CreateICmpSGT(copyN, builder_.getInt64(0)), loop, done);
            builder_.SetInsertPoint(loop);
            llvm::PHINode *i = builder_.CreatePHI(i64, 2);
            i->addIncoming(builder_.getInt64(0), pre);
            llvm::Value *v = builder_.CreateLoad(elemLl, builder_.CreateGEP(elemLl, oldData, i));
            emitRetain(v, elem);
            builder_.CreateStore(v, builder_.CreateGEP(elemLl, data, i));
            llvm::Value *next = builder_.CreateAdd(i, builder_.getInt64(1));
            i->addIncoming(next, builder_.GetInsertBlock());
            builder_.CreateCondBr(builder_.CreateICmpSLT(next, copyN), loop, done);
            builder_.SetInsertPoint(done);

            emitRelease(old, var.type);
            llvm::Value *arr = zeroValue(var.type);
            arr = builder_.CreateInsertValue(arr, data, 0);
            arr = builder_.CreateInsertValue(arr, count, 1);
            arr = builder_.CreateInsertValue(arr, count, 2);
            builder_.CreateStore(arr, var.addr);
            var.isManual = true;
            return;
        }

        llvm::Value *slots = builder_.CreateSelect(builder_.CreateICmpSLT(count, builder_.getInt64(1)),
                                                   builder_.getInt64(1), count);
        llvm::Value *data = builder_.CreateCall(memoryFn("alloc").fn, {slots, sizeOf(elem)});
        builder_.CreateStore(builder_.CreateLoad(elemLl, var.addr), data);
        llvm::Value *ptrSlot = entryAlloca(builder_.getPtrTy(), decl.name + ".manual");
        builder_.CreateStore(data, ptrSlot);
        llvm::Value *countSlot = entryAlloca(i64, decl.name + ".count");
        builder_.CreateStore(slots, countSlot);
        var.addr = ptrSlot;
        var.indirect = true;
        var.countSlot = countSlot;
        var.isManual = true;
    }

    void CodeGen::genVarDecl(const VarDecl &decl)
    {
        if (decl.isManual)
            return genManualVarDecl(decl);
        TyP ty = decl.type ? resolveType(*decl.type) : nullptr;
        llvm::Value *init = nullptr;
        Value initVal;
        if (decl.init)
        {
            Value v = genExpr(*decl.init, ty);
            if (ty)
                v = coerce(v, ty, decl.line, decl.col, "the initializer of '" + decl.name + "'");
            else
                ty = v.type;
            init = v.value;
            initVal = v;
        }
        if (ty->is(Type::Kind::Nil))
            fail("cannot infer the type of '" + decl.name + "' from nil; write its type: var* " +
                     decl.name + " *T = nil",
                 decl.line, decl.col);
        if (ty->is(Type::Kind::Void))
            fail("'" + decl.name + "' cannot be initialized with a call that returns nothing", decl.line,
                 decl.col);
        if (init)
            init = take(initVal);
        else
            init = zeroValue(ty);
        llvm::Value *slot = newVarSlot(ty, decl.name);
        builder_.CreateStore(init, slot);
        declare(decl.name, Var{slot, ty, false}, decl.line, decl.col);
    }

    void CodeGen::genAssign(const Assign &assign)
    {
        if (auto *ref = dynamic_cast<const VarRef *>(assign.target.get()))
            if (Var *v = lookupLocal(ref->name); v && v->isManual && !v->indirect)
                fail("cannot assign to '" + ref->name +
                         "': a 'var&' variable is manually managed and cannot be reassigned "
                         "(release it with free(" + ref->name + "))",
                     assign.line, assign.col);
        mapTarget_ = MapTarget{};
        Addr target = requireAddress(*assign.target, true);
        MapTarget mt = mapTarget_;
        mapTarget_ = MapTarget{};
        if (mt.active && !dynamic_cast<const Index *>(assign.target.get()))
            fail("cannot assign to a field of a map element", assign.line, assign.col);
        Value rhs = genExpr(*assign.value, target.type);
        bool manualElem = isManualElement(*assign.target);

        if (assign.op == AssignOp::Set)
        {
            rhs = coerce(rhs, target.type, assign.line, assign.col, "assignment");
            storeValue(target.addr, target.type, rhs, manualElem);
            commitMapTarget(mt);
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
        Value cur{builder_.CreateLoad(llvmType(target.type), target.addr), target.type};
        Value res = genArith(op, cur, rhs, assign.line, assign.col);
        if (res.type != target.type)
            fail("cannot assign " + res.type->name() + " to a value of type " + target.type->name(),
                 assign.line, assign.col);
        storeValue(target.addr, target.type, res, manualElem);
        commitMapTarget(mt);
    }

    void CodeGen::genReturn(const Return &ret)
    {
        if (currentRet_->is(Type::Kind::Tuple) && ret.value)
            return genMultiReturn(ret);
        if (!ret.rest.empty())
            fail("this function returns a single value", ret.line, ret.col);
        if (currentRet_->is(Type::Kind::Void))
        {
            if (ret.value)
                fail("this function does not return a value", ret.line, ret.col);
            flushTemps();
            emitScopeReleases(0);
            builder_.CreateRetVoid();
        }
        else
        {
            if (!ret.value)
                fail("missing return value of type " + currentRet_->name(), ret.line, ret.col);
            Value v = genExpr(*ret.value, currentRet_);
            v = coerce(v, currentRet_, ret.line, ret.col, "return");
            llvm::Value *result = take(v);
            flushTemps();
            emitScopeReleases(0);
            builder_.CreateRet(result);
        }
        startDeadBlock();
    }

    void CodeGen::genIf(const If &node)
    {
        llvm::Function *f = builder_.GetInsertBlock()->getParent();
        Value cond = genCond(*node.cond);
        flushTemps();

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
            flushTemps();
            builder_.CreateCondBr(cond.value, bodyBB, endBB);
        }
        else
        {
            builder_.CreateBr(bodyBB);
        }

        builder_.SetInsertPoint(bodyBB);
        loops_.push_back({postBB, endBB, scopes_.size()});
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

    void CodeGen::genForIn(const ForIn &loop)
    {
        llvm::Function *f = builder_.GetInsertBlock()->getParent();
        auto *condBB = llvm::BasicBlock::Create(ctx_, "for.cond", f);
        auto *bodyBB = llvm::BasicBlock::Create(ctx_, "for.body", f);
        auto *postBB = llvm::BasicBlock::Create(ctx_, "for.post", f);
        auto *endBB = llvm::BasicBlock::Create(ctx_, "for.end", f);
        pushScope();

        if (loop.rangeEnd)
        {
            Value from = genExpr(*loop.iterable, types_.intT);
            Value to = genExpr(*loop.rangeEnd, from.type);
            if (!from.type->isIntegral() || from.type->isUnsigned() || to.type != from.type)
                fail("the bounds of a range must be integers of the same type", loop.line, loop.col);
            TyP ty = from.type;
            llvm::Value *slot = newVarSlot(ty, loop.value);
            builder_.CreateStore(from.value, slot);
            declare(loop.value, Var{slot, ty, false}, loop.line, loop.col);
            builder_.CreateBr(condBB);

            builder_.SetInsertPoint(condBB);
            llvm::Value *i = builder_.CreateLoad(llvmType(ty), slot, loop.value);
            builder_.CreateCondBr(builder_.CreateICmpSLT(i, to.value), bodyBB, endBB);

            builder_.SetInsertPoint(bodyBB);
            loops_.push_back({postBB, endBB, scopes_.size()});
            genBlock(loop.body);
            loops_.pop_back();
            builder_.CreateBr(postBB);

            builder_.SetInsertPoint(postBB);
            llvm::Value *cur = builder_.CreateLoad(llvmType(ty), slot, loop.value);
            builder_.CreateStore(builder_.CreateAdd(cur, llvm::ConstantInt::get(llvmType(ty), 1)),
                                 slot);
            builder_.CreateBr(condBB);

            builder_.SetInsertPoint(endBB);
            popScope();
            return;
        }

        Value seq = genExpr(*loop.iterable);
        holdValue(seq);
        flushTemps();
        if (seq.type->is(Type::Kind::Map))
        {
            condBB->eraseFromParent();
            bodyBB->eraseFromParent();
            postBB->eraseFromParent();
            endBB->eraseFromParent();
            genMapForIn(loop, seq);
            return;
        }
        TyP elemTy;
        llvm::Value *len, *data;
        if (seq.type->is(Type::Kind::Array))
        {
            elemTy = seq.type->elem;
            data = builder_.CreateExtractValue(seq.value, 0, "for.data");
            len = builder_.CreateExtractValue(seq.value, 1, "for.len");
        }
        else if (seq.type->is(Type::Kind::String))
        {
            elemTy = types_.charT;
            data = seq.value;
            len = builder_.CreateSExt(rt("pgt_strlen", builder_.getInt32Ty(), {seq.value}),
                                      builder_.getInt64Ty(), "for.len");
        }
        else
        {
            fail("cannot iterate over " + seq.type->name() +
                     " (use an array, a string or a range a..b)",
                 loop.iterable->line, loop.iterable->col);
        }

        auto *idx = entryAlloca(builder_.getInt64Ty(), "for.i");
        builder_.CreateStore(builder_.getInt64(0), idx);
        llvm::Value *keySlot = nullptr;
        if (!loop.key.empty())
        {
            keySlot = newVarSlot(types_.intT, loop.key);
            declare(loop.key, Var{keySlot, types_.intT, false}, loop.line, loop.col);
        }
        llvm::Value *valSlot = newVarSlot(elemTy, loop.value);
        builder_.CreateStore(zeroValue(elemTy), valSlot);
        declare(loop.value, Var{valSlot, elemTy, false}, loop.line, loop.col);
        builder_.CreateBr(condBB);

        builder_.SetInsertPoint(condBB);
        llvm::Value *i = builder_.CreateLoad(builder_.getInt64Ty(), idx);
        builder_.CreateCondBr(builder_.CreateICmpSLT(i, len), bodyBB, endBB);

        builder_.SetInsertPoint(bodyBB);
        if (keySlot)
            builder_.CreateStore(builder_.CreateTrunc(i, builder_.getInt32Ty()), keySlot);
        llvm::Value *elemPtr = builder_.CreateGEP(llvmType(elemTy), data, i);
        storeValue(valSlot, elemTy, Value{builder_.CreateLoad(llvmType(elemTy), elemPtr), elemTy}, false);
        loops_.push_back({postBB, endBB, scopes_.size()});
        genBlock(loop.body);
        loops_.pop_back();
        builder_.CreateBr(postBB);

        builder_.SetInsertPoint(postBB);
        llvm::Value *cur = builder_.CreateLoad(builder_.getInt64Ty(), idx);
        builder_.CreateStore(builder_.CreateAdd(cur, builder_.getInt64(1)), idx);
        builder_.CreateBr(condBB);

        builder_.SetInsertPoint(endBB);
        popScope();
    }

    void CodeGen::genSwitch(const Switch &sw)
    {
        if (sw.typeSwitch)
            return genTypeSwitch(sw);
        llvm::Function *f = builder_.GetInsertBlock()->getParent();
        auto *endBB = llvm::BasicBlock::Create(ctx_, "switch.end", f);

        pushScope();
        Value tag;
        if (sw.tag)
        {
            tag = genExpr(*sw.tag);
            if (tag.type->is(Type::Kind::Void) || tag.type->is(Type::Kind::Nil))
                fail("cannot switch on " + tag.type->name(), sw.tag->line, sw.tag->col);
            holdValue(tag);
            flushTemps();
        }

        std::vector<llvm::BasicBlock *> bodies;
        for (size_t i = 0; i < sw.cases.size(); ++i)
            bodies.push_back(llvm::BasicBlock::Create(ctx_, "case.body", f));
        auto *defaultBB = sw.hasDefault ? llvm::BasicBlock::Create(ctx_, "switch.default", f) : endBB;

        for (size_t i = 0; i < sw.cases.size(); ++i)
        {
            const Case &c = sw.cases[i];
            for (const ExprPtr &valueExpr : c.values)
            {
                Value cond;
                if (sw.tag)
                {
                    Value v = genExpr(*valueExpr, tag.type);
                    cond = compareValues(BinOp::Eq, tag, v, valueExpr->line, valueExpr->col);
                }
                else
                {
                    cond = genCond(*valueExpr);
                }
                flushTemps();
                auto *next = llvm::BasicBlock::Create(ctx_, "case.next", f);
                builder_.CreateCondBr(cond.value, bodies[i], next);
                builder_.SetInsertPoint(next);
            }
        }
        builder_.CreateBr(defaultBB);

        loops_.push_back({nullptr, endBB, scopes_.size()});
        for (size_t i = 0; i < sw.cases.size(); ++i)
        {
            builder_.SetInsertPoint(bodies[i]);
            genBlock(sw.cases[i].body);
            builder_.CreateBr(endBB);
        }
        if (sw.hasDefault)
        {
            builder_.SetInsertPoint(defaultBB);
            genBlock(sw.defaultBody);
            builder_.CreateBr(endBB);
        }
        loops_.pop_back();
        builder_.SetInsertPoint(endBB);
        popScope();
    }

}

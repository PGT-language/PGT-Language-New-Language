#include "codegen/CodeGen.h"

#include "common/Error.h"

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
            return genVarDecl(*decl);
        if (auto *assign = dynamic_cast<const Assign *>(&stmt))
            return genAssign(*assign);
        if (auto *ret = dynamic_cast<const Return *>(&stmt))
            return genReturn(*ret);
        if (auto *es = dynamic_cast<const ExprStmt *>(&stmt))
        {
            genCall(*static_cast<const Call *>(es->expr.get()), nullptr);
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
        if (dynamic_cast<const Break *>(&stmt))
        {
            if (loops_.empty())
                fail("'break' outside of a loop or switch", stmt.line, stmt.col);
            builder_.CreateBr(loops_.back().breakTo);
            startDeadBlock();
            return;
        }
        if (dynamic_cast<const Continue *>(&stmt))
        {
            llvm::BasicBlock *target = nullptr;
            for (auto it = loops_.rbegin(); it != loops_.rend() && !target; ++it)
                target = it->continueTo;
            if (!target)
                fail("'continue' outside of a loop", stmt.line, stmt.col);
            builder_.CreateBr(target);
            startDeadBlock();
            return;
        }
        fail("internal error: unknown statement", stmt.line, stmt.col);
    }

    // `var& := x = malloc(free[T] N)`: x is a zeroed []T with len = cap = N, released by free(x).
    void CodeGen::genManualVarDecl(const VarDecl &decl)
    {
        const auto &m = static_cast<const MallocExpr &>(*decl.init);
        bool hasMemory = false;
        for (const auto &[name, syms] : fileImports_)
            hasMemory = hasMemory || syms->pkg->importPath == "memory";
        if (!hasMemory)
            fail("malloc needs #use \"pgt/std/memory\"", m.line, m.col);

        TyP elem = resolveType(m.elem);
        Value n = genExpr(*m.count, types_.intT);
        if (!n.type->isIntegral())
            fail("the size in 'malloc' must be an integer, found " + n.type->name(), m.count->line,
                 m.count->col);
        auto *i64 = builder_.getInt64Ty();
        llvm::Value *count = builder_.CreateIntCast(n.value, i64, !n.type->isUnsigned());
        llvm::Value *bytes = builder_.CreateMul(count, sizeOf(elem));
        llvm::Value *data = rt("pgt_manual_alloc", builder_.getPtrTy(), {bytes});

        TyP ty = types_.arrayOf(elem);
        llvm::Value *arr = zeroValue(ty);
        arr = builder_.CreateInsertValue(arr, data, 0);
        arr = builder_.CreateInsertValue(arr, count, 1);
        arr = builder_.CreateInsertValue(arr, count, 2);
        llvm::Value *slot = newVarSlot(ty, decl.name);
        builder_.CreateStore(arr, slot);
        declare(decl.name, Var{slot, ty, false, true}, decl.line, decl.col);
    }

    void CodeGen::genVarDecl(const VarDecl &decl)
    {
        if (decl.isManual)
            return genManualVarDecl(decl);
        TyP ty = decl.type ? resolveType(*decl.type) : nullptr;
        llvm::Value *init = nullptr;
        if (decl.init)
        {
            Value v = genExpr(*decl.init, ty);
            if (ty)
                v = coerce(v, ty, decl.line, decl.col, "the initializer of '" + decl.name + "'");
            else
                ty = v.type;
            init = v.value;
        }
        if (ty->is(Type::Kind::Nil))
            fail("cannot infer the type of '" + decl.name + "' from nil; write its type: var* " +
                     decl.name + " *T = nil",
                 decl.line, decl.col);
        if (ty->is(Type::Kind::Void))
            fail("'" + decl.name + "' cannot be initialized with a call that returns nothing", decl.line,
                 decl.col);
        if (!init)
            init = zeroValue(ty);
        llvm::Value *slot = newVarSlot(ty, decl.name);
        builder_.CreateStore(init, slot);
        declare(decl.name, Var{slot, ty, false}, decl.line, decl.col);
    }

    void CodeGen::genAssign(const Assign &assign)
    {
        if (auto *ref = dynamic_cast<const VarRef *>(assign.target.get()))
            if (Var *v = lookupLocal(ref->name); v && v->isManual)
                fail("cannot assign to '" + ref->name +
                         "': a 'var&' variable is manually managed and cannot be reassigned "
                         "(release it with free(" + ref->name + "))",
                     assign.line, assign.col);
        Addr target = requireAddress(*assign.target, true);
        Value rhs = genExpr(*assign.value, target.type);

        if (assign.op == AssignOp::Set)
        {
            rhs = coerce(rhs, target.type, assign.line, assign.col, "assignment");
            builder_.CreateStore(rhs.value, target.addr);
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
        builder_.CreateStore(res.value, target.addr);
    }

    void CodeGen::genReturn(const Return &ret)
    {
        if (currentRet_->is(Type::Kind::Void))
        {
            if (ret.value)
                fail("this function does not return a value", ret.line, ret.col);
            builder_.CreateRetVoid();
        }
        else
        {
            if (!ret.value)
                fail("missing return value of type " + currentRet_->name(), ret.line, ret.col);
            Value v = genExpr(*ret.value, currentRet_);
            v = coerce(v, currentRet_, ret.line, ret.col, "return");
            builder_.CreateRet(v.value);
        }
        startDeadBlock();
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
            loops_.push_back({postBB, endBB});
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
        declare(loop.value, Var{valSlot, elemTy, false}, loop.line, loop.col);
        builder_.CreateBr(condBB);

        builder_.SetInsertPoint(condBB);
        llvm::Value *i = builder_.CreateLoad(builder_.getInt64Ty(), idx);
        builder_.CreateCondBr(builder_.CreateICmpSLT(i, len), bodyBB, endBB);

        builder_.SetInsertPoint(bodyBB);
        if (keySlot)
            builder_.CreateStore(builder_.CreateTrunc(i, builder_.getInt32Ty()), keySlot);
        llvm::Value *elemPtr = builder_.CreateGEP(llvmType(elemTy), data, i);
        builder_.CreateStore(builder_.CreateLoad(llvmType(elemTy), elemPtr), valSlot);
        loops_.push_back({postBB, endBB});
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
        llvm::Function *f = builder_.GetInsertBlock()->getParent();
        auto *endBB = llvm::BasicBlock::Create(ctx_, "switch.end", f);

        Value tag;
        if (sw.tag)
        {
            tag = genExpr(*sw.tag);
            if (tag.type->is(Type::Kind::Void) || tag.type->is(Type::Kind::Nil))
                fail("cannot switch on " + tag.type->name(), sw.tag->line, sw.tag->col);
        }

        std::vector<llvm::BasicBlock *> bodies;
        for (size_t i = 0; i < sw.cases.size(); ++i)
            bodies.push_back(llvm::BasicBlock::Create(ctx_, "case.body", f));
        auto *defaultBB = sw.hasDefault ? llvm::BasicBlock::Create(ctx_, "switch.default", f) : endBB;

        // Test the cases one after another; the first match wins.
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
                auto *next = llvm::BasicBlock::Create(ctx_, "case.next", f);
                builder_.CreateCondBr(cond.value, bodies[i], next);
                builder_.SetInsertPoint(next);
            }
        }
        builder_.CreateBr(defaultBB);

        loops_.push_back({nullptr, endBB});
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
    }

}

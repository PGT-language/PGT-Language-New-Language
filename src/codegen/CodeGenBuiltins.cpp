#include "codegen/CodeGen.h"

#include "common/Error.h"

namespace pgt
{

    llvm::FunctionCallee CodeGen::rtDecl(const std::string &name, llvm::Type *ret,
                                         std::vector<llvm::Type *> params, bool noReturn)
    {
        if (llvm::Function *existing = module_->getFunction(name))
            return {existing->getFunctionType(), existing};
        auto *fnTy = llvm::FunctionType::get(ret, params, false);
        auto *fn = llvm::Function::Create(fnTy, llvm::Function::ExternalLinkage, name, module_.get());
        for (unsigned i = 0; i < params.size(); ++i)
            if (params[i]->isIntegerTy(1) || params[i]->isIntegerTy(8))
                fn->addParamAttr(i, llvm::Attribute::ZExt);
        if (ret->isIntegerTy(1) || ret->isIntegerTy(8))
            fn->addRetAttr(llvm::Attribute::ZExt);
        if (noReturn)
            fn->addFnAttr(llvm::Attribute::NoReturn);
        return {fnTy, fn};
    }

    llvm::Value *CodeGen::rt(const std::string &name, llvm::Type *ret, std::vector<llvm::Value *> args,
                             bool noReturn)
    {
        std::vector<llvm::Type *> params;
        for (llvm::Value *a : args)
            params.push_back(a->getType());
        return builder_.CreateCall(rtDecl(name, ret, params, noReturn), args);
    }

    void CodeGen::panicIf(llvm::Value *cond, const std::string &fn, std::vector<llvm::Value *> args)
    {
        llvm::Function *f = builder_.GetInsertBlock()->getParent();
        auto *bad = llvm::BasicBlock::Create(ctx_, "panic", f);
        auto *ok = llvm::BasicBlock::Create(ctx_, "ok", f);
        builder_.CreateCondBr(cond, bad, ok);
        builder_.SetInsertPoint(bad);
        rt(fn, builder_.getVoidTy(), args, true);
        builder_.CreateUnreachable();
        builder_.SetInsertPoint(ok);
    }

    llvm::Value *CodeGen::arrayToPtr(llvm::Value *arr)
    {
        llvm::Value *tmp = entryAlloca(arrayTy_, "arr.tmp");
        builder_.CreateStore(arr, tmp);
        return tmp;
    }

    CodeGen::Value CodeGen::genConversion(const Call &call, TyP target)
    {
        if (call.args.size() != 1)
            fail("the conversion '" + target->name() + "' takes exactly one argument", call.line,
                 call.col);
        Value v = genExpr(*call.args[0], target->isNumeric() ? target : nullptr);
        return convert(v, target, call.line, call.col);
    }

    CodeGen::Value CodeGen::genMath(const Call &call, const std::string &name)
    {
        size_t arity = name == "pow" ? 2 : 1;
        if (call.args.size() != arity)
            fail("'" + name + "' takes " + std::to_string(arity) + " argument(s)", call.line, call.col);
        std::vector<llvm::Value *> args;
        for (const ExprPtr &a : call.args)
        {
            Value v = genExpr(*a, types_.floatT);
            if (v.type->isIntegral())
                v = convert(v, types_.floatT, a->line, a->col);
            if (!v.type->is(Type::Kind::Float))
                fail("'" + name + "' needs a number, found " + v.type->name(), a->line, a->col);
            args.push_back(v.value);
        }
        return {rt(name, builder_.getDoubleTy(), args), types_.floatT};
    }

    std::optional<CodeGen::Value> CodeGen::genBuiltin(const Call &call, const std::string &name, TyP hint)
    {
        auto *ptr = builder_.getPtrTy();
        auto *i32 = builder_.getInt32Ty();
        auto *i64 = builder_.getInt64Ty();
        auto *i1 = builder_.getInt1Ty();
        auto voidV = Value{nullptr, types_.voidT};

        auto arity = [&](size_t n)
        {
            if (call.args.size() != n)
                fail("'" + name + "' takes " + std::to_string(n) + " argument(s), got " +
                         std::to_string(call.args.size()),
                     call.line, call.col);
            if (call.typeArg && name != "make" && name != "new")
                fail("'" + name + "' does not take a type argument", call.line, call.col);
        };
        auto argOf = [&](size_t i, TyP want) -> llvm::Value *
        {
            const Expr &a = *call.args[i];
            Value v = genExpr(a, want);
            return coerce(v, want, a.line, a.col,
                          "argument " + std::to_string(i + 1) + " of '" + name + "'")
                .value;
        };
        auto valOf = [&](size_t i, TyP want) -> Value
        {
            const Expr &a = *call.args[i];
            return coerce(genExpr(a, want), want, a.line, a.col,
                          "argument " + std::to_string(i + 1) + " of '" + name + "'");
        };
        auto str = [&](size_t i)
        { return argOf(i, types_.stringT); };

        if (name == "len")
        {
            arity(1);
            Value v = genExpr(*call.args[0]);
            if (v.type->is(Type::Kind::Array))
                return Value{builder_.CreateTrunc(builder_.CreateExtractValue(v.value, 1), i32),
                             types_.intT};
            if (v.type->is(Type::Kind::String))
                return Value{rt("pgt_strlen", i32, {v.value}), types_.intT};
            fail("'len' needs an array or a string, found " + v.type->name(), call.line, call.col);
        }
        if (name == "append")
        {
            if (call.args.empty())
                fail("'append' needs an array to append to", call.line, call.col);
            const Expr &first = *call.args[0];
            Value arr = genExpr(first, hint);
            if (!arr.type->is(Type::Kind::Array))
                fail("the first argument of 'append' must be an array, found " + arr.type->name(),
                     first.line, first.col);
            TyP elemTy = arr.type->elem;
            std::vector<Value> items;
            for (size_t i = 1; i < call.args.size(); ++i)
                items.push_back(valOf(i, elemTy));

            llvm::Value *tmp = arrayToPtr(arr.value);
            llvm::Value *len = builder_.CreateExtractValue(arr.value, 1);
            llvm::Value *newLen = builder_.CreateAdd(len, builder_.getInt64(items.size()));
            rt("pgt_array_reserve", builder_.getVoidTy(),
               {tmp, newLen, sizeOf(elemTy), dropFn(elemTy), retainFn(elemTy)});
            llvm::Value *grown = builder_.CreateLoad(arrayTy_, tmp);
            llvm::Value *data = builder_.CreateExtractValue(grown, 0);
            for (size_t i = 0; i < items.size(); ++i)
            {
                llvm::Value *at = builder_.CreateAdd(len, builder_.getInt64(i));
                storeValue(builder_.CreateGEP(llvmType(elemTy), data, at), elemTy, items[i], false);
            }
            return owned(Value{builder_.CreateInsertValue(grown, newLen, 1), arr.type});
        }
        if (name == "make")
        {
            if (!call.typeArg)
                fail("'make' needs a type: make([]int, n)", call.line, call.col);
            TyP t = resolveType(*call.typeArg);
            if (!t->is(Type::Kind::Array))
                fail("'make' creates arrays; use new(T) for a pointer", call.line, call.col);
            if (call.args.empty() || call.args.size() > 2)
                fail("'make' takes a length and an optional capacity: make([]T, len, cap)", call.line,
                     call.col);
            llvm::Value *len = builder_.CreateSExt(argOf(0, types_.intT), i64);
            llvm::Value *cap = call.args.size() == 2 ? builder_.CreateSExt(argOf(1, types_.intT), i64) : len;
            llvm::Value *tmp = entryAlloca(arrayTy_, "make.tmp");
            rt("pgt_array_make", builder_.getVoidTy(), {tmp, len, cap, sizeOf(t->elem), dropFn(t->elem)});
            return owned(Value{builder_.CreateLoad(arrayTy_, tmp), t});
        }
        if (name == "new")
        {
            if (!call.typeArg)
                fail("'new' needs a type: new(Point)", call.line, call.col);
            if (!call.args.empty())
                fail("'new' takes only a type", call.line, call.col);
            TyP t = resolveType(*call.typeArg);
            return owned(Value{rt("pgt_alloc", ptr, {sizeOf(t), dropFn(t)}), types_.pointerTo(t)});
        }
        if (name == "free")
        {
            arity(1);
            if (auto *ref = dynamic_cast<const VarRef *>(call.args[0].get()))
            {
                if (Var *var = lookupLocal(ref->name); var && var->isManual)
                {
                    llvm::Value *cur = builder_.CreateLoad(arrayTy_, var->addr);
                    rt("pgt_manual_free", builder_.getVoidTy(), {builder_.CreateExtractValue(cur, 0)});
                    builder_.CreateStore(zeroValue(var->type), var->addr);
                    return voidV;
                }
            }
            Value v;
            std::optional<Addr> where = addressOf(*call.args[0], false);
            if (where)
                v = Value{builder_.CreateLoad(llvmType(where->type), where->addr), where->type};
            else
                v = genExpr(*call.args[0]);
            if (v.type->is(Type::Kind::Pointer))
                rt("pgt_free", builder_.getVoidTy(), {v.value});
            else if (v.type->is(Type::Kind::Array))
                rt("pgt_free", builder_.getVoidTy(), {builder_.CreateExtractValue(v.value, 0)});
            else
                fail("'free' needs a pointer or an array, found " + v.type->name(), call.line, call.col);
            if (where && !where->isConst)
                builder_.CreateStore(zeroValue(v.type), where->addr);
            return voidV;
        }

        if (name == "str")
        {
            arity(1);
            return toStringValue(genExpr(*call.args[0]), call.line, call.col);
        }
        if (name == "chars")
        {
            arity(1);
            llvm::Value *s = str(0);
            llvm::Value *tmp = entryAlloca(arrayTy_, "chars.tmp");
            rt("pgt_to_chars", builder_.getVoidTy(), {tmp, s});
            return owned(Value{builder_.CreateLoad(arrayTy_, tmp), types_.arrayOf(types_.charT)});
        }
        if (name == "parseInt")
        {
            arity(1);
            return Value{rt("pgt_parse_int", i32, {str(0)}), types_.intT};
        }
        if (name == "parseInt64")
        {
            arity(1);
            return Value{rt("pgt_parse_int64", i64, {str(0)}), types_.int64T};
        }
        if (name == "parseFloat")
        {
            arity(1);
            return Value{rt("pgt_parse_float", builder_.getDoubleTy(), {str(0)}), types_.floatT};
        }
        if (name == "substr")
        {
            arity(3);
            llvm::Value *s = str(0);
            llvm::Value *from = argOf(1, types_.intT);
            llvm::Value *n = argOf(2, types_.intT);
            return owned(Value{rt("pgt_substr", ptr, {s, from, n}), types_.stringT});
        }
        if (name == "indexOf")
        {
            arity(2);
            llvm::Value *s = str(0);
            return Value{rt("pgt_index_of", i32, {s, str(1)}), types_.intT};
        }
        static const std::pair<const char *, const char *> stringPredicates[] = {
            {"contains", "pgt_contains"}, {"startsWith", "pgt_starts_with"}, {"endsWith", "pgt_ends_with"}};
        for (const auto &[builtin, fn] : stringPredicates)
        {
            if (name != builtin)
                continue;
            arity(2);
            llvm::Value *s = str(0);
            return Value{rt(fn, i1, {s, str(1)}), types_.boolT};
        }
        if (name == "upper" || name == "lower")
        {
            arity(1);
            return owned(Value{rt(name == "upper" ? "pgt_upper" : "pgt_lower", ptr, {str(0)}), types_.stringT});
        }

        if (name == "abs")
        {
            arity(1);
            Value v = genExpr(*call.args[0], hint);
            if (v.type->is(Type::Kind::Float))
                return Value{builder_.CreateSelect(
                                 builder_.CreateFCmpOLT(v.value, llvm::ConstantFP::get(v.value->getType(), 0)),
                                 builder_.CreateFNeg(v.value), v.value),
                             v.type};
            if (!v.type->is(Type::Kind::Int) && !v.type->is(Type::Kind::Int64))
                fail("'abs' needs a number, found " + v.type->name(), call.line, call.col);
            llvm::Value *zero = llvm::ConstantInt::get(v.value->getType(), 0);
            return Value{builder_.CreateSelect(builder_.CreateICmpSLT(v.value, zero),
                                               builder_.CreateNeg(v.value), v.value),
                         v.type};
        }
        if (name == "min" || name == "max")
        {
            arity(2);
            Value l, r;
            genOperands(*call.args[0], *call.args[1], hint, l, r);
            if (l.type != r.type || !(l.type->isNumeric() || l.type->is(Type::Kind::String)))
                fail("'" + name + "' needs two numbers of the same type, found " + l.type->name() +
                         " and " + r.type->name(),
                     call.line, call.col);
            Value less = compareValues(BinOp::Lt, l, r, call.line, call.col);
            return Value{name == "min" ? builder_.CreateSelect(less.value, l.value, r.value)
                                       : builder_.CreateSelect(less.value, r.value, l.value),
                         l.type};
        }
        static const char *mathFns[] = {"sqrt", "pow", "floor", "ceil", "round", "sin", "cos",
                                        "tan",  "log", "exp",   "trunc"};
        for (const char *m : mathFns)
            if (name == m)
                return genMath(call, name);

        if (name == "readFile")
        {
            arity(1);
            return owned(Value{rt("pgt_read_file", ptr, {str(0)}), types_.stringT});
        }
        if (name == "writeFile" || name == "appendFile")
        {
            arity(2);
            llvm::Value *path = str(0);
            return Value{rt(name == "writeFile" ? "pgt_write_file" : "pgt_append_file", i1,
                            {path, str(1)}),
                         types_.boolT};
        }
        if (name == "fileExists")
        {
            arity(1);
            return Value{rt("pgt_file_exists", i1, {str(0)}), types_.boolT};
        }

        if (name == "rand")
        {
            arity(0);
            return Value{rt("pgt_rand", i32, {}), types_.intT};
        }
        if (name == "srand")
        {
            arity(1);
            rt("pgt_srand", builder_.getVoidTy(), {argOf(0, types_.intT)});
            return voidV;
        }
        if (name == "exit")
        {
            arity(1);
            rt("pgt_exit", builder_.getVoidTy(), {argOf(0, types_.intT)}, true);
            builder_.CreateUnreachable();
            startDeadBlock();
            return voidV;
        }
        if (name == "panic")
        {
            arity(1);
            Value msg = toStringValue(genExpr(*call.args[0]), call.line, call.col);
            rt("pgt_panic", builder_.getVoidTy(), {msg.value}, true);
            builder_.CreateUnreachable();
            startDeadBlock();
            return voidV;
        }
        return std::nullopt;
    }

    void CodeGen::genPrint(const Call &call, bool newline)
    {
        std::vector<Value> values;
        for (const ExprPtr &a : call.args)
            values.push_back(genExpr(*a));
        for (const Value &v : values)
            printValue(v, true);
        if (newline)
            rt("pgt_print_char", builder_.getVoidTy(), {builder_.getInt8('\n')});
    }

    void CodeGen::printValue(Value v, bool topLevel)
    {
        auto *voidTy = builder_.getVoidTy();
        auto text = [&](const char *s)
        { rt("pgt_print_str", voidTy, {builder_.CreateGlobalString(s, ".str")}); };

        switch (v.type->kind)
        {
        case Type::Kind::Int:
            rt("pgt_print_int", voidTy, {v.value});
            return;
        case Type::Kind::Int64:
            rt("pgt_print_int64", voidTy, {v.value});
            return;
        case Type::Kind::Float:
            rt("pgt_print_float", voidTy, {v.value});
            return;
        case Type::Kind::Char:
            rt("pgt_print_char", voidTy, {v.value});
            return;
        case Type::Kind::Byte:
            rt("pgt_print_int", voidTy, {builder_.CreateZExt(v.value, builder_.getInt32Ty())});
            return;
        case Type::Kind::Bool:
            rt("pgt_print_bool", voidTy, {v.value});
            return;
        case Type::Kind::String:
            rt("pgt_print_str", voidTy, {v.value});
            return;
        case Type::Kind::Nil:
            text("<nil>");
            return;
        case Type::Kind::Void:
            return;
        case Type::Kind::Array:
        case Type::Kind::Struct:
            builder_.CreateCall(printFunction(v.type), {v.value});
            return;
        case Type::Kind::Pointer:
            if (topLevel && v.type->elem->is(Type::Kind::Struct))
            {
                llvm::Function *f = builder_.GetInsertBlock()->getParent();
                auto *nilBB = llvm::BasicBlock::Create(ctx_, "print.nil", f);
                auto *valBB = llvm::BasicBlock::Create(ctx_, "print.ptr", f);
                auto *endBB = llvm::BasicBlock::Create(ctx_, "print.end", f);
                builder_.CreateCondBr(builder_.CreateIsNull(v.value), nilBB, valBB);
                builder_.SetInsertPoint(nilBB);
                text("<nil>");
                builder_.CreateBr(endBB);
                builder_.SetInsertPoint(valBB);
                text("&");
                TyP st = v.type->elem;
                builder_.CreateCall(printFunction(st), {builder_.CreateLoad(llvmType(st), v.value)});
                builder_.CreateBr(endBB);
                builder_.SetInsertPoint(endBB);
                return;
            }
            rt("pgt_print_ptr", voidTy, {v.value});
            return;
        case Type::Kind::Interface:
        {
            llvm::Function *f = builder_.GetInsertBlock()->getParent();
            auto *nilBB = llvm::BasicBlock::Create(ctx_, "print.nil", f);
            auto *valBB = llvm::BasicBlock::Create(ctx_, "print.iface", f);
            auto *endBB = llvm::BasicBlock::Create(ctx_, "print.end", f);
            llvm::Value *data = builder_.CreateExtractValue(v.value, 0);
            llvm::Value *vt = builder_.CreateExtractValue(v.value, 1);
            builder_.CreateCondBr(builder_.CreateIsNull(vt), nilBB, valBB);
            builder_.SetInsertPoint(nilBB);
            text("<nil>");
            builder_.CreateBr(endBB);
            builder_.SetInsertPoint(valBB);
            auto *thunkTy = llvm::FunctionType::get(voidTy, {builder_.getPtrTy()}, false);
            builder_.CreateCall(thunkTy, builder_.CreateLoad(builder_.getPtrTy(), vt), {data});
            builder_.CreateBr(endBB);
            builder_.SetInsertPoint(endBB);
            return;
        }
        }
    }

    llvm::Function *CodeGen::printFunction(TyP t)
    {
        if (auto it = printFns_.find(t); it != printFns_.end())
            return it->second;
        auto *fnTy = llvm::FunctionType::get(builder_.getVoidTy(), {llvmType(t)}, false);
        auto *fn = llvm::Function::Create(fnTy, llvm::Function::InternalLinkage,
                                          "pgt.print." + t->name(), module_.get());
        printFns_[t] = fn;

        llvm::IRBuilderBase::InsertPointGuard guard(builder_);
        builder_.SetInsertPoint(llvm::BasicBlock::Create(ctx_, "entry", fn));
        auto text = [&](const char *s)
        { rt("pgt_print_str", builder_.getVoidTy(), {builder_.CreateGlobalString(s, ".str")}); };
        llvm::Value *arg = fn->getArg(0);

        if (t->is(Type::Kind::Struct))
        {
            text("{");
            const auto &fields = t->st->fields;
            for (size_t i = 0; i < fields.size(); ++i)
            {
                if (i)
                    text(" ");
                printValue({builder_.CreateExtractValue(arg, static_cast<unsigned>(i)), fields[i].type},
                           false);
            }
            text("}");
            builder_.CreateRetVoid();
            return fn;
        }

        TyP elem = t->elem;
        text("[");
        llvm::Value *data = builder_.CreateExtractValue(arg, 0);
        llvm::Value *len = builder_.CreateExtractValue(arg, 1);
        auto *idx = entryAlloca(builder_.getInt64Ty(), "i");
        builder_.CreateStore(builder_.getInt64(0), idx);
        auto *condBB = llvm::BasicBlock::Create(ctx_, "cond", fn);
        auto *bodyBB = llvm::BasicBlock::Create(ctx_, "body", fn);
        auto *sepBB = llvm::BasicBlock::Create(ctx_, "sep", fn);
        auto *elemBB = llvm::BasicBlock::Create(ctx_, "elem", fn);
        auto *endBB = llvm::BasicBlock::Create(ctx_, "end", fn);
        builder_.CreateBr(condBB);

        builder_.SetInsertPoint(condBB);
        llvm::Value *i = builder_.CreateLoad(builder_.getInt64Ty(), idx);
        builder_.CreateCondBr(builder_.CreateICmpSLT(i, len), bodyBB, endBB);

        builder_.SetInsertPoint(bodyBB);
        builder_.CreateCondBr(builder_.CreateICmpEQ(i, builder_.getInt64(0)), elemBB, sepBB);
        builder_.SetInsertPoint(sepBB);
        text(" ");
        builder_.CreateBr(elemBB);

        builder_.SetInsertPoint(elemBB);
        llvm::Value *p = builder_.CreateGEP(llvmType(elem), data, i);
        printValue({builder_.CreateLoad(llvmType(elem), p), elem}, false);
        builder_.CreateStore(builder_.CreateAdd(i, builder_.getInt64(1)), idx);
        builder_.CreateBr(condBB);

        builder_.SetInsertPoint(endBB);
        text("]");
        builder_.CreateRetVoid();
        return fn;
    }

    llvm::Function *CodeGen::printThunk(TyP concrete)
    {
        if (auto it = printThunks_.find(concrete); it != printThunks_.end())
            return it->second;
        auto *fnTy = llvm::FunctionType::get(builder_.getVoidTy(), {builder_.getPtrTy()}, false);
        auto *fn = llvm::Function::Create(fnTy, llvm::Function::InternalLinkage,
                                          "pgt.print.iface." + concrete->name(), module_.get());
        printThunks_[concrete] = fn;

        llvm::IRBuilderBase::InsertPointGuard guard(builder_);
        builder_.SetInsertPoint(llvm::BasicBlock::Create(ctx_, "entry", fn));
        llvm::Value *data = fn->getArg(0);
        if (concrete->is(Type::Kind::Pointer))
            printValue({data, concrete}, true);
        else
            printValue({builder_.CreateLoad(llvmType(concrete), data), concrete}, true);
        builder_.CreateRetVoid();
        return fn;
    }

    void CodeGen::genInput(const Call &call, bool wholeLine)
    {
        const std::string name = wholeLine ? "inputln" : "input";
        if (call.args.empty() || call.args.size() > 2)
            fail("'" + name + "' takes an optional prompt and a variable", call.line, call.col);

        if (call.args.size() == 2)
        {
            Value prompt = genExpr(*call.args.front());
            if (!prompt.type->is(Type::Kind::String))
                fail("the prompt of '" + name + "' must be a string", call.line, call.col);
            rt("pgt_print_str", builder_.getVoidTy(), {prompt.value});
        }

        const Expr &targetExpr = *call.args.back();
        Addr target = requireAddress(targetExpr, true);
        if (wholeLine && !target.type->is(Type::Kind::String))
            fail("'inputln' needs a string variable", call.line, call.col);

        const char *reader = nullptr;
        switch (target.type->kind)
        {
        case Type::Kind::Int:
            reader = "pgt_read_int";
            break;
        case Type::Kind::Int64:
            reader = "pgt_read_int64";
            break;
        case Type::Kind::Float:
            reader = "pgt_read_float";
            break;
        case Type::Kind::Char:
            reader = "pgt_read_char";
            break;
        case Type::Kind::Byte:
            fail("'" + name + "' cannot read a byte; read an int and convert it with byte(...)",
                 targetExpr.line, targetExpr.col);
        case Type::Kind::Bool:
            reader = "pgt_read_bool";
            break;
        case Type::Kind::String:
            reader = wholeLine ? "pgt_read_line" : "pgt_read_word";
            break;
        default:
            fail("'" + name + "' cannot read a value of type " + target.type->name(), targetExpr.line,
                 targetExpr.col);
        }
        llvm::Type *ty = llvmType(target.type);
        llvm::Value *cur = builder_.CreateLoad(ty, target.addr);
        llvm::Value *result = rt(reader, ty, {cur});
        builder_.CreateStore(result, target.addr);
        if (target.type->is(Type::Kind::String))
            emitRelease(cur, target.type);
    }

}

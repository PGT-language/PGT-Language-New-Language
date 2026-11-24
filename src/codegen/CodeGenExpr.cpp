#include "codegen/CodeGen.h"

#include "common/Error.h"

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
                return (un->op == UnOp::Neg || un->op == UnOp::BitNot) && isConstExpr(*un->operand);
            if (auto *bin = dynamic_cast<const Binary *>(&e))
            {
                switch (bin->op)
                {
                case BinOp::Add:
                case BinOp::Sub:
                case BinOp::Mul:
                case BinOp::Div:
                case BinOp::Mod:
                case BinOp::BitAnd:
                case BinOp::BitOr:
                case BinOp::BitXor:
                case BinOp::Shl:
                case BinOp::Shr:
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

        std::string ordinal(size_t i)
        {
            return std::to_string(i);
        }

    }

    CodeGen::Value CodeGen::genExpr(const Expr &expr, TyP hint)
    {
        if (hint && hint->is(Type::Kind::Any))
            hint = nullptr;
        if (auto *lit = dynamic_cast<const IntLit *>(&expr))
        {
            TyP t = types_.intT;
            if (hint && (hint->is(Type::Kind::Int64) || hint->is(Type::Kind::Float) ||
                         hint->is(Type::Kind::Char) || hint->is(Type::Kind::Byte)))
                t = hint;
            switch (t->kind)
            {
            case Type::Kind::Int64:
                return {builder_.getInt64(static_cast<uint64_t>(lit->value)), t};
            case Type::Kind::Float:
                return {llvm::ConstantFP::get(builder_.getDoubleTy(), static_cast<double>(lit->value)),
                        t};
            case Type::Kind::Char:
            case Type::Kind::Byte:
                if (lit->value > 255)
                    fail("constant " + std::to_string(lit->value) + " overflows " + t->name(),
                         lit->line, lit->col);
                return {builder_.getInt8(static_cast<uint8_t>(lit->value)), t};
            default:
                if (lit->value > INT_MAX)
                    fail("integer literal does not fit into 'int' (32 bits); use int64", lit->line,
                         lit->col);
                return {builder_.getInt32(static_cast<uint32_t>(lit->value)), types_.intT};
            }
        }
        if (auto *lit = dynamic_cast<const FloatLit *>(&expr))
            return {llvm::ConstantFP::get(builder_.getDoubleTy(), lit->value), types_.floatT};
        if (auto *lit = dynamic_cast<const CharLit *>(&expr))
            return {builder_.getInt8(static_cast<uint8_t>(lit->value)), types_.charT};
        if (auto *lit = dynamic_cast<const BoolLit *>(&expr))
            return {builder_.getInt1(lit->value), types_.boolT};
        if (auto *lit = dynamic_cast<const StringLit *>(&expr))
            return {builder_.CreateGlobalString(lit->value, ".str"), types_.stringT};
        if (dynamic_cast<const NilLit *>(&expr))
            return {llvm::ConstantPointerNull::get(builder_.getPtrTy()), types_.nilT};

        if (auto *ref = dynamic_cast<const VarRef *>(&expr))
        {
            if (auto var = lookupVar(ref->name))
                return {builder_.CreateLoad(llvmType(var->type), var->addr, ref->name), var->type};
            if (lookupPackage(ref->name))
                fail("package '" + ref->name + "' is not a value; use " + ref->name + ".Name",
                     ref->line, ref->col);
            if (pkg_->functions.count(ref->name))
                fail("function '" + ref->name + "' is not a value; call it: " + ref->name + "(...)",
                     ref->line, ref->col);
            fail("undefined variable '" + ref->name + "'", ref->line, ref->col);
        }
        if (auto *un = dynamic_cast<const Unary *>(&expr))
            return genUnary(*un, hint);
        if (auto *bin = dynamic_cast<const Binary *>(&expr))
            return genBinary(*bin, hint);
        if (auto *call = dynamic_cast<const Call *>(&expr))
        {
            Value v = genCall(*call, hint);
            if (v.type->is(Type::Kind::Void))
                fail("this call does not return a value", call->line, call->col);
            return v;
        }
        if (auto *sel = dynamic_cast<const Selector *>(&expr))
            return genSelector(*sel);
        if (auto *idx = dynamic_cast<const Index *>(&expr))
            return genIndex(*idx);
        if (auto *lit = dynamic_cast<const ArrayLit *>(&expr))
            return genArrayLit(*lit, hint);
        if (auto *lit = dynamic_cast<const StructLit *>(&expr))
            return genStructLit(*lit);

        fail("internal error: unknown expression", expr.line, expr.col);
    }

    CodeGen::Value CodeGen::genCond(const Expr &expr)
    {
        Value v = genExpr(expr);
        if (!v.type->is(Type::Kind::Bool))
            fail("condition must be bool, found " + v.type->name(), expr.line, expr.col);
        return v;
    }

    CodeGen::Value CodeGen::coerce(Value v, TyP to, int line, int col, const std::string &what)
    {
        if (v.type == to)
            return v;
        if (v.type->is(Type::Kind::Nil) && to->isNillable())
            return {zeroValue(to), to};
        if (to->is(Type::Kind::Interface) && !v.type->is(Type::Kind::Void) &&
            !v.type->is(Type::Kind::Nil) && !v.type->is(Type::Kind::Interface) &&
            !v.type->is(Type::Kind::Any))
            return makeIface(v, to, line, col);
        if (to->is(Type::Kind::Any) && !v.type->is(Type::Kind::Void))
            return makeAny(v);
        if (v.type->is(Type::Kind::Void))
            fail("a call that returns nothing cannot be used in " + what, line, col);
        fail("cannot use " + v.type->name() + " as " + to->name() + " in " + what, line, col);
    }

    CodeGen::Value CodeGen::convert(Value v, TyP to, int line, int col)
    {
        if (v.type == to)
            return v;
        TyP from = v.type;
        if (to->is(Type::Kind::String))
            return toStringValue(v, line, col);
        if (from->isIntegral() && to->isIntegral())
            return {builder_.CreateIntCast(v.value, llvmType(to), !from->isUnsigned()), to};
        if (from->isIntegral() && to->is(Type::Kind::Float))
            return {from->isUnsigned() ? builder_.CreateUIToFP(v.value, llvmType(to))
                                               : builder_.CreateSIToFP(v.value, llvmType(to)),
                    to};
        if (from->is(Type::Kind::Float) && to->isIntegral())
            return {to->isUnsigned() ? builder_.CreateFPToUI(v.value, llvmType(to))
                                             : builder_.CreateFPToSI(v.value, llvmType(to)),
                    to};
        if (from->is(Type::Kind::Bool) && to->isIntegral())
            return {builder_.CreateZExt(v.value, llvmType(to)), to};
        if (from->isIntegral() && to->is(Type::Kind::Bool))
            return {builder_.CreateICmpNE(v.value, llvm::ConstantInt::get(llvmType(from), 0)), to};
        if (from->is(Type::Kind::Float) && to->is(Type::Kind::Bool))
            return {builder_.CreateFCmpUNE(v.value, llvm::ConstantFP::get(llvmType(from), 0.0)), to};
        if (from->is(Type::Kind::Bool) && to->is(Type::Kind::Float))
            return {builder_.CreateUIToFP(v.value, llvmType(to)), to};
        if (from->is(Type::Kind::String) && to->isNumeric())
            fail("cannot convert string to " + to->name() + "; use parseInt or parseFloat", line, col);
        fail("cannot convert " + from->name() + " to " + to->name(), line, col);
    }

    CodeGen::Value CodeGen::toStringValue(Value v, int line, int col)
    {
        auto *ptr = builder_.getPtrTy();
        switch (v.type->kind)
        {
        case Type::Kind::String:
            return v;
        case Type::Kind::Int:
            return owned({rt("pgt_str_int", ptr, {v.value}), types_.stringT});
        case Type::Kind::Int64:
            return owned({rt("pgt_str_int64", ptr, {v.value}), types_.stringT});
        case Type::Kind::Float:
            return owned({rt("pgt_str_float", ptr, {v.value}), types_.stringT});
        case Type::Kind::Char:
            return owned({rt("pgt_str_char", ptr, {v.value}), types_.stringT});
        case Type::Kind::Byte:
            return owned({rt("pgt_str_int", ptr, {builder_.CreateZExt(v.value, builder_.getInt32Ty())}),
                          types_.stringT});
        case Type::Kind::Bool:
            return owned({rt("pgt_str_bool", ptr, {v.value}), types_.stringT});
        case Type::Kind::Array:
            if (v.type->elem->is(Type::Kind::Char))
                return owned({rt("pgt_from_chars", ptr, {arrayToPtr(v.value)}), types_.stringT});
            break;
        default:
            break;
        }
        fail("cannot convert " + v.type->name() + " to string", line, col);
    }

    void CodeGen::nilCheck(llvm::Value *ptr)
    {
        panicIf(builder_.CreateIsNull(ptr), "panicNil", {});
    }

    void CodeGen::genOperands(const Expr &lhs, const Expr &rhs, TyP hint, Value &l, Value &r)
    {
        bool lc = isConstExpr(lhs), rc = isConstExpr(rhs);
        if (lc && rc)
        {
            TyP h = hint;
            if (!h && (hasFloatLit(lhs) || hasFloatLit(rhs)))
                h = types_.floatT;
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
            r = genExpr(rhs, l.type);
        }
    }

    CodeGen::Value CodeGen::genUnary(const Unary &un, TyP hint)
    {
        switch (un.op)
        {
        case UnOp::Neg:
        {
            Value v = genExpr(*un.operand, hint);
            if (v.type->is(Type::Kind::Float))
                return {builder_.CreateFNeg(v.value), v.type};
            if (!v.type->is(Type::Kind::Int) && !v.type->is(Type::Kind::Int64))
                fail("unary '-' is not defined for " + v.type->name(), un.line, un.col);
            return {builder_.CreateNeg(v.value), v.type};
        }
        case UnOp::Not:
        {
            Value v = genExpr(*un.operand);
            if (!v.type->is(Type::Kind::Bool))
                fail("'!' needs a bool operand", un.line, un.col);
            return {builder_.CreateNot(v.value), v.type};
        }
        case UnOp::BitNot:
        {
            Value v = genExpr(*un.operand, hint);
            if (!v.type->isIntegral())
                fail("'~' needs an integer operand", un.line, un.col);
            return {builder_.CreateNot(v.value), v.type};
        }
        case UnOp::Deref:
        {
            Value v = genExpr(*un.operand);
            if (!v.type->is(Type::Kind::Pointer))
                fail("cannot dereference " + v.type->name() + " (it is not a pointer)", un.line,
                     un.col);
            nilCheck(v.value);
            return {builder_.CreateLoad(llvmType(v.type->elem), v.value), v.type->elem};
        }
        case UnOp::AddrOf:
        {
            if (auto *lit = dynamic_cast<const StructLit *>(un.operand.get()))
            {
                Value v = genStructLit(*lit);
                llvm::Value *fields = take(v);
                llvm::Value *p = rt("pgt_alloc", builder_.getPtrTy(), {sizeOf(v.type), dropFn(v.type)});
                builder_.CreateStore(fields, p);
                return owned({p, types_.pointerTo(v.type)});
            }
            Addr a = requireAddress(*un.operand, true);
            return {a.addr, types_.pointerTo(a.type)};
        }
        }
        fail("internal error: unknown unary operator", un.line, un.col);
    }

    CodeGen::Value CodeGen::genLogical(const Binary &bin)
    {
        llvm::Function *f = builder_.GetInsertBlock()->getParent();
        bool isAnd = bin.op == BinOp::And;
        auto needBool = [&]
        { fail(std::string("operator '") + opText(bin.op) + "' needs bool operands", bin.line, bin.col); };

        Value lhs = genExpr(*bin.lhs);
        if (!lhs.type->is(Type::Kind::Bool))
            needBool();
        llvm::BasicBlock *lhsEnd = builder_.GetInsertBlock();
        auto *rhsBB = llvm::BasicBlock::Create(ctx_, isAnd ? "and.rhs" : "or.rhs", f);
        auto *endBB = llvm::BasicBlock::Create(ctx_, isAnd ? "and.end" : "or.end", f);
        if (isAnd)
            builder_.CreateCondBr(lhs.value, rhsBB, endBB);
        else
            builder_.CreateCondBr(lhs.value, endBB, rhsBB);

        builder_.SetInsertPoint(rhsBB);
        size_t mark = temps_.size();
        Value rhs = genExpr(*bin.rhs);
        if (!rhs.type->is(Type::Kind::Bool))
            needBool();
        flushTemps(mark);
        llvm::BasicBlock *rhsEnd = builder_.GetInsertBlock();
        builder_.CreateBr(endBB);

        builder_.SetInsertPoint(endBB);
        llvm::PHINode *phi = builder_.CreatePHI(builder_.getInt1Ty(), 2);
        phi->addIncoming(builder_.getInt1(!isAnd), lhsEnd);
        phi->addIncoming(rhs.value, rhsEnd);
        return {phi, types_.boolT};
    }

    CodeGen::Value CodeGen::genCompare(const Binary &bin)
    {
        Value l, r;
        genOperands(*bin.lhs, *bin.rhs, nullptr, l, r);
        return compareValues(bin.op, l, r, bin.line, bin.col);
    }

    CodeGen::Value CodeGen::compareValues(BinOp op, Value l, Value r, int line, int col)
    {
        bool equality = op == BinOp::Eq || op == BinOp::Ne;
        auto result = [&](llvm::Value *isEqual)
        { return Value{op == BinOp::Eq ? isEqual : builder_.CreateNot(isEqual), types_.boolT}; };

        if (l.type->is(Type::Kind::Nil) || r.type->is(Type::Kind::Nil))
        {
            Value v = l.type->is(Type::Kind::Nil) ? r : l;
            if (!equality)
                fail(std::string("operator '") + opText(op) + "' cannot be used with nil", line, col);
            switch (v.type->kind)
            {
            case Type::Kind::Nil:
                return result(builder_.getInt1(true));
            case Type::Kind::Pointer:
                return result(builder_.CreateIsNull(v.value));
            case Type::Kind::String:
                return result(builder_.CreateICmpEQ(rt("pgt_strlen", builder_.getInt32Ty(), {v.value}),
                                                    builder_.getInt32(0)));
            case Type::Kind::Array:
                return result(builder_.CreateICmpEQ(builder_.CreateExtractValue(v.value, 1),
                                                    builder_.getInt64(0)));
            case Type::Kind::Interface:
            case Type::Kind::Any:
                return result(builder_.CreateIsNull(builder_.CreateExtractValue(v.value, 1)));
            default:
                fail("cannot compare " + v.type->name() + " with nil", line, col);
            }
        }

        if (l.type != r.type)
            fail("cannot compare " + l.type->name() + " with " + r.type->name() +
                     " (use an explicit conversion)",
                 line, col);
        TyP t = l.type;

        if (t->is(Type::Kind::String))
        {
            llvm::Value *c = rt("pgt_strcmp", builder_.getInt32Ty(), {l.value, r.value});
            llvm::Value *zero = builder_.getInt32(0);
            llvm::Value *res = nullptr;
            switch (op)
            {
            case BinOp::Eq:
                res = builder_.CreateICmpEQ(c, zero);
                break;
            case BinOp::Ne:
                res = builder_.CreateICmpNE(c, zero);
                break;
            case BinOp::Lt:
                res = builder_.CreateICmpSLT(c, zero);
                break;
            case BinOp::Gt:
                res = builder_.CreateICmpSGT(c, zero);
                break;
            case BinOp::Le:
                res = builder_.CreateICmpSLE(c, zero);
                break;
            default:
                res = builder_.CreateICmpSGE(c, zero);
                break;
            }
            return {res, types_.boolT};
        }
        if (t->is(Type::Kind::Bool) || t->is(Type::Kind::Pointer))
        {
            if (!equality)
                fail(std::string("operator '") + opText(op) + "' cannot compare " + t->name() +
                         " values",
                     line, col);
            return result(builder_.CreateICmpEQ(l.value, r.value));
        }
        if (!t->isNumeric())
            fail(t->name() + " values can only be compared with nil", line, col);

        llvm::Value *res = nullptr;
        if (t->is(Type::Kind::Float))
        {
            switch (op)
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
            return {res, types_.boolT};
        }

        bool isSigned = !t->isUnsigned();
        switch (op)
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
        return {res, types_.boolT};
    }

    CodeGen::Value CodeGen::genArith(BinOp op, Value l, Value r, int line, int col)
    {
        if (op == BinOp::Add && (l.type->is(Type::Kind::String) || r.type->is(Type::Kind::String)))
        {
            Value ls = toStringValue(l, line, col);
            Value rs = toStringValue(r, line, col);
            return owned({rt("pgt_concat", builder_.getPtrTy(), {ls.value, rs.value}), types_.stringT});
        }

        if (op == BinOp::Shl || op == BinOp::Shr)
        {
            if (!l.type->isIntegral() || !r.type->isIntegral())
                fail(std::string("operator '") + opText(op) + "' needs integer operands", line, col);
            llvm::Value *amount = builder_.CreateIntCast(r.value, llvmType(l.type), false);
            if (op == BinOp::Shl)
                return {builder_.CreateShl(l.value, amount), l.type};
            return {l.type->isUnsigned() ? builder_.CreateLShr(l.value, amount)
                                                 : builder_.CreateAShr(l.value, amount),
                    l.type};
        }

        if (l.type != r.type)
            fail("mismatched types " + l.type->name() + " and " + r.type->name() + " for operator '" +
                     opText(op) + "' (use an explicit conversion)",
                 line, col);
        TyP t = l.type;

        if (op == BinOp::BitAnd || op == BinOp::BitOr || op == BinOp::BitXor)
        {
            if (!t->isIntegral())
                fail(std::string("operator '") + opText(op) + "' needs integer operands, found " +
                         t->name(),
                     line, col);
            if (op == BinOp::BitAnd)
                return {builder_.CreateAnd(l.value, r.value), t};
            if (op == BinOp::BitOr)
                return {builder_.CreateOr(l.value, r.value), t};
            return {builder_.CreateXor(l.value, r.value), t};
        }

        if (!t->isNumeric())
            fail(std::string("operator '") + opText(op) + "' is not defined for " + t->name(), line,
                 col);

        if (t->is(Type::Kind::Float))
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
                fail("operator '%' needs integer operands", line, col);
            }
        }

        bool isSigned = !t->isUnsigned();
        switch (op)
        {
        case BinOp::Add:
            return {builder_.CreateAdd(l.value, r.value), t};
        case BinOp::Sub:
            return {builder_.CreateSub(l.value, r.value), t};
        case BinOp::Mul:
            return {builder_.CreateMul(l.value, r.value), t};
        case BinOp::Div:
        case BinOp::Mod:
        {
            if (auto *c = llvm::dyn_cast<llvm::ConstantInt>(r.value); !c || c->isZero())
                panicIf(builder_.CreateICmpEQ(r.value, llvm::ConstantInt::get(llvmType(t), 0)),
                        "panicDiv", {});
            if (op == BinOp::Div)
                return {isSigned ? builder_.CreateSDiv(l.value, r.value)
                                 : builder_.CreateUDiv(l.value, r.value),
                        t};
            return {isSigned ? builder_.CreateSRem(l.value, r.value)
                             : builder_.CreateURem(l.value, r.value),
                    t};
        }
        default:
            break;
        }
        fail("internal error: unknown operator", line, col);
    }

    CodeGen::Value CodeGen::genBinary(const Binary &bin, TyP hint)
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
        case BinOp::Shl:
        case BinOp::Shr:
        {
            Value l = genExpr(*bin.lhs, hint);
            Value r = genExpr(*bin.rhs);
            return genArith(bin.op, l, r, bin.line, bin.col);
        }
        default:
            break;
        }

        Value l, r;
        genOperands(*bin.lhs, *bin.rhs, hint, l, r);
        return genArith(bin.op, l, r, bin.line, bin.col);
    }

    std::optional<CodeGen::Addr> CodeGen::packageMember(const Selector &sel)
    {
        auto *ref = dynamic_cast<const VarRef *>(sel.base.get());
        if (!ref || lookupVar(ref->name))
            return std::nullopt;
        PackageSyms *p = lookupPackage(ref->name);
        if (!p)
            return std::nullopt;
        std::string full = ref->name + "." + sel.name;
        if (!isPublic(sel.name))
            fail("'" + full + "' is not exported (lowercase names are private)", sel.line, sel.col);
        auto g = p->globals.find(sel.name);
        if (g == p->globals.end())
        {
            if (p->functions.count(sel.name))
                fail("function '" + full + "' is not a value; call it: " + full + "(...)", sel.line,
                     sel.col);
            fail("package '" + ref->name + "' has no constant or variable '" + sel.name + "'", sel.line,
                 sel.col);
        }
        return Addr{g->second.gv, g->second.type, g->second.isConst};
    }

    llvm::Value *CodeGen::elementAddr(Value arr, const Expr &index, int line, int col)
    {
        Value i = genExpr(index, types_.intT);
        if (!i.type->isIntegral())
            fail("an index must be an integer, found " + i.type->name(), index.line, index.col);
        llvm::Value *idx = builder_.CreateIntCast(i.value, builder_.getInt64Ty(),
                                                  !i.type->isUnsigned());
        llvm::Value *len = builder_.CreateExtractValue(arr.value, 1, "len");
        panicIf(builder_.CreateICmpUGE(idx, len), "panicIndex", {idx, len});
        llvm::Value *data = builder_.CreateExtractValue(arr.value, 0, "data");
        (void)line;
        (void)col;
        return builder_.CreateGEP(llvmType(arr.type->elem), data, idx);
    }

    std::optional<CodeGen::Addr> CodeGen::addressOf(const Expr &expr, bool forWrite)
    {
        if (auto *ref = dynamic_cast<const VarRef *>(&expr))
        {
            auto var = lookupVar(ref->name);
            if (!var)
                return std::nullopt;
            if (var->isConst && forWrite)
                fail("cannot modify the constant '" + ref->name + "'", ref->line, ref->col);
            return Addr{var->addr, var->type, var->isConst};
        }

        if (auto *sel = dynamic_cast<const Selector *>(&expr))
        {
            if (auto member = packageMember(*sel))
            {
                if (member->isConst && forWrite)
                    fail("cannot modify the constant '" + sel->name + "'", sel->line, sel->col);
                return member;
            }

            auto fieldOf = [&](llvm::Value *structPtr, TyP structTy, bool isConst) -> Addr
            {
                StructInfo *st = structTy->st;
                int i = st->fieldIndex(sel->name);
                if (i < 0)
                {
                    if (st->methods.count(sel->name))
                        fail("'" + sel->name + "' is a method of " + structTy->name() +
                                 "; call it: ." + sel->name + "(...)",
                             sel->line, sel->col);
                    fail(structTy->name() + " has no field '" + sel->name + "'", sel->line, sel->col);
                }
                if (st->pkgPath != pkg_->pkg->importPath && !isPublic(sel->name))
                    fail("field '" + sel->name + "' of " + structTy->name() +
                             " is not exported (lowercase names are private)",
                         sel->line, sel->col);
                return Addr{builder_.CreateStructGEP(st->llvm, structPtr, static_cast<unsigned>(i)),
                            st->fields[i].type, isConst};
            };
            auto noFields = [&](TyP t)
            { fail(t->name() + " has no fields (looking for '" + sel->name + "')", sel->line, sel->col); };

            if (auto base = addressOf(*sel->base, forWrite))
            {
                if (base->type->is(Type::Kind::Struct))
                    return fieldOf(base->addr, base->type, base->isConst);
                if (base->type->isPtrToStruct())
                {
                    llvm::Value *p = builder_.CreateLoad(builder_.getPtrTy(), base->addr);
                    nilCheck(p);
                    return fieldOf(p, base->type->elem, false);
                }
                noFields(base->type);
            }
            if (!forWrite)
                return std::nullopt;
            Value v = genExpr(*sel->base);
            if (v.type->isPtrToStruct())
            {
                nilCheck(v.value);
                return fieldOf(v.value, v.type->elem, false);
            }
            if (v.type->is(Type::Kind::Struct))
                fail("cannot modify a field of a temporary value", sel->line, sel->col);
            noFields(v.type);
        }

        if (auto *idx = dynamic_cast<const Index *>(&expr))
        {
            Value base;
            if (auto a = addressOf(*idx->base, false))
                base = {builder_.CreateLoad(llvmType(a->type), a->addr), a->type};
            else
                base = genExpr(*idx->base);
            if (base.type->is(Type::Kind::String))
                fail("strings are immutable; build a new string instead", idx->line, idx->col);
            if (!base.type->is(Type::Kind::Array))
                fail("cannot index " + base.type->name(), idx->line, idx->col);
            return Addr{elementAddr(base, *idx->index, idx->line, idx->col), base.type->elem, false};
        }

        if (auto *un = dynamic_cast<const Unary *>(&expr); un && un->op == UnOp::Deref)
        {
            Value v = genExpr(*un->operand);
            if (!v.type->is(Type::Kind::Pointer))
                fail("cannot dereference " + v.type->name() + " (it is not a pointer)", un->line,
                     un->col);
            nilCheck(v.value);
            return Addr{v.value, v.type->elem, false};
        }
        return std::nullopt;
    }

    CodeGen::Addr CodeGen::requireAddress(const Expr &expr, bool forWrite)
    {
        if (auto a = addressOf(expr, forWrite))
            return *a;
        if (auto *ref = dynamic_cast<const VarRef *>(&expr))
            genExpr(*ref);
        fail(forWrite ? "cannot assign to this expression" : "cannot take the address of this expression",
             expr.line, expr.col);
    }

    CodeGen::Value CodeGen::genSelector(const Selector &sel)
    {
        if (auto a = addressOf(sel, false))
            return {builder_.CreateLoad(llvmType(a->type), a->addr), a->type};

        Value v = genExpr(*sel.base);
        if (v.type->isPtrToStruct())
        {
            nilCheck(v.value);
            llvm::Value *tmp = v.value;
            StructInfo *st = v.type->elem->st;
            int i = st->fieldIndex(sel.name);
            if (i < 0)
                fail(v.type->elem->name() + " has no field '" + sel.name + "'", sel.line, sel.col);
            TyP ft = st->fields[i].type;
            return {builder_.CreateLoad(llvmType(ft),
                                        builder_.CreateStructGEP(st->llvm, tmp, static_cast<unsigned>(i))),
                    ft};
        }
        if (v.type->is(Type::Kind::Struct))
        {
            StructInfo *st = v.type->st;
            int i = st->fieldIndex(sel.name);
            if (i < 0)
                fail(v.type->name() + " has no field '" + sel.name + "'", sel.line, sel.col);
            if (st->pkgPath != pkg_->pkg->importPath && !isPublic(sel.name))
                fail("field '" + sel.name + "' of " + v.type->name() + " is not exported", sel.line,
                     sel.col);
            return {builder_.CreateExtractValue(v.value, static_cast<unsigned>(i)), st->fields[i].type};
        }
        fail(v.type->name() + " has no fields (looking for '" + sel.name + "')", sel.line, sel.col);
    }

    CodeGen::Value CodeGen::genIndex(const Index &idx)
    {
        Value base = genExpr(*idx.base);
        if (base.type->is(Type::Kind::Array))
        {
            llvm::Value *p = elementAddr(base, *idx.index, idx.line, idx.col);
            return {builder_.CreateLoad(llvmType(base.type->elem), p), base.type->elem};
        }
        if (base.type->is(Type::Kind::String))
        {
            Value i = genExpr(*idx.index, types_.intT);
            if (!i.type->isIntegral())
                fail("an index must be an integer, found " + i.type->name(), idx.index->line,
                     idx.index->col);
            llvm::Value *at = builder_.CreateIntCast(i.value, builder_.getInt64Ty(),
                                                     !i.type->isUnsigned());
            llvm::Value *len = builder_.CreateSExt(rt("pgt_strlen", builder_.getInt32Ty(), {base.value}),
                                                   builder_.getInt64Ty());
            panicIf(builder_.CreateICmpUGE(at, len), "panicIndex", {at, len});
            llvm::Value *p = builder_.CreateGEP(builder_.getInt8Ty(), base.value, at);
            return {builder_.CreateLoad(builder_.getInt8Ty(), p), types_.charT};
        }
        fail("cannot index " + base.type->name(), idx.line, idx.col);
    }

    CodeGen::Value CodeGen::genArrayLit(const ArrayLit &lit, TyP hint)
    {
        TyP elemTy = nullptr;
        std::vector<Value> values;
        if (hint && hint->is(Type::Kind::Array))
            elemTy = hint->elem;
        else if (lit.elems.empty())
            fail("cannot infer the type of an empty array; write it: var* a []int = []", lit.line,
                 lit.col);

        for (size_t i = 0; i < lit.elems.size(); ++i)
        {
            const Expr &e = *lit.elems[i];
            Value v = genExpr(e, elemTy);
            if (!elemTy)
            {
                if (v.type->is(Type::Kind::Nil) || v.type->is(Type::Kind::Void))
                    fail("cannot infer the element type of this array", e.line, e.col);
                elemTy = v.type;
            }
            values.push_back(coerce(v, elemTy, e.line, e.col, "an array element"));
        }

        return packArray(elemTy, values);
    }

    CodeGen::Value CodeGen::packArray(TyP elemTy, const std::vector<Value> &values)
    {
        TyP arrTy = types_.arrayOf(elemTy);
        if (values.empty())
            return {zeroValue(arrTy), arrTy};

        llvm::Type *elemLl = llvmType(elemTy);
        auto *n = builder_.getInt64(values.size());
        std::vector<llvm::Value *> items;
        for (const Value &v : values)
            items.push_back(take(v));
        llvm::Value *data = rt("pgt_alloc_n", builder_.getPtrTy(), {n, sizeOf(elemTy), dropFn(elemTy)});
        for (size_t i = 0; i < items.size(); ++i)
            builder_.CreateStore(items[i], builder_.CreateGEP(elemLl, data, builder_.getInt64(i)));
        llvm::Value *arr = llvm::UndefValue::get(arrayTy_);
        arr = builder_.CreateInsertValue(arr, data, 0);
        arr = builder_.CreateInsertValue(arr, n, 1);
        arr = builder_.CreateInsertValue(arr, n, 2);
        return owned({arr, arrTy});
    }

    CodeGen::Value CodeGen::genStructLit(const StructLit &lit)
    {
        TyP t = resolveType(lit.type);
        if (!t->is(Type::Kind::Struct))
            fail("'" + lit.type.text() + "' is not a struct", lit.line, lit.col);
        StructInfo *st = t->st;
        llvm::Value *agg = zeroValue(t);
        std::set<std::string> seen;
        for (const FieldInit &f : lit.fields)
        {
            int i = st->fieldIndex(f.name);
            if (i < 0)
                fail(t->name() + " has no field '" + f.name + "'", f.line, f.col);
            if (!seen.insert(f.name).second)
                fail("field '" + f.name + "' is set twice", f.line, f.col);
            if (st->pkgPath != pkg_->pkg->importPath && !isPublic(f.name))
                fail("field '" + f.name + "' of " + t->name() + " is not exported", f.line, f.col);
            TyP ft = st->fields[i].type;
            Value v = coerce(genExpr(*f.value, ft), ft, f.line, f.col, "field '" + f.name + "'");
            agg = builder_.CreateInsertValue(agg, take(v), static_cast<unsigned>(i));
        }
        return owned({agg, t});
    }

    CodeGen::Value CodeGen::callFunction(const FuncInfo &info, const std::string &name, const Call &call,
                                         std::vector<llvm::Value *> args, size_t firstParam)
    {
        size_t fixed = info.params.size() - firstParam - (info.variadic ? 1 : 0);
        if (call.typeArg)
            fail("'" + name + "' does not take a type argument", call.line, call.col);
        if (call.spread && !info.variadic)
            fail("cannot use '...' when calling '" + name + "': it has no variadic parameter", call.line,
                 call.col);
        if (info.variadic)
        {
            if (call.args.size() < fixed)
                fail("'" + name + "' takes at least " + std::to_string(fixed) + " argument(s), got " +
                         std::to_string(call.args.size()),
                     call.line, call.col);
            if (call.spread && call.args.size() != fixed + 1)
                fail("'...' must be applied to the argument for the variadic parameter of '" + name + "'",
                     call.line, call.col);
        }
        else if (call.args.size() != fixed)
            fail("'" + name + "' takes " + std::to_string(fixed) + " argument(s), got " +
                     std::to_string(call.args.size()),
                 call.line, call.col);

        for (size_t i = 0; i < fixed; ++i)
        {
            TyP pt = info.params[firstParam + i];
            const Expr &a = *call.args[i];
            Value v = coerce(genExpr(a, pt), pt, a.line, a.col,
                             "argument " + ordinal(i + 1) + " of '" + name + "'");
            args.push_back(info.isExtern && pt->is(Type::Kind::Array) ? arrayToPtr(v.value) : v.value);
        }
        if (info.variadic)
        {
            TyP arrTy = info.params.back();
            if (call.spread)
            {
                const Expr &a = *call.args[fixed];
                Value v = coerce(genExpr(a, arrTy), arrTy, a.line, a.col,
                                 "argument " + ordinal(fixed + 1) + " of '" + name + "'");
                args.push_back(v.value);
            }
            else
            {
                std::vector<Value> extra;
                for (size_t i = fixed; i < call.args.size(); ++i)
                {
                    const Expr &a = *call.args[i];
                    extra.push_back(coerce(genExpr(a, arrTy->elem), arrTy->elem, a.line, a.col,
                                           "argument " + ordinal(i + 1) + " of '" + name + "'"));
                }
                args.push_back(packArray(arrTy->elem, extra).value);
            }
        }
        llvm::Value *result = builder_.CreateCall(info.fn, args);
        if (info.ret->is(Type::Kind::Void))
            return {nullptr, info.ret};
        return owned({result, info.ret});
    }

    CodeGen::Value CodeGen::genCall(const Call &call, TyP hint)
    {
        if (auto *ref = dynamic_cast<const VarRef *>(call.callee.get()))
        {
            const std::string &name = ref->name;
            if (auto it = pkg_->functions.find(name); it != pkg_->functions.end())
                return callFunction(it->second, name, call, {}, 0);
            if (call.spread)
                fail("'...' can only be used when calling a variadic function", call.line, call.col);

            static const std::pair<const char *, int> conversions[] = {
                {"int", 0}, {"int64", 1}, {"float", 2}, {"char", 3}, {"bool", 4}, {"string", 5}, {"byte", 6}};
            for (const auto &[conv, k] : conversions)
            {
                if (name != conv)
                    continue;
                TyP targets[] = {types_.intT,  types_.int64T, types_.floatT,
                                 types_.charT, types_.boolT,  types_.stringT, types_.byteT};
                return genConversion(call, targets[k]);
            }
            if (auto v = genBuiltin(call, name, hint))
                return *v;
            if (lookupVar(name))
                fail("'" + name + "' is a variable, not a function", call.line, call.col);
            if (pkg_->structs.count(name))
                fail("'" + name + "' is a struct; create it with " + name + "{Field: value}", call.line,
                     call.col);
            static const std::map<std::string, std::string> removed = {
                {"print", "fmt.Print"},
                {"println", "fmt.Println"},
                {"input", "fmt.Input"},
                {"inputln", "fmt.InputLine"},
                {"output", "fmt.Print"}};
            if (auto it = removed.find(name); it != removed.end())
                fail("undefined: " + name + " (use " + it->second + " from #use \"fmt\")", call.line,
                     call.col);
            fail("call to undefined function '" + name + "'", call.line, call.col);
        }

        if (auto *sel = dynamic_cast<const Selector *>(call.callee.get()))
        {
            auto *ref = dynamic_cast<const VarRef *>(sel->base.get());
            if (ref && !lookupVar(ref->name))
            {
                if (PackageSyms *p = lookupPackage(ref->name))
                {
                    std::string full = ref->name + "." + sel->name;
                    if (!isPublic(sel->name))
                        fail("'" + full + "' is not exported (lowercase names are private)", call.line,
                             call.col);
                    auto it = p->functions.find(sel->name);
                    if (it == p->functions.end())
                        fail("package '" + ref->name + "' has no function '" + sel->name + "'",
                             call.line, call.col);
                    return callFunction(it->second, full, call, {}, 0);
                }
            }
            return genMethodCall(call, *sel);
        }
        fail("this expression cannot be called", call.line, call.col);
    }

    CodeGen::Value CodeGen::genMethodCall(const Call &call, const Selector &sel)
    {
        Value recvVal;
        std::optional<Addr> recvAddr = addressOf(*sel.base, false);
        TyP t;
        if (recvAddr)
            t = recvAddr->type;
        else
        {
            recvVal = genExpr(*sel.base);
            t = recvVal.type;
        }

        if (t->is(Type::Kind::Interface))
        {
            Value v = recvAddr ? Value{builder_.CreateLoad(ifaceTy_, recvAddr->addr), t} : recvVal;
            return genIfaceCall(v, call, sel.name);
        }

        TyP structTy = t->is(Type::Kind::Struct) ? t : t->isPtrToStruct() ? t->elem : nullptr;
        if (!structTy)
            fail(t->name() + " has no methods (calling '" + sel.name + "')", sel.line, sel.col);
        StructInfo *st = structTy->st;
        auto it = st->methods.find(sel.name);
        if (it == st->methods.end())
        {
            if (st->fieldIndex(sel.name) >= 0)
                fail("'" + sel.name + "' is a field of " + structTy->name() + ", not a method", sel.line,
                     sel.col);
            fail(structTy->name() + " has no method '" + sel.name + "'", sel.line, sel.col);
        }
        const FuncInfo &m = it->second;
        if (st->pkgPath != pkg_->pkg->importPath && !isPublic(sel.name))
            fail("method '" + sel.name + "' of " + structTy->name() +
                     " is not exported (lowercase names are private)",
                 sel.line, sel.col);

        llvm::Value *recv = nullptr;
        if (t->is(Type::Kind::Struct))
        {
            if (m.ptrRecv)
            {
                if (recvAddr && recvAddr->isConst)
                    fail("cannot call the pointer method '" + sel.name + "' on a constant", sel.line,
                         sel.col);
                if (recvAddr)
                    recv = recvAddr->addr;
                else
                {
                    recv = entryAlloca(st->llvm, "recv");
                    builder_.CreateStore(recvVal.value, recv);
                }
            }
            else
            {
                recv = recvAddr ? builder_.CreateLoad(st->llvm, recvAddr->addr) : recvVal.value;
            }
        }
        else
        {
            llvm::Value *p = recvAddr ? builder_.CreateLoad(builder_.getPtrTy(), recvAddr->addr)
                                      : recvVal.value;
            if (m.ptrRecv)
                recv = p;
            else
            {
                nilCheck(p);
                recv = builder_.CreateLoad(st->llvm, p);
            }
        }
        return callFunction(m, structTy->name() + "." + sel.name, call, {recv}, 1);
    }

    CodeGen::Value CodeGen::genIfaceCall(Value iface, const Call &call, const std::string &method)
    {
        if (call.spread)
            fail("'...' can only be used when calling a variadic function", call.line, call.col);
        IfaceInfo *ii = iface.type->iface;
        int idx = ii->methodIndex(method);
        if (idx < 0)
            fail("interface " + iface.type->name() + " has no method '" + method + "'", call.line,
                 call.col);
        const IfaceMethod &m = ii->methods[idx];
        std::string name = iface.type->name() + "." + method;
        if (call.args.size() != m.params.size())
            fail("'" + name + "' takes " + std::to_string(m.params.size()) + " argument(s), got " +
                     std::to_string(call.args.size()),
                 call.line, call.col);

        llvm::Value *data = builder_.CreateExtractValue(iface.value, 0, "iface.data");
        llvm::Value *vt = builder_.CreateExtractValue(iface.value, 1, "iface.vtable");
        panicIf(builder_.CreateIsNull(vt), "panicString",
                {builder_.CreateGlobalString("method call on a nil interface", ".str")});

        std::vector<llvm::Type *> paramTys{builder_.getPtrTy()};
        std::vector<llvm::Value *> args{data};
        for (size_t i = 0; i < call.args.size(); ++i)
        {
            const Expr &a = *call.args[i];
            Value v = coerce(genExpr(a, m.params[i]), m.params[i], a.line, a.col,
                             "argument " + ordinal(i + 1) + " of '" + name + "'");
            paramTys.push_back(llvmType(m.params[i]));
            args.push_back(v.value);
        }
        auto *fnTy = llvm::FunctionType::get(llvmType(m.ret), paramTys, false);
        llvm::Value *slot = builder_.CreateGEP(builder_.getPtrTy(), vt, builder_.getInt64(idx + 1));
        llvm::Value *fn = builder_.CreateLoad(builder_.getPtrTy(), slot);
        llvm::Value *result = builder_.CreateCall(fnTy, fn, args);
        if (m.ret->is(Type::Kind::Void))
            return {nullptr, m.ret};
        return owned({result, m.ret});
    }

    std::string CodeGen::missingMethod(TyP concrete, const IfaceInfo &iface) const
    {
        StructInfo *st = concrete->is(Type::Kind::Struct) ? concrete->st
                         : concrete->isPtrToStruct()     ? concrete->elem->st
                                                         : nullptr;
        for (const IfaceMethod &im : iface.methods)
        {
            const FuncInfo *m = nullptr;
            if (st)
                if (auto it = st->methods.find(im.name); it != st->methods.end())
                    m = &it->second;
            if (!m)
                return "missing method '" + im.name + "'";
            if (m->ptrRecv && concrete->is(Type::Kind::Struct))
                return "method '" + im.name + "' has a pointer receiver; use &value";
            bool same = m->ret == im.ret && m->params.size() == im.params.size() + 1;
            for (size_t i = 0; same && i < im.params.size(); ++i)
                same = m->params[i + 1] == im.params[i];
            if (!same)
                return "method '" + im.name + "' has a different signature";
        }
        return "";
    }

    CodeGen::Value CodeGen::makeIface(Value v, TyP ifaceType, int line, int col)
    {
        std::string why = missingMethod(v.type, *ifaceType->iface);
        if (!why.empty())
            fail(v.type->name() + " does not implement " + ifaceType->name() + ": " + why, line, col);

        llvm::Value *data;
        if (v.type->is(Type::Kind::Pointer))
            data = take(v);
        else
        {
            llvm::Value *copy = take(v);
            data = rt("pgt_alloc", builder_.getPtrTy(), {sizeOf(v.type), dropFn(v.type)});
            builder_.CreateStore(copy, data);
        }
        llvm::Value *res = llvm::UndefValue::get(ifaceTy_);
        res = builder_.CreateInsertValue(res, data, 0);
        res = builder_.CreateInsertValue(res, vtableFor(v.type, ifaceType), 1);
        return owned({res, ifaceType});
    }

    CodeGen::Value CodeGen::makeAny(Value v)
    {
        auto *ptr = builder_.getPtrTy();
        if (v.type->is(Type::Kind::Any))
            return v;
        if (v.type->is(Type::Kind::Nil))
            return {zeroValue(types_.anyT), types_.anyT};

        llvm::Value *data;
        llvm::Value *desc;
        if (v.type->is(Type::Kind::Interface))
        {
            desc = rt("pgt_iface_type", ptr, {builder_.CreateExtractValue(v.value, 1)});
            data = builder_.CreateExtractValue(take(v), 0);
        }
        else if (v.type->is(Type::Kind::Pointer))
        {
            desc = typeDesc(v.type);
            data = take(v);
        }
        else
        {
            desc = typeDesc(v.type);
            llvm::Value *copy = take(v);
            data = rt("pgt_alloc", ptr, {sizeOf(v.type), dropFn(v.type)});
            builder_.CreateStore(copy, data);
        }
        llvm::Value *res = llvm::UndefValue::get(ifaceTy_);
        res = builder_.CreateInsertValue(res, data, 0);
        res = builder_.CreateInsertValue(res, desc, 1);
        return owned({res, types_.anyT});
    }

    llvm::Constant *CodeGen::typeDesc(TyP t)
    {
        if (auto it = descs_.find(t); it != descs_.end())
            return it->second;
        auto *gv = new llvm::GlobalVariable(*module_, descTy_, true, llvm::GlobalValue::InternalLinkage,
                                            nullptr, "pgt.type." + t->name());
        descs_[t] = gv;

        auto *ptrTy = builder_.getPtrTy();
        auto *null = llvm::ConstantPointerNull::get(ptrTy);
        int kind = 0;
        switch (t->kind)
        {
        case Type::Kind::Int:
            kind = 1;
            break;
        case Type::Kind::Int64:
            kind = 2;
            break;
        case Type::Kind::Float:
            kind = 3;
            break;
        case Type::Kind::Char:
            kind = 4;
            break;
        case Type::Kind::Byte:
            kind = 5;
            break;
        case Type::Kind::Bool:
            kind = 6;
            break;
        case Type::Kind::String:
            kind = 7;
            break;
        case Type::Kind::Array:
            kind = 8;
            break;
        case Type::Kind::Pointer:
            kind = 9;
            break;
        case Type::Kind::Struct:
            kind = 10;
            break;
        case Type::Kind::Interface:
            kind = 11;
            break;
        case Type::Kind::Any:
            kind = 12;
            break;
        default:
            break;
        }

        llvm::Constant *elem = null;
        if (t->is(Type::Kind::Array) || t->is(Type::Kind::Pointer))
            elem = typeDesc(t->elem);

        llvm::Constant *fields = null;
        size_t nfields = 0;
        if (t->is(Type::Kind::Struct))
        {
            std::vector<llvm::Constant *> items;
            for (size_t i = 0; i < t->st->fields.size(); ++i)
            {
                const FieldInfo &f = t->st->fields[i];
                llvm::Constant *offset = llvm::ConstantExpr::getPtrToInt(
                    llvm::ConstantExpr::getGetElementPtr(
                        t->st->llvm, null,
                        llvm::ArrayRef<llvm::Constant *>{builder_.getInt32(0),
                                                         builder_.getInt32(static_cast<uint32_t>(i))}),
                    builder_.getInt64Ty());
                items.push_back(llvm::ConstantStruct::get(
                    fieldDescTy_,
                    {builder_.CreateGlobalString(f.name, ".fname", 0, module_.get()), typeDesc(f.type),
                     offset}));
            }
            nfields = items.size();
            if (!items.empty())
            {
                auto *arrTy = llvm::ArrayType::get(fieldDescTy_, items.size());
                fields = new llvm::GlobalVariable(*module_, arrTy, true, llvm::GlobalValue::InternalLinkage,
                                                  llvm::ConstantArray::get(arrTy, items), "pgt.fields");
            }
        }

        gv->setInitializer(llvm::ConstantStruct::get(
            descTy_, {builder_.getInt32(static_cast<uint32_t>(kind)),
                      builder_.getInt32(static_cast<uint32_t>(nfields)),
                      builder_.CreateGlobalString(t->name(), ".tname", 0, module_.get()), elem, sizeOf(t),
                      dropFn(t), retainFn(t), fields}));
        return gv;
    }

    llvm::Value *CodeGen::vtableFor(TyP concrete, TyP ifaceType)
    {
        auto key = std::make_pair(concrete, ifaceType);
        if (auto it = vtables_.find(key); it != vtables_.end())
            return it->second;

        std::vector<llvm::Constant *> entries{typeDesc(concrete)};
        StructInfo *st = concrete->is(Type::Kind::Struct) ? concrete->st
                         : concrete->isPtrToStruct()     ? concrete->elem->st
                                                         : nullptr;
        for (const IfaceMethod &im : ifaceType->iface->methods)
            entries.push_back(methodThunk(st, st->methods.at(im.name)));

        auto *arrTy = llvm::ArrayType::get(builder_.getPtrTy(), entries.size());
        auto *gv = new llvm::GlobalVariable(*module_, arrTy, true, llvm::GlobalValue::InternalLinkage,
                                            llvm::ConstantArray::get(arrTy, entries),
                                            "pgt.vtable." + concrete->name() + "." + ifaceType->name());
        vtables_[key] = gv;
        return gv;
    }

    llvm::Function *CodeGen::methodThunk(StructInfo *st, const FuncInfo &m)
    {
        std::string name = m.fn->getName().str() + ".thunk";
        if (auto it = thunks_.find(name); it != thunks_.end())
            return it->second;

        std::vector<llvm::Type *> params{builder_.getPtrTy()};
        for (size_t i = 1; i < m.params.size(); ++i)
            params.push_back(llvmType(m.params[i]));
        auto *fnTy = llvm::FunctionType::get(llvmType(m.ret), params, false);
        auto *fn = llvm::Function::Create(fnTy, llvm::Function::InternalLinkage, name, module_.get());
        thunks_[name] = fn;

        llvm::IRBuilderBase::InsertPointGuard guard(builder_);
        builder_.SetInsertPoint(llvm::BasicBlock::Create(ctx_, "entry", fn));
        llvm::Value *data = fn->getArg(0);
        std::vector<llvm::Value *> args;
        if (m.ptrRecv)
            args.push_back(data);
        else
        {
            nilCheck(data);
            args.push_back(builder_.CreateLoad(st->llvm, data));
        }
        for (unsigned i = 1; i < fn->arg_size(); ++i)
            args.push_back(fn->getArg(i));
        llvm::Value *res = builder_.CreateCall(m.fn, args);
        if (m.ret->is(Type::Kind::Void))
            builder_.CreateRetVoid();
        else
            builder_.CreateRet(res);
        return fn;
    }

}

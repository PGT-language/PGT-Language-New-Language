#pragma once
#include "ast/AST.h"

namespace pgt
{

    inline bool hasValueReturn(const Block &block)
    {
        for (const StmtPtr &s : block)
        {
            if (auto *r = dynamic_cast<const Return *>(s.get()))
            {
                if (r->value)
                    return true;
            }
            else if (auto *i = dynamic_cast<const If *>(s.get()))
            {
                if (hasValueReturn(i->thenBody) || hasValueReturn(i->elseBody))
                    return true;
            }
            else if (auto *l = dynamic_cast<const Loop *>(s.get()))
            {
                if (hasValueReturn(l->body))
                    return true;
            }
            else if (auto *l = dynamic_cast<const ForIn *>(s.get()))
            {
                if (hasValueReturn(l->body))
                    return true;
            }
            else if (auto *sw = dynamic_cast<const Switch *>(s.get()))
            {
                if (hasValueReturn(sw->defaultBody))
                    return true;
                for (const Case &c : sw->cases)
                    if (hasValueReturn(c.body))
                        return true;
            }
        }
        return false;
    }

    // Does the block contain a `break` that leaves the enclosing loop?
    // Breaks inside nested loops and switches belong to those statements.
    inline bool hasBreak(const Block &block)
    {
        for (const StmtPtr &s : block)
        {
            if (dynamic_cast<const Break *>(s.get()))
                return true;
            if (auto *i = dynamic_cast<const If *>(s.get()))
            {
                if (hasBreak(i->thenBody) || hasBreak(i->elseBody))
                    return true;
            }
        }
        return false;
    }

    inline bool terminates(const Block &block);

    inline bool callsExit(const Stmt *s)
    {
        auto *es = dynamic_cast<const ExprStmt *>(s);
        if (!es)
            return false;
        auto *call = dynamic_cast<const Call *>(es->expr.get());
        if (!call)
            return false;
        auto *ref = dynamic_cast<const VarRef *>(call->callee.get());
        return ref && (ref->name == "exit" || ref->name == "panic");
    }

    inline bool terminates(const Block &block)
    {
        if (block.empty())
            return false;
        const Stmt *last = block.back().get();
        if (dynamic_cast<const Return *>(last) || callsExit(last))
            return true;
        if (auto *i = dynamic_cast<const If *>(last))
            return i->hasElse && terminates(i->thenBody) && terminates(i->elseBody);
        if (auto *l = dynamic_cast<const Loop *>(last))
            return !l->cond && !hasBreak(l->body);
        if (auto *sw = dynamic_cast<const Switch *>(last))
        {
            if (!sw->hasDefault || !terminates(sw->defaultBody))
                return false;
            for (const Case &c : sw->cases)
                if (!terminates(c.body) || hasBreak(c.body))
                    return false;
            return !hasBreak(sw->defaultBody);
        }
        return false;
    }

}

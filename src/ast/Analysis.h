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
            else if (auto *l = dynamic_cast<const RangeLoop *>(s.get()))
            {
                if (hasValueReturn(l->body))
                    return true;
            }
        }
        return false;
    }

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

    inline bool terminates(const Block &block)
    {
        if (block.empty())
            return false;
        const Stmt *last = block.back().get();
        if (dynamic_cast<const Return *>(last))
            return true;
        if (auto *i = dynamic_cast<const If *>(last))
            return i->hasElse && terminates(i->thenBody) && terminates(i->elseBody);
        if (auto *l = dynamic_cast<const Loop *>(last))
            return !l->cond && !hasBreak(l->body);
        return false;
    }

}

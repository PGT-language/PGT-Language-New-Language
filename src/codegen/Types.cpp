#include "codegen/Types.h"

namespace pgt
{

    std::string Type::name() const
    {
        switch (kind)
        {
        case Kind::Void:
            return "void";
        case Kind::Int:
            return "int";
        case Kind::Int64:
            return "int64";
        case Kind::Float:
            return "float";
        case Kind::Char:
            return "char";
        case Kind::Byte:
            return "byte";
        case Kind::Bool:
            return "bool";
        case Kind::String:
            return "string";
        case Kind::Nil:
            return "nil";
        case Kind::Array:
            return "[]" + elem->name();
        case Kind::Pointer:
            return "*" + elem->name();
        case Kind::Struct:
            return st->pkgName == "main" ? st->name : st->pkgName + "." + st->name;
        case Kind::Interface:
            return iface->pkgName == "main" ? iface->name : iface->pkgName + "." + iface->name;
        case Kind::Any:
            return "any";
        case Kind::Map:
            return "map[" + key->name() + "]" + elem->name();
        }
        return "?";
    }

    TypeTable::TypeTable()
    {
        auto basic = [&](Type::Kind k)
        {
            Type t;
            t.kind = k;
            return add(t);
        };
        voidT = basic(Type::Kind::Void);
        intT = basic(Type::Kind::Int);
        int64T = basic(Type::Kind::Int64);
        floatT = basic(Type::Kind::Float);
        charT = basic(Type::Kind::Char);
        byteT = basic(Type::Kind::Byte);
        boolT = basic(Type::Kind::Bool);
        stringT = basic(Type::Kind::String);
        nilT = basic(Type::Kind::Nil);
        anyT = basic(Type::Kind::Any);
    }

    TyP TypeTable::add(Type t)
    {
        storage_.push_back(t);
        return &storage_.back();
    }

    TyP TypeTable::arrayOf(TyP elem)
    {
        auto &slot = arrays_[elem];
        if (!slot)
        {
            Type t;
            t.kind = Type::Kind::Array;
            t.elem = elem;
            slot = add(t);
        }
        return slot;
    }

    TyP TypeTable::pointerTo(TyP elem)
    {
        auto &slot = pointers_[elem];
        if (!slot)
        {
            Type t;
            t.kind = Type::Kind::Pointer;
            t.elem = elem;
            slot = add(t);
        }
        return slot;
    }

    TyP TypeTable::mapOf(TyP key, TyP elem)
    {
        auto &slot = maps_[{key, elem}];
        if (!slot)
        {
            Type t;
            t.kind = Type::Kind::Map;
            t.key = key;
            t.elem = elem;
            slot = add(t);
        }
        return slot;
    }

    TyP TypeTable::structT(StructInfo *st)
    {
        auto &slot = named_[st];
        if (!slot)
        {
            Type t;
            t.kind = Type::Kind::Struct;
            t.st = st;
            slot = add(t);
        }
        return slot;
    }

    TyP TypeTable::ifaceT(IfaceInfo *iface)
    {
        auto &slot = named_[iface];
        if (!slot)
        {
            Type t;
            t.kind = Type::Kind::Interface;
            t.iface = iface;
            slot = add(t);
        }
        return slot;
    }

}

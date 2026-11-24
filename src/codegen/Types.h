#pragma once
#include "ast/AST.h"

#include <llvm/IR/DerivedTypes.h>
#include <llvm/IR/Function.h>

#include <deque>
#include <map>
#include <string>
#include <vector>

namespace pgt
{

    struct StructInfo;
    struct IfaceInfo;

    struct Type
    {
        enum class Kind
        {
            Void,
            Int,
            Int64,
            Float,
            Char,
            Byte,
            Bool,
            String,
            Nil,
            Array,
            Pointer,
            Struct,
            Interface,
            Any,
            Map,
            Uint64
        };
        Kind kind = Kind::Void;
        const Type *elem = nullptr;
        const Type *key = nullptr;
        StructInfo *st = nullptr;
        IfaceInfo *iface = nullptr;

        bool is(Kind k) const
        {
            return kind == k;
        }
        bool isIntegral() const
        {
            return kind == Kind::Int || kind == Kind::Int64 || kind == Kind::Uint64 || kind == Kind::Char ||
                   kind == Kind::Byte;
        }
        bool isUnsigned() const
        {
            return kind == Kind::Char || kind == Kind::Byte || kind == Kind::Uint64;
        }
        bool isNumeric() const
        {
            return isIntegral() || kind == Kind::Float;
        }
        bool isScalar() const
        {
            return isNumeric() || kind == Kind::Bool || kind == Kind::String;
        }
        bool isNillable() const
        {
            return kind == Kind::Pointer || kind == Kind::Array || kind == Kind::String ||
                   kind == Kind::Interface || kind == Kind::Any || kind == Kind::Map;
        }
        bool isPtrToStruct() const
        {
            return kind == Kind::Pointer && elem->kind == Kind::Struct;
        }
        std::string name() const;
    };
    using TyP = const Type *;

    struct FuncInfo
    {
        std::string name;
        llvm::Function *fn = nullptr;
        std::vector<TyP> params;
        TyP ret = nullptr;
        bool isExtern = false;
        std::string intrinsic;
        bool ptrRecv = false;
        bool variadic = false;
        int line = 0, col = 0;
    };

    struct FieldInfo
    {
        std::string name;
        TyP type;
    };

    struct StructInfo
    {
        std::string name;
        std::string pkgPath;
        std::string pkgName;
        const StructDecl *decl = nullptr;
        const struct Program *program = nullptr;
        std::vector<FieldInfo> fields;
        llvm::StructType *llvm = nullptr;
        std::map<std::string, FuncInfo> methods;

        int fieldIndex(const std::string &n) const
        {
            for (size_t i = 0; i < fields.size(); ++i)
                if (fields[i].name == n)
                    return static_cast<int>(i);
            return -1;
        }
    };

    struct IfaceMethod
    {
        std::string name;
        std::vector<TyP> params;
        TyP ret;
    };

    struct IfaceInfo
    {
        std::string name;
        std::string pkgPath;
        std::string pkgName;
        const InterfaceDecl *decl = nullptr;
        const struct Program *program = nullptr;
        std::vector<IfaceMethod> methods;

        int methodIndex(const std::string &n) const
        {
            for (size_t i = 0; i < methods.size(); ++i)
                if (methods[i].name == n)
                    return static_cast<int>(i);
            return -1;
        }
    };

    class TypeTable
    {
    public:
        TypeTable();
        TyP voidT, intT, int64T, uint64T, floatT, charT, byteT, boolT, stringT, nilT, anyT;

        TyP arrayOf(TyP elem);
        TyP pointerTo(TyP elem);
        TyP mapOf(TyP key, TyP elem);
        TyP structT(StructInfo *st);
        TyP ifaceT(IfaceInfo *iface);

    private:
        TyP add(Type t);
        std::deque<Type> storage_;
        std::map<TyP, TyP> arrays_, pointers_;
        std::map<std::pair<TyP, TyP>, TyP> maps_;
        std::map<const void *, TyP> named_;
    };

}

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
            Bool,
            String,
            Nil,
            Array,
            Pointer,
            Struct,
            Interface
        };
        Kind kind = Kind::Void;
        const Type *elem = nullptr; // Array, Pointer
        StructInfo *st = nullptr;
        IfaceInfo *iface = nullptr;

        bool is(Kind k) const
        {
            return kind == k;
        }
        bool isIntegral() const
        {
            return kind == Kind::Int || kind == Kind::Int64 || kind == Kind::Char;
        }
        bool isNumeric() const
        {
            return isIntegral() || kind == Kind::Float;
        }
        bool isScalar() const
        {
            return isNumeric() || kind == Kind::Bool || kind == Kind::String;
        }
        // Types whose zero value is spelled `nil`.
        bool isNillable() const
        {
            return kind == Kind::Pointer || kind == Kind::Array || kind == Kind::String ||
                   kind == Kind::Interface;
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
        std::string name; // as written, for messages
        llvm::Function *fn = nullptr;
        std::vector<TyP> params; // for methods: the receiver comes first
        TyP ret = nullptr;
        bool isExtern = false;
        bool ptrRecv = false;
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

    // Owns every Type; equal types are the same pointer.
    class TypeTable
    {
    public:
        TypeTable();
        TyP voidT, intT, int64T, floatT, charT, boolT, stringT, nilT;

        TyP arrayOf(TyP elem);
        TyP pointerTo(TyP elem);
        TyP structT(StructInfo *st);
        TyP ifaceT(IfaceInfo *iface);

    private:
        TyP add(Type t);
        std::deque<Type> storage_;
        std::map<TyP, TyP> arrays_, pointers_;
        std::map<const void *, TyP> named_;
    };

}

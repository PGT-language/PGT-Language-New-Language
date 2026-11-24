#pragma once
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace pgt::lsp
{

    class Json
    {
    public:
        enum class Kind
        {
            Null,
            Bool,
            Number,
            String,
            Array,
            Object
        };

        Json() = default;
        Json(std::nullptr_t) {}
        Json(bool b) : kind_(Kind::Bool), bool_(b) {}
        Json(int n) : kind_(Kind::Number), num_(n) {}
        Json(long long n) : kind_(Kind::Number), num_(static_cast<double>(n)) {}
        Json(size_t n) : kind_(Kind::Number), num_(static_cast<double>(n)) {}
        Json(double n) : kind_(Kind::Number), num_(n) {}
        Json(const char *s) : kind_(Kind::String), str_(s) {}
        Json(std::string s) : kind_(Kind::String), str_(std::move(s)) {}

        static Json array();
        static Json object();
        static Json parse(const std::string &text);

        Kind kind() const
        {
            return kind_;
        }
        bool isNull() const
        {
            return kind_ == Kind::Null;
        }
        bool isObject() const
        {
            return kind_ == Kind::Object;
        }
        bool isArray() const
        {
            return kind_ == Kind::Array;
        }
        bool isString() const
        {
            return kind_ == Kind::String;
        }
        bool isNumber() const
        {
            return kind_ == Kind::Number;
        }

        const std::string &str() const
        {
            return str_;
        }
        double num() const
        {
            return num_;
        }
        int integer() const
        {
            return static_cast<int>(num_);
        }
        bool boolean() const
        {
            return bool_;
        }

        const Json &operator[](const std::string &key) const;
        Json &operator[](const std::string &key);
        bool has(const std::string &key) const;
        const std::vector<Json> &items() const
        {
            return arr_;
        }
        void push(Json v);
        size_t size() const
        {
            return kind_ == Kind::Array ? arr_.size() : obj_.size();
        }

        std::string dump() const;

    private:
        void dumpTo(std::string &out) const;

        Kind kind_ = Kind::Null;
        bool bool_ = false;
        double num_ = 0;
        std::string str_;
        std::vector<Json> arr_;
        std::vector<std::pair<std::string, Json>> obj_;
    };

}

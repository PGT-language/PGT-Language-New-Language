#include "lsp/Json.h"

#include <cmath>
#include <cstdio>
#include <stdexcept>

namespace pgt::lsp
{
    namespace
    {

        const Json kNull;

        class Reader
        {
        public:
            explicit Reader(const std::string &s) : s_(s) {}

            Json value()
            {
                skip();
                if (pos_ >= s_.size())
                    throw std::runtime_error("json: unexpected end");
                char c = s_[pos_];
                if (c == '{')
                    return object();
                if (c == '[')
                    return array();
                if (c == '"')
                    return Json(string());
                if (c == 't' && s_.compare(pos_, 4, "true") == 0)
                {
                    pos_ += 4;
                    return Json(true);
                }
                if (c == 'f' && s_.compare(pos_, 5, "false") == 0)
                {
                    pos_ += 5;
                    return Json(false);
                }
                if (c == 'n' && s_.compare(pos_, 4, "null") == 0)
                {
                    pos_ += 4;
                    return Json();
                }
                return number();
            }

        private:
            void skip()
            {
                while (pos_ < s_.size() && (s_[pos_] == ' ' || s_[pos_] == '\n' || s_[pos_] == '\r' ||
                                            s_[pos_] == '\t'))
                    ++pos_;
            }

            void expect(char c)
            {
                skip();
                if (pos_ >= s_.size() || s_[pos_] != c)
                    throw std::runtime_error(std::string("json: expected '") + c + "'");
                ++pos_;
            }

            Json object()
            {
                Json o = Json::object();
                expect('{');
                skip();
                if (pos_ < s_.size() && s_[pos_] == '}')
                {
                    ++pos_;
                    return o;
                }
                while (true)
                {
                    skip();
                    std::string key = string();
                    expect(':');
                    o[key] = value();
                    skip();
                    if (pos_ < s_.size() && s_[pos_] == ',')
                    {
                        ++pos_;
                        continue;
                    }
                    expect('}');
                    return o;
                }
            }

            Json array()
            {
                Json a = Json::array();
                expect('[');
                skip();
                if (pos_ < s_.size() && s_[pos_] == ']')
                {
                    ++pos_;
                    return a;
                }
                while (true)
                {
                    a.push(value());
                    skip();
                    if (pos_ < s_.size() && s_[pos_] == ',')
                    {
                        ++pos_;
                        continue;
                    }
                    expect(']');
                    return a;
                }
            }

            unsigned hex4()
            {
                if (pos_ + 4 > s_.size())
                    throw std::runtime_error("json: bad \\u escape");
                unsigned v = std::stoul(s_.substr(pos_, 4), nullptr, 16);
                pos_ += 4;
                return v;
            }

            static void utf8(std::string &out, unsigned cp)
            {
                if (cp < 0x80)
                    out += static_cast<char>(cp);
                else if (cp < 0x800)
                {
                    out += static_cast<char>(0xC0 | (cp >> 6));
                    out += static_cast<char>(0x80 | (cp & 0x3F));
                }
                else if (cp < 0x10000)
                {
                    out += static_cast<char>(0xE0 | (cp >> 12));
                    out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
                    out += static_cast<char>(0x80 | (cp & 0x3F));
                }
                else
                {
                    out += static_cast<char>(0xF0 | (cp >> 18));
                    out += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
                    out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
                    out += static_cast<char>(0x80 | (cp & 0x3F));
                }
            }

            std::string string()
            {
                expect('"');
                std::string out;
                while (pos_ < s_.size() && s_[pos_] != '"')
                {
                    char c = s_[pos_++];
                    if (c != '\\')
                    {
                        out += c;
                        continue;
                    }
                    if (pos_ >= s_.size())
                        break;
                    char e = s_[pos_++];
                    switch (e)
                    {
                    case 'n':
                        out += '\n';
                        break;
                    case 't':
                        out += '\t';
                        break;
                    case 'r':
                        out += '\r';
                        break;
                    case 'b':
                        out += '\b';
                        break;
                    case 'f':
                        out += '\f';
                        break;
                    case 'u':
                    {
                        unsigned cp = hex4();
                        if (cp >= 0xD800 && cp < 0xDC00 && pos_ + 6 <= s_.size() && s_[pos_] == '\\' &&
                            s_[pos_ + 1] == 'u')
                        {
                            pos_ += 2;
                            unsigned lo = hex4();
                            cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
                        }
                        utf8(out, cp);
                        break;
                    }
                    default:
                        out += e;
                    }
                }
                expect('"');
                return out;
            }

            Json number()
            {
                size_t start = pos_;
                while (pos_ < s_.size() && (std::isdigit(static_cast<unsigned char>(s_[pos_])) ||
                                            s_[pos_] == '-' || s_[pos_] == '+' || s_[pos_] == '.' ||
                                            s_[pos_] == 'e' || s_[pos_] == 'E'))
                    ++pos_;
                if (start == pos_)
                    throw std::runtime_error("json: unexpected character");
                return Json(std::stod(s_.substr(start, pos_ - start)));
            }

            const std::string &s_;
            size_t pos_ = 0;
        };

        void escape(std::string &out, const std::string &s)
        {
            out += '"';
            for (unsigned char c : s)
            {
                switch (c)
                {
                case '"':
                    out += "\\\"";
                    break;
                case '\\':
                    out += "\\\\";
                    break;
                case '\n':
                    out += "\\n";
                    break;
                case '\r':
                    out += "\\r";
                    break;
                case '\t':
                    out += "\\t";
                    break;
                default:
                    if (c < 0x20)
                    {
                        char buf[8];
                        std::snprintf(buf, sizeof buf, "\\u%04x", c);
                        out += buf;
                    }
                    else
                        out += static_cast<char>(c);
                }
            }
            out += '"';
        }

    }

    Json Json::array()
    {
        Json j;
        j.kind_ = Kind::Array;
        return j;
    }

    Json Json::object()
    {
        Json j;
        j.kind_ = Kind::Object;
        return j;
    }

    Json Json::parse(const std::string &text)
    {
        Reader r(text);
        return r.value();
    }

    const Json &Json::operator[](const std::string &key) const
    {
        for (const auto &[k, v] : obj_)
            if (k == key)
                return v;
        return kNull;
    }

    Json &Json::operator[](const std::string &key)
    {
        if (kind_ != Kind::Object)
        {
            kind_ = Kind::Object;
            obj_.clear();
        }
        for (auto &[k, v] : obj_)
            if (k == key)
                return v;
        obj_.emplace_back(key, Json());
        return obj_.back().second;
    }

    bool Json::has(const std::string &key) const
    {
        for (const auto &[k, v] : obj_)
            if (k == key)
                return true;
        return false;
    }

    void Json::push(Json v)
    {
        if (kind_ != Kind::Array)
        {
            kind_ = Kind::Array;
            arr_.clear();
        }
        arr_.push_back(std::move(v));
    }

    std::string Json::dump() const
    {
        std::string out;
        dumpTo(out);
        return out;
    }

    void Json::dumpTo(std::string &out) const
    {
        switch (kind_)
        {
        case Kind::Null:
            out += "null";
            break;
        case Kind::Bool:
            out += bool_ ? "true" : "false";
            break;
        case Kind::Number:
        {
            if (std::floor(num_) == num_ && std::fabs(num_) < 1e15)
                out += std::to_string(static_cast<long long>(num_));
            else
            {
                char buf[32];
                std::snprintf(buf, sizeof buf, "%.17g", num_);
                out += buf;
            }
            break;
        }
        case Kind::String:
            escape(out, str_);
            break;
        case Kind::Array:
        {
            out += '[';
            for (size_t i = 0; i < arr_.size(); ++i)
            {
                if (i)
                    out += ',';
                arr_[i].dumpTo(out);
            }
            out += ']';
            break;
        }
        case Kind::Object:
        {
            out += '{';
            for (size_t i = 0; i < obj_.size(); ++i)
            {
                if (i)
                    out += ',';
                escape(out, obj_[i].first);
                out += ':';
                obj_[i].second.dumpTo(out);
            }
            out += '}';
            break;
        }
        }
    }

}

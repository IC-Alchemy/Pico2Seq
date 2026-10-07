// JsonWriter - just enough JSON to dump the editor's resource files deterministically.
// Objects and arrays print one member per line; arrays opened with beginArray(true) print on
// one line (for short lists of scalars). Floats print with 9 significant digits, which
// round-trips an IEEE-754 single exactly.
#pragma once

#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

namespace testsupport
{

class JsonWriter
{
public:
    void beginObject() { open('{', false); }
    void endObject() { close('}'); }
    void beginArray(bool oneLine = false) { open('[', oneLine); }
    void endArray() { close(']'); }
    void key(const std::string &name)
    {
        separate();
        text(name);
        out_ += ": ";
        afterKey_ = true;
    }
    void str(const std::string &value)
    {
        separate();
        text(value);
    }
    void real(float value)
    {
        separate();
        char buf[40];
        std::snprintf(buf, sizeof buf, "%.9g", value == 0.0f ? 0.0f : value);
        out_ += buf;
    }
    void integer(int64_t value)
    {
        separate();
        out_ += std::to_string(value);
    }
    void boolean(bool value)
    {
        separate();
        out_ += value ? "true" : "false";
    }
    void null()
    {
        separate();
        out_ += "null";
    }
    // key + scalar shorthands
    void member(const std::string &name, const std::string &value) { key(name); str(value); }
    void member(const std::string &name, const char *value) { key(name); str(value); }
    void member(const std::string &name, float value) { key(name); real(value); }
    void member(const std::string &name, int value) { key(name); integer(value); }
    void member(const std::string &name, int64_t value) { key(name); integer(value); }
    void member(const std::string &name, unsigned value) { key(name); integer(value); }
    void member(const std::string &name, bool value) { key(name); boolean(value); }
    std::string take() { return out_ + "\n"; }

private:
    struct Level
    {
        bool oneLine;
        bool empty = true;
    };

    void text(const std::string &s)
    {
        out_ += '"';
        for (char c : s)
        {
            switch (c)
            {
            case '"': out_ += "\\\""; break;
            case '\\': out_ += "\\\\"; break;
            case '\n': out_ += "\\n"; break;
            default:
                if (static_cast<unsigned char>(c) < 0x20)
                {
                    char buf[8];
                    std::snprintf(buf, sizeof buf, "\\u%04x", c);
                    out_ += buf;
                }
                else
                    out_ += c;
            }
        }
        out_ += '"';
    }
    void newline(size_t depth)
    {
        out_ += '\n';
        out_.append(depth * 2, ' ');
    }
    void separate()
    {
        if (afterKey_)
        {
            afterKey_ = false;
            return;
        }
        if (stack_.empty())
            return;
        Level &top = stack_.back();
        if (!top.empty)
            out_ += top.oneLine ? ", " : ",";
        if (!top.oneLine)
            newline(stack_.size());
        top.empty = false;
    }
    void open(char c, bool oneLine)
    {
        separate();
        out_ += c;
        stack_.push_back({oneLine || (!stack_.empty() && stack_.back().oneLine)});
    }
    void close(char c)
    {
        const Level top = stack_.back();
        stack_.pop_back();
        if (!top.empty && !top.oneLine)
            newline(stack_.size());
        out_ += c;
    }

    std::string out_;
    std::vector<Level> stack_;
    bool afterKey_ = false;
};

} // namespace testsupport

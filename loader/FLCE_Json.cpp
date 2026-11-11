// Minimal JSON reader for ForgedLCE data mods. C++03, no dependencies.
#include "stdafx.h"
#include "FLCE_Json.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

namespace ForgedLCE {
namespace Json {

const Value *Value::get(const wchar_t *key) const
{
    if (type != OBJECT) return NULL;
    for (size_t i = 0; i < keys.size(); ++i) {
        if (keys[i] == key) return items[i];
    }
    return NULL;
}

Document::~Document()
{
    for (size_t i = 0; i < m_pool.size(); ++i) delete m_pool[i];
}

Value *Document::newValue(int line)
{
    Value *v = new Value();
    v->line = line;
    m_pool.push_back(v);
    return v;
}

namespace {

std::wstring IntText(int v)
{
    char buf[16];
    sprintf(buf, "%d", v);
    std::wstring out;
    for (const char *p = buf; *p; ++p) out += (wchar_t)*p;
    return out;
}

// Appends code point `cp` to `out` in the platform's wchar_t encoding
// (UTF-16 where wchar_t is 2 bytes, UTF-32 otherwise).
void AppendCodePoint(std::wstring &out, unsigned long cp)
{
    if (cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF)) cp = 0xFFFD;
    if (sizeof(wchar_t) == 2 && cp >= 0x10000) {
        cp -= 0x10000;
        out += (wchar_t)(0xD800 + (cp >> 10));
        out += (wchar_t)(0xDC00 + (cp & 0x3FF));
    } else {
        out += (wchar_t)cp;
    }
}

} // namespace

class Parser
{
public:
    Parser(Document &doc, const std::string &text)
        : m_doc(doc), m_s(text), m_pos(0), m_line(1) {}

    bool run(std::wstring &error)
    {
        if (m_s.size() >= 3 && (unsigned char)m_s[0] == 0xEF && (unsigned char)m_s[1] == 0xBB &&
            (unsigned char)m_s[2] == 0xBF) {
            m_pos = 3;
        }
        Value *root = parseValue(0);
        if (root != NULL) {
            skipSpace();
            if (m_pos != m_s.size()) root = fail(L"unexpected text after the end");
        }
        if (root == NULL) {
            error = L"line " + IntText(m_line) + L": " + m_error;
            return false;
        }
        m_doc.root = root;
        return true;
    }

private:
    Document          &m_doc;
    const std::string &m_s;
    size_t             m_pos;
    int                m_line;
    std::wstring       m_error;

    Value *fail(const wchar_t *message)
    {
        if (m_error.empty()) m_error = message;
        return NULL;
    }

    void skipSpace()
    {
        while (m_pos < m_s.size()) {
            char c = m_s[m_pos];
            if (c == '\n') { ++m_line; ++m_pos; }
            else if (c == ' ' || c == '\t' || c == '\r') ++m_pos;
            else break;
        }
    }

    bool match(const char *word)
    {
        size_t n = strlen(word);
        if (m_s.compare(m_pos, n, word) != 0) return false;
        m_pos += n;
        return true;
    }

    Value *parseValue(int depth)
    {
        if (depth > 64) return fail(L"nested too deeply");
        skipSpace();
        if (m_pos >= m_s.size()) return fail(L"unexpected end of file");
        char c = m_s[m_pos];
        int line = m_line;
        if (c == '{') return parseObject(depth);
        if (c == '[') return parseArray(depth);
        if (c == '"') {
            Value *v = m_doc.newValue(line);
            v->type = Value::STRING;
            return parseString(v->string) ? v : NULL;
        }
        if (c == '-' || (c >= '0' && c <= '9')) return parseNumber();
        Value *v = m_doc.newValue(line);
        if (match("true"))  { v->type = Value::BOOL; v->boolean = true;  return v; }
        if (match("false")) { v->type = Value::BOOL; v->boolean = false; return v; }
        if (match("null"))  { v->type = Value::NUL; return v; }
        return fail(L"expected a value");
    }

    Value *parseObject(int depth)
    {
        Value *v = m_doc.newValue(m_line);
        v->type = Value::OBJECT;
        ++m_pos; // {
        skipSpace();
        if (m_pos < m_s.size() && m_s[m_pos] == '}') { ++m_pos; return v; }
        for (;;) {
            skipSpace();
            if (m_pos >= m_s.size() || m_s[m_pos] != '"') return fail(L"expected a \"key\"");
            std::wstring key;
            if (!parseString(key)) return NULL;
            skipSpace();
            if (m_pos >= m_s.size() || m_s[m_pos] != ':') return fail(L"expected ':' after a key");
            ++m_pos;
            Value *item = parseValue(depth + 1);
            if (item == NULL) return NULL;
            v->keys.push_back(key);
            v->items.push_back(item);
            skipSpace();
            if (m_pos < m_s.size() && m_s[m_pos] == ',') { ++m_pos; continue; }
            if (m_pos < m_s.size() && m_s[m_pos] == '}') { ++m_pos; return v; }
            return fail(L"expected ',' or '}' (is a comma missing?)");
        }
    }

    Value *parseArray(int depth)
    {
        Value *v = m_doc.newValue(m_line);
        v->type = Value::ARRAY;
        ++m_pos; // [
        skipSpace();
        if (m_pos < m_s.size() && m_s[m_pos] == ']') { ++m_pos; return v; }
        for (;;) {
            Value *item = parseValue(depth + 1);
            if (item == NULL) return NULL;
            v->items.push_back(item);
            skipSpace();
            if (m_pos < m_s.size() && m_s[m_pos] == ',') { ++m_pos; continue; }
            if (m_pos < m_s.size() && m_s[m_pos] == ']') { ++m_pos; return v; }
            return fail(L"expected ',' or ']' (is a comma missing?)");
        }
    }

    Value *parseNumber()
    {
        size_t start = m_pos;
        if (m_s[m_pos] == '-') ++m_pos;
        while (m_pos < m_s.size()) {
            char c = m_s[m_pos];
            if ((c >= '0' && c <= '9') || c == '.' || c == 'e' || c == 'E' || c == '+' || c == '-') ++m_pos;
            else break;
        }
        std::string text = m_s.substr(start, m_pos - start);
        char *end = NULL;
        double d = strtod(text.c_str(), &end);
        if (end == NULL || *end != 0 || text == "-") return fail(L"bad number");
        Value *v = m_doc.newValue(m_line);
        v->type = Value::NUMBER;
        v->number = d;
        return v;
    }

    bool hex4(unsigned long &out)
    {
        if (m_pos + 4 > m_s.size()) return false;
        out = 0;
        for (int i = 0; i < 4; ++i) {
            char c = m_s[m_pos++];
            out <<= 4;
            if (c >= '0' && c <= '9') out |= (unsigned long)(c - '0');
            else if (c >= 'a' && c <= 'f') out |= (unsigned long)(c - 'a' + 10);
            else if (c >= 'A' && c <= 'F') out |= (unsigned long)(c - 'A' + 10);
            else return false;
        }
        return true;
    }

    bool parseString(std::wstring &out)
    {
        ++m_pos; // opening quote
        for (;;) {
            if (m_pos >= m_s.size()) { fail(L"unterminated string"); return false; }
            unsigned char c = (unsigned char)m_s[m_pos++];
            if (c == '"') return true;
            if (c == '\n') { fail(L"line break inside a string"); return false; }
            if (c == '\\') {
                if (m_pos >= m_s.size()) { fail(L"unterminated string"); return false; }
                char e = m_s[m_pos++];
                switch (e) {
                case '"':  out += L'"';  break;
                case '\\': out += L'\\'; break;
                case '/':  out += L'/';  break;
                case 'b':  out += L'\b'; break;
                case 'f':  out += L'\f'; break;
                case 'n':  out += L'\n'; break;
                case 'r':  out += L'\r'; break;
                case 't':  out += L'\t'; break;
                case 'u': {
                    unsigned long cp;
                    if (!hex4(cp)) { fail(L"bad \\u escape"); return false; }
                    if (cp >= 0xD800 && cp <= 0xDBFF && m_pos + 6 <= m_s.size() &&
                        m_s[m_pos] == '\\' && m_s[m_pos + 1] == 'u') {
                        size_t save = m_pos;
                        m_pos += 2;
                        unsigned long lo;
                        if (hex4(lo) && lo >= 0xDC00 && lo <= 0xDFFF) {
                            cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
                        } else {
                            m_pos = save;
                        }
                    }
                    AppendCodePoint(out, cp);
                    break;
                }
                default:
                    fail(L"unknown escape in a string");
                    return false;
                }
                continue;
            }
            // UTF-8 decoding
            unsigned long cp;
            int extra;
            if (c < 0x80)                { cp = c;        extra = 0; }
            else if ((c & 0xE0) == 0xC0) { cp = c & 0x1F; extra = 1; }
            else if ((c & 0xF0) == 0xE0) { cp = c & 0x0F; extra = 2; }
            else if ((c & 0xF8) == 0xF0) { cp = c & 0x07; extra = 3; }
            else { fail(L"invalid UTF-8 (save the file as UTF-8)"); return false; }
            for (int i = 0; i < extra; ++i) {
                if (m_pos >= m_s.size() || ((unsigned char)m_s[m_pos] & 0xC0) != 0x80) {
                    fail(L"invalid UTF-8 (save the file as UTF-8)");
                    return false;
                }
                cp = (cp << 6) | ((unsigned char)m_s[m_pos++] & 0x3F);
            }
            AppendCodePoint(out, cp);
        }
    }
};

bool Document::parse(const std::string &utf8, std::wstring &error)
{
    Parser p(*this, utf8);
    return p.run(error);
}

} // namespace Json
} // namespace ForgedLCE

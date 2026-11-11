// Minimal JSON reader for ForgedLCE data mods (forgedlce.json). C++03.
#ifndef FLCE_JSON_H
#define FLCE_JSON_H

#include <string>
#include <vector>

namespace ForgedLCE {
namespace Json {

class Value
{
public:
    enum Type { NUL, BOOL, NUMBER, STRING, ARRAY, OBJECT };

    Value() : type(NUL), boolean(false), number(0), line(0) {}

    Type         type;
    bool         boolean;
    double       number;
    std::wstring string;
    int          line;   // line number in the file, for error messages

    // ARRAY: items. OBJECT: items with matching keys.
    std::vector<Value *>      items;
    std::vector<std::wstring> keys;

    // OBJECT member, or NULL.
    const Value *get(const wchar_t *key) const;

    bool isString() const { return type == STRING; }
    bool isNumber() const { return type == NUMBER; }
    bool isArray()  const { return type == ARRAY; }
    bool isObject() const { return type == OBJECT; }
};

// Owns every Value it parsed.
class Document
{
public:
    Document() : root(NULL) {}
    ~Document();

    // Parses UTF-8 text (a byte order mark is allowed). On failure returns
    // false and sets `error` (with the line number).
    bool parse(const std::string &utf8, std::wstring &error);

    const Value *root;

private:
    std::vector<Value *> m_pool;
    Value *newValue(int line);
    friend class Parser;

    Document(const Document &);
    Document &operator=(const Document &);
};

} // namespace Json
} // namespace ForgedLCE

#endif

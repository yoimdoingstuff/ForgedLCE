// ForgedLCE injected mode: the address map.
//
// A plugin injected into the compiled game can't call game functions by
// name; it needs their addresses in the specific game version, plus a few
// struct offsets and sizes. Those are read from a text file
// (inject/addresses/*.txt), one per game version, so the plugin itself
// never has to be rebuilt for a new version.
#ifndef FLCE_ADDRESSES_H
#define FLCE_ADDRESSES_H

#include <map>
#include <string>

namespace ForgedLCE {
namespace Inject {

// Wide enough for a host pointer when testing on a 64-bit PC.
typedef unsigned long long Addr;

enum KeyKind {
    KIND_CHECK,   // game-version check
    KIND_HOOK,    // function ForgedLCE hooks
    KIND_FUNC,    // function ForgedLCE calls
    KIND_DATA,    // address of a static variable
    KIND_OFFSET,  // byte offset of a struct member
    KIND_SIZE,    // sizeof a game type
    KIND_FLAG     // 0/1 calling-convention detail
};

struct KeyInfo {
    const char *name;
    KeyKind     kind;
    bool        required;
    const char *help;    // what it is, and where it is in the LCE source
};

// Every key the address map understands, terminated by { NULL }.
extern const KeyInfo KEYS[];
const KeyInfo *FindKey(const std::string &name);

class AddressMap
{
public:
    // Text format, one "key = value" per line, '#' starts a comment:
    //   game = Minecraft: Xbox 360 Edition, TU19     (free text)
    //   fn.Tile_ctor = 0x8215A2C8                     (absolute address)
    //   fn.Tile_ctor = 0:0x0015A2C9                   (segment 0 + offset; PS Vita)
    // Unknown keys and bad values are errors (with the line number).
    bool parse(const std::string &text, std::wstring &error);

    bool has(const char *key) const;
    Addr get(const char *key) const;       // 0 if missing
    int  segment(const char *key) const;   // -1 = absolute
    void set(const char *key, Addr value, int segment = -1);

    // Comma-separated required keys that are missing, or empty.
    std::string missingRequired() const;

    std::string game;

private:
    struct Entry { Addr value; int segment; };
    std::map<std::string, Entry> m_values;
};

} // namespace Inject
} // namespace ForgedLCE

#endif

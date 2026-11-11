// ForgedLCE C++ helpers for mod authors.
//
// Header-only and compiled into the mod, so it does not affect the ABI.
// C++03-compatible so it also builds with the console compilers.
#ifndef FORGEDLCE_HPP
#define FORGEDLCE_HPP

#include "ForgedLCE.h"

#include <string.h>
#include <string>

namespace flce {

// Zero-initialised descriptors with structSize and sensible defaults.
inline FLCE_BlockDesc blockDesc(const wchar_t *registryName, const wchar_t *displayName)
{
    FLCE_BlockDesc d;
    memset(&d, 0, sizeof(d));
    d.structSize    = sizeof(d);
    d.registryName  = registryName;
    d.displayName   = displayName;
    d.material      = FLCE_MATERIAL_STONE;
    d.sound         = FLCE_SOUND_STONE;
    d.hardness      = 1.5f;
    d.resistance    = 10.0f;
    d.textureName   = L"stone";
    return d;
}

inline FLCE_ItemDesc itemDesc(const wchar_t *registryName, const wchar_t *displayName)
{
    FLCE_ItemDesc d;
    memset(&d, 0, sizeof(d));
    d.structSize   = sizeof(d);
    d.registryName = registryName;
    d.displayName  = displayName;
    d.textureName  = L"stick";
    return d;
}

// Typed event subscription with a plain function:
//   void onBreak(FLCE_BlockBreakEvent &e, const FLCE_Api *api) { ... }
//   flce::on(api, FLCE_EVENT_BLOCK_BREAK, onBreak);
template <typename EventT>
struct Subscription {
    typedef void (*Fn)(EventT &event, const FLCE_Api *api);
    Fn              fn;
    const FLCE_Api *api;

    static void FLCE_CALL thunk(FLCE_Event *e, void *user)
    {
        Subscription *s = static_cast<Subscription *>(user);
        s->fn(*reinterpret_cast<EventT *>(e), s->api);
    }
};

template <typename EventT>
inline bool on(const FLCE_Api *api, FLCE_EventType type,
               void (*fn)(EventT &event, const FLCE_Api *api),
               FLCE_EventPriority priority = FLCE_PRIORITY_NORMAL)
{
    // Lives for the life of the process (mods are never unloaded).
    Subscription<EventT> *s = new Subscription<EventT>();
    s->fn  = fn;
    s->api = api;
    if (!api->subscribe(type, &Subscription<EventT>::thunk, priority, s)) {
        delete s;
        return false;
    }
    return true;
}

// Integer to wide string without std::to_wstring (missing or ambiguous on
// some console compilers).
inline std::wstring str(int value)
{
    wchar_t buf[16];
    wchar_t *p = buf + 15;
    *p = 0;
    unsigned int v = value < 0 ? 0u - (unsigned int)value : (unsigned int)value;
    do { *--p = (wchar_t)(L'0' + v % 10); v /= 10; } while (v != 0);
    if (value < 0) *--p = L'-';
    return std::wstring(p);
}

// Mod-scoped logger: flce::Log log(api, L"examplemod"); log.info(L"hi");
class Log {
public:
    Log(const FLCE_Api *api, const wchar_t *modId) : api_(api), modId_(modId) {}
    void debug(const std::wstring &m) const { api_->log(modId_, FLCE_LOG_DEBUG, m.c_str()); }
    void info (const std::wstring &m) const { api_->log(modId_, FLCE_LOG_INFO,  m.c_str()); }
    void warn (const std::wstring &m) const { api_->log(modId_, FLCE_LOG_WARN,  m.c_str()); }
    void error(const std::wstring &m) const { api_->log(modId_, FLCE_LOG_ERROR, m.c_str()); }
private:
    const FLCE_Api *api_;
    const wchar_t  *modId_;
};

} // namespace flce

#endif // FORGEDLCE_HPP

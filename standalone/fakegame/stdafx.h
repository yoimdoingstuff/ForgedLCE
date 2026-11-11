// Fake stand-in for the LCE precompiled header (Minecraft.World/stdafx.h).
//
// Part of the ForgedLCE fake game layer, which lets the loader and mods be
// compiled and tested without the licensed LCE source. It provides what the
// real stdafx.h provides to ForgedLCE code on every platform:
//   - `using namespace std`, shared_ptr, dynamic_pointer_cast
//   - CRITICAL_SECTION (real on Windows, 4J stubs on consoles; shim here)
//   - the global `app` with DebugPrintf
//
// This file is NOT copied from the game. It only mirrors the names.
#ifndef FLCE_FAKE_STDAFX_H
#define FLCE_FAKE_STDAFX_H

#ifdef _WINDOWS64
#  include <windows.h>
#endif

#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <wchar.h>
#include <stdlib.h>

#include <string>
#include <vector>
#include <map>

#include <ciso646> // defines _LIBCPP_VERSION when the library is libc++

#if defined(_MSC_VER) || __cplusplus >= 201103L || defined(_LIBCPP_VERSION)
// libc++ provides std::shared_ptr even in C++03 mode.
#  include <memory>
#else
// C++03 with libstdc++ (stand-in for the older console toolchains).
#  include <tr1/memory>
namespace std {
    using std::tr1::shared_ptr;
    using std::tr1::dynamic_pointer_cast;
    using std::tr1::enable_shared_from_this;
}
#endif

using namespace std;

#ifndef _WIN32
#  include <pthread.h>
// Like 4J's console stubs: a recursive lock under the Windows names.
struct CRITICAL_SECTION { pthread_mutex_t m; };
inline void InitializeCriticalSection(CRITICAL_SECTION *cs)
{
    pthread_mutexattr_t a;
    pthread_mutexattr_init(&a);
    pthread_mutexattr_settype(&a, PTHREAD_MUTEX_RECURSIVE);
    pthread_mutex_init(&cs->m, &a);
    pthread_mutexattr_destroy(&a);
}
inline void EnterCriticalSection(CRITICAL_SECTION *cs) { pthread_mutex_lock(&cs->m); }
inline void LeaveCriticalSection(CRITICAL_SECTION *cs) { pthread_mutex_unlock(&cs->m); }
typedef const wchar_t *LPCWSTR;
#elif !defined(_WINDOWS64)
// Windows host emulating a console build (FLCE_EMULATE_CONSOLE).
#  include <windows.h>
#endif

// Stand-in for CMinecraftApp. DebugPrintf output is kept for tests.
class FakeApp
{
public:
    void DebugPrintf(const char *format, ...);
    std::string log; // everything printed so far
};
extern FakeApp app;

#endif // FLCE_FAKE_STDAFX_H

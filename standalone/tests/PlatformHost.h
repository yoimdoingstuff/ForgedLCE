// Platform services for the injected-mode tests on a PC (Windows or POSIX).
#ifndef FLCE_PLATFORMHOST_H
#define FLCE_PLATFORMHOST_H

#include <string>

namespace ForgedLCE {
namespace Platform {

void HostSetModsDir(const std::wstring &dir); // must end in a separator
std::string HostLog();                        // everything logged so far (UTF-8)
std::wstring HostWiden(const std::string &utf8);

} // namespace Platform
} // namespace ForgedLCE

#endif

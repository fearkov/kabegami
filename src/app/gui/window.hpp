#pragma once

#include <string>

namespace kabegami::app {

// the studio's window: the wallpaper drawn live as the look changes, and
// the buttons that put it where Azahar, Zakuro and Luma3DS read it. home is
// a dump to open at once, or empty. the exit status.
int run_window(const std::string& home);

}  // namespace kabegami::app

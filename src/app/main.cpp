#include <iostream>
#include <string>
#include <vector>

#include "app/cli.hpp"

#ifdef KABEGAMI_GUI
#include "app/gui/window.hpp"
#endif

int main(int argc, char** argv) {
    std::vector<std::string> args(argv + 1, argv + argc);
    // the window, by itself or with "window <home>"; anything else is a command
    if (args.empty() || args[0] == "window") {
#ifdef KABEGAMI_GUI
        return kabegami::app::run_window(args.size() > 1 ? args[1] : std::string());
#else
        if (!args.empty()) {
            std::cerr << "kabegami: this build has no window; build it with -DKABEGAMI_GUI=ON\n";
            return 2;
        }
#endif
    }
    return kabegami::app::run_cli(args);
}

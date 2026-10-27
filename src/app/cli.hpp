#pragma once

#include <string>
#include <vector>

namespace kabegami::app {

// runs a command line, the words after the program's name; the exit status
int run_cli(const std::vector<std::string>& args);

}  // namespace kabegami::app

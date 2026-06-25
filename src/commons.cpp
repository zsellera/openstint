#include "commons.hpp"

#include <cstdlib>
#include <string>

bool parse_common_arguments(CommonOptions& opts, int& i, const int argc, const std::string& arg, char** argv) {
    if (arg == "-p" && i + 1 < argc) {
        opts.zmq_port = std::atoi(argv[++i]);
    } else if (arg == "-m") {
        opts.monitor_mode = true;
    } else if (arg == "-t") {
        opts.mode_sysclk = true;
    } else if (arg == "-s" && i + 1 < argc) {
        opts.storage_dir = argv[++i];
    } else {
        return false;
    }
    return true;
}

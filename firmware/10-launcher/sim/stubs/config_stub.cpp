/**
 * config_stub.cpp — host stub for config::network() used by about.cpp and settings.cpp.
 *
 * about.cpp uses config::network().hostname, settings.cpp uses the same.
 * The real config.cpp reads LittleFS; on the host sim there is no filesystem,
 * so we return a fixed default NetworkCfg.
 */
#include "../../src/os/config.h"
#include <cstring>

namespace config {

static NetworkCfg s_network = { "", "", "espscreen" };

const NetworkCfg& network() { return s_network; }

} // namespace config

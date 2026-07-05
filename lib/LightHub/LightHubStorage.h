#pragma once

#ifdef ARDUINO

#include "LightHubRegistry.h"

namespace LightHub {

extern const char REGISTRY_FILE[];

bool loadRegistry(Registry& registry);  // true if the file existed and parsed
bool saveRegistry(Registry& registry);  // true on successful write

}

#endif

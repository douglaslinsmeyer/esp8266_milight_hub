#pragma once

#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <vector>

namespace LightHub {

static const uint16_t HUB_DEVICE_ID_FLOOR = 0x2000;
static const size_t MAX_NAME_LEN = 32;         // fixture names become aliases (MAX_ALIAS_LEN)
static const size_t MAX_GROUP_NAME_LEN = 48;   // group names are registry-only free text

enum class Kind : uint8_t { RGB_CCT = 0, DUAL_WHITE = 1, RGBW = 2, RGB = 3, UNKNOWN = 255 };

const char* kindToString(Kind k);
Kind kindFromString(const char* s);
const char* kindToProtocol(Kind k);       // milight remote-type string for MiLightRemoteConfig::fromType
uint8_t fixtureGroup(Kind k);             // RF group a fixture pairs on: 1, or 0 for rgb
bool isValidSlug(const char* name);       // [a-z0-9_]{1,MAX_NAME_LEN}
bool isValidGroupName(const char* name);  // 1..MAX_GROUP_NAME_LEN printable ASCII (0x20-0x7E)

enum class FixtureStatus : uint8_t { UNPAIRED = 0, PAIRED = 1 };

struct Fixture {
  uint16_t id;
  char name[MAX_NAME_LEN + 1];
  Kind kind;
  uint16_t deviceId;   // this fixture's own device ID (>= HUB_DEVICE_ID_FLOOR)
  uint8_t group;       // fixtureGroup(kind), stored for convenience
  FixtureStatus status;
};

struct DeviceGroup {
  uint16_t id;
  char name[MAX_GROUP_NAME_LEN + 1];
  std::vector<uint16_t> fixtureIds;  // ordered, no duplicates

  bool hasMember(uint16_t fixtureId) const {
    for (uint16_t fid : fixtureIds) {
      if (fid == fixtureId) return true;
    }
    return false;
  }
};

}

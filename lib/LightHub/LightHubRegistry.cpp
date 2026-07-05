#include "LightHubRegistry.h"

namespace LightHub {

const char* resultToString(Result r) {
  switch (r) {
    case Result::OK: return "ok";
    case Result::NAME_TAKEN: return "name already in use";
    case Result::INVALID_NAME: return "invalid name";
    case Result::NOT_FOUND: return "not found";
    case Result::INVALID_KIND: return "invalid kind";
    default: return "unknown error";
  }
}

Registry::Registry()
  : nextDeviceIdCounter(HUB_DEVICE_ID_FLOOR)
  , nextFixtureId(1)
  , nextGroupId(1)
{ }

void Registry::copyStr(char* dst, const char* src, size_t cap) {
  if (src == nullptr) {
    dst[0] = '\0';
    return;
  }
  strncpy(dst, src, cap);
  dst[cap] = '\0';
}

uint16_t Registry::allocateDeviceId() {
  if (nextDeviceIdCounter < HUB_DEVICE_ID_FLOOR) {
    nextDeviceIdCounter = HUB_DEVICE_ID_FLOOR;
  }
  // monotonic counter; defensively skip any ID a loaded registry already uses
  bool inUse = true;
  while (inUse) {
    inUse = false;
    for (const auto& f : fixtureList) {
      if (f.deviceId == nextDeviceIdCounter) {
        inUse = true;
        nextDeviceIdCounter++;
        break;
      }
    }
  }
  return nextDeviceIdCounter++;
}

bool Registry::fixtureNameInUse(const char* name) const {
  for (const auto& f : fixtureList) {
    if (strcmp(f.name, name) == 0) return true;
  }
  return false;
}

Result Registry::createFixture(const char* name, Kind kind, Fixture** out) {
  if (!isValidSlug(name)) return Result::INVALID_NAME;
  if (kind == Kind::UNKNOWN) return Result::INVALID_KIND;
  if (fixtureNameInUse(name)) return Result::NAME_TAKEN;

  fixtureList.emplace_back();
  Fixture& f = fixtureList.back();
  f.id = nextFixtureId++;
  copyStr(f.name, name, MAX_NAME_LEN);
  f.kind = kind;
  f.deviceId = allocateDeviceId();
  f.group = fixtureGroup(kind);
  f.status = FixtureStatus::UNPAIRED;
  if (out) *out = &f;
  return Result::OK;
}

Result Registry::renameFixture(uint16_t fixtureId, const char* newName) {
  Fixture* f = findFixture(fixtureId);
  if (f == nullptr) return Result::NOT_FOUND;
  if (!isValidSlug(newName)) return Result::INVALID_NAME;
  if (strcmp(f->name, newName) != 0 && fixtureNameInUse(newName)) return Result::NAME_TAKEN;
  copyStr(f->name, newName, MAX_NAME_LEN);
  return Result::OK;
}

Result Registry::deleteFixture(uint16_t fixtureId) {
  for (auto it = fixtureList.begin(); it != fixtureList.end(); ++it) {
    if (it->id != fixtureId) continue;
    // remove membership everywhere before erasing
    for (auto& g : groupList) {
      for (auto mit = g.fixtureIds.begin(); mit != g.fixtureIds.end(); ) {
        if (*mit == fixtureId) {
          mit = g.fixtureIds.erase(mit);
        } else {
          ++mit;
        }
      }
    }
    fixtureList.erase(it);
    return Result::OK;
  }
  return Result::NOT_FOUND;
}

Fixture* Registry::findFixture(uint16_t fixtureId) {
  for (auto& f : fixtureList) {
    if (f.id == fixtureId) return &f;
  }
  return nullptr;
}

}

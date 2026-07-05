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

bool Registry::groupNameInUse(const char* name) const {
  for (const auto& g : groupList) {
    if (strcmp(g.name, name) == 0) return true;
  }
  return false;
}

Result Registry::createGroup(const char* name, DeviceGroup** out) {
  if (!isValidGroupName(name)) return Result::INVALID_NAME;
  if (groupNameInUse(name)) return Result::NAME_TAKEN;

  groupList.emplace_back();
  DeviceGroup& g = groupList.back();
  g.id = nextGroupId++;
  copyStr(g.name, name, MAX_GROUP_NAME_LEN);
  if (out) *out = &g;
  return Result::OK;
}

Result Registry::renameGroup(uint16_t groupId, const char* newName) {
  DeviceGroup* g = findGroup(groupId);
  if (g == nullptr) return Result::NOT_FOUND;
  if (!isValidGroupName(newName)) return Result::INVALID_NAME;
  if (strcmp(g->name, newName) != 0 && groupNameInUse(newName)) return Result::NAME_TAKEN;
  copyStr(g->name, newName, MAX_GROUP_NAME_LEN);
  return Result::OK;
}

Result Registry::deleteGroup(uint16_t groupId) {
  for (auto it = groupList.begin(); it != groupList.end(); ++it) {
    if (it->id == groupId) {
      groupList.erase(it);
      return Result::OK;
    }
  }
  return Result::NOT_FOUND;
}

DeviceGroup* Registry::findGroup(uint16_t groupId) {
  for (auto& g : groupList) {
    if (g.id == groupId) return &g;
  }
  return nullptr;
}

Result Registry::addMember(uint16_t groupId, uint16_t fixtureId) {
  DeviceGroup* g = findGroup(groupId);
  if (g == nullptr) return Result::NOT_FOUND;
  if (findFixture(fixtureId) == nullptr) return Result::NOT_FOUND;
  if (!g->hasMember(fixtureId)) {
    g->fixtureIds.push_back(fixtureId);
  }
  return Result::OK;
}

Result Registry::removeMember(uint16_t groupId, uint16_t fixtureId) {
  DeviceGroup* g = findGroup(groupId);
  if (g == nullptr) return Result::NOT_FOUND;
  for (auto it = g->fixtureIds.begin(); it != g->fixtureIds.end(); ++it) {
    if (*it == fixtureId) {
      g->fixtureIds.erase(it);
      break;
    }
  }
  return Result::OK;
}

Result Registry::setMembers(uint16_t groupId, const std::vector<uint16_t>& fixtureIds) {
  DeviceGroup* g = findGroup(groupId);
  if (g == nullptr) return Result::NOT_FOUND;
  // all-or-nothing: validate every id before mutating
  for (uint16_t fid : fixtureIds) {
    if (findFixture(fid) == nullptr) return Result::NOT_FOUND;
  }
  std::vector<uint16_t> deduped;
  for (uint16_t fid : fixtureIds) {
    bool seen = false;
    for (uint16_t d : deduped) {
      if (d == fid) { seen = true; break; }
    }
    if (!seen) deduped.push_back(fid);
  }
  g->fixtureIds = deduped;
  return Result::OK;
}

size_t Registry::groupCountForFixture(uint16_t fixtureId) const {
  size_t n = 0;
  for (const auto& g : groupList) {
    if (g.hasMember(fixtureId)) n++;
  }
  return n;
}

}

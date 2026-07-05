#pragma once

#include "LightHubModel.h"
#include <ArduinoJson.h>
#include <list>

namespace LightHub {

enum class Result : uint8_t {
  OK = 0,
  NAME_TAKEN,
  INVALID_NAME,
  NOT_FOUND,
  INVALID_KIND
};

const char* resultToString(Result r);

class Registry {
 public:
  Registry();

  // fixtures (Task 2)
  Result createFixture(const char* name, Kind kind, Fixture** out = nullptr);
  Result renameFixture(uint16_t fixtureId, const char* newName);
  Result deleteFixture(uint16_t fixtureId);  // also removes it from every group
  Fixture* findFixture(uint16_t fixtureId);
  const std::list<Fixture>& fixtures() const { return fixtureList; }
  bool fixtureNameInUse(const char* name) const;
  uint16_t peekNextDeviceId() const { return nextDeviceIdCounter; }

  // groups (Task 3)
  Result createGroup(const char* name, DeviceGroup** out = nullptr);
  Result renameGroup(uint16_t groupId, const char* newName);
  Result deleteGroup(uint16_t groupId);  // never touches fixtures
  DeviceGroup* findGroup(uint16_t groupId);
  const std::list<DeviceGroup>& groups() const { return groupList; }
  bool groupNameInUse(const char* name) const;
  Result addMember(uint16_t groupId, uint16_t fixtureId);       // idempotent
  Result removeMember(uint16_t groupId, uint16_t fixtureId);    // idempotent
  Result setMembers(uint16_t groupId, const std::vector<uint16_t>& fixtureIds);
  size_t groupCountForFixture(uint16_t fixtureId) const;

  // serialization (Task 4)
  void toJson(JsonDocument& doc) const;
  bool fromJson(JsonVariantConst src);

 private:
  std::list<Fixture> fixtureList;
  std::list<DeviceGroup> groupList;
  uint16_t nextDeviceIdCounter;
  uint16_t nextFixtureId;
  uint16_t nextGroupId;

  uint16_t allocateDeviceId();
  static void copyStr(char* dst, const char* src, size_t cap);
};

}

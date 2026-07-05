#include "LightHubModel.h"

namespace LightHub {

const char* kindToString(Kind k) {
  switch (k) {
    case Kind::RGB_CCT: return "rgb_cct";
    case Kind::DUAL_WHITE: return "dual_white";
    case Kind::RGBW: return "rgbw";
    case Kind::RGB: return "rgb";
    default: return "unknown";
  }
}

Kind kindFromString(const char* s) {
  if (s == nullptr) return Kind::UNKNOWN;
  if (strcmp(s, "rgb_cct") == 0) return Kind::RGB_CCT;
  if (strcmp(s, "dual_white") == 0) return Kind::DUAL_WHITE;
  if (strcmp(s, "rgbw") == 0) return Kind::RGBW;
  if (strcmp(s, "rgb") == 0) return Kind::RGB;
  return Kind::UNKNOWN;
}

const char* kindToProtocol(Kind k) {
  switch (k) {
    case Kind::RGB_CCT: return "fut089";
    case Kind::DUAL_WHITE: return "fut091";
    case Kind::RGBW: return "rgbw";
    case Kind::RGB: return "rgb";
    default: return "unknown";
  }
}

uint8_t fixtureGroup(Kind k) {
  return (k == Kind::RGB) ? 0 : 1;
}

bool isValidSlug(const char* name) {
  if (name == nullptr) return false;
  const size_t len = strnlen(name, MAX_NAME_LEN + 1);
  if (len == 0 || len > MAX_NAME_LEN) return false;
  for (size_t i = 0; i < len; i++) {
    const char c = name[i];
    const bool ok = (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_';
    if (!ok) return false;
  }
  return true;
}

bool isValidGroupName(const char* name) {
  if (name == nullptr) return false;
  const size_t len = strnlen(name, MAX_GROUP_NAME_LEN + 1);
  if (len == 0 || len > MAX_GROUP_NAME_LEN) return false;
  for (size_t i = 0; i < len; i++) {
    const char c = name[i];
    if (c < 0x20 || c > 0x7E) return false;
  }
  return true;
}

}

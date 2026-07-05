#ifdef ARDUINO

#include "LightHubStorage.h"

#include <Arduino.h>
#include <FS.h>
#ifdef ESP32
  #include <SPIFFS.h>
#endif
#include <ProjectFS.h>

#ifndef LIGHT_REGISTRY_BUFFER_SIZE
#define LIGHT_REGISTRY_BUFFER_SIZE 8192
#endif

namespace LightHub {

const char REGISTRY_FILE[] = "/light_registry.json";

bool loadRegistry(Registry& registry) {
  if (!ProjectFS.exists(REGISTRY_FILE)) {
    return false;
  }
  File f = ProjectFS.open(REGISTRY_FILE, "r");
  if (!f) {
    return false;
  }
  DynamicJsonDocument doc(LIGHT_REGISTRY_BUFFER_SIZE);
  const DeserializationError err = deserializeJson(doc, f);
  f.close();
  if (err) {
    Serial.printf_P(PSTR("LightHub: failed to parse %s: %s\n"), REGISTRY_FILE, err.c_str());
    return false;
  }
  return registry.fromJson(doc.as<JsonVariantConst>());
}

bool saveRegistry(Registry& registry) {
  DynamicJsonDocument doc(LIGHT_REGISTRY_BUFFER_SIZE);
  if (doc.capacity() == 0) {
    Serial.println(F("LightHub: registry buffer allocation failed"));
    return false;
  }
  registry.toJson(doc);
  if (doc.overflowed()) {
    Serial.println(F("LightHub: registry exceeds LIGHT_REGISTRY_BUFFER_SIZE"));
    return false;
  }

  File f = ProjectFS.open(REGISTRY_FILE, "w");
  if (!f) {
    Serial.println(F("LightHub: failed to open registry file for writing"));
    return false;
  }
  const size_t expected = measureJson(doc);
  const size_t written = serializeJson(doc, f);
  f.close();
  if (written != expected) {
    Serial.printf_P(PSTR("LightHub: short write to %s (%u of %u bytes)\n"),
        REGISTRY_FILE, (unsigned) written, (unsigned) expected);
    return false;
  }
  return true;
}

}

#endif

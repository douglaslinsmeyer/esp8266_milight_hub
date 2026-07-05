#include <FS.h>
#include <MiLightHttpServer.h>
#include <LightHubStorage.h>
#include <MiLightRemoteConfig.h>

using namespace std::placeholders;

void MiLightHttpServer::bindLightHubRoutes() {
  server
    .buildHandler("/fixtures")
    // onSimple: the handler streams its own chunked response, so the framework must not
    // auto-send a JSON response afterwards (same reason handleListGroups uses onSimple)
    .onSimple(HTTP_GET, std::bind(&MiLightHttpServer::handleListFixtures, this))
    .on(HTTP_POST, std::bind(&MiLightHttpServer::handleCreateFixture, this, _1));

  server
    .buildHandler("/fixtures/:fixture_id")
    .on(HTTP_GET, std::bind(&MiLightHttpServer::handleGetFixture, this, _1))
    .on(HTTP_PUT, std::bind(&MiLightHttpServer::handleUpdateFixture, this, _1))
    .on(HTTP_DELETE, std::bind(&MiLightHttpServer::handleDeleteFixture, this, _1));

  server
    .buildHandler("/fixtures/:fixture_id/state")
    .on(HTTP_PUT, std::bind(&MiLightHttpServer::handleFixtureState, this, _1));

  server
    .buildHandler("/fixtures/:fixture_id/pair")
    .on(HTTP_POST, std::bind(&MiLightHttpServer::handlePairFixture, this, _1));

  server
    .buildHandler("/fixtures/:fixture_id/blink")
    .on(HTTP_POST, std::bind(&MiLightHttpServer::handleBlinkFixture, this, _1));

  server
    .buildHandler("/light_registry.json")
    .onSimple(HTTP_GET, std::bind(&MiLightHttpServer::serveFile, this, LightHub::REGISTRY_FILE, "application/json"));

  server
    .buildHandler("/groups")
    .on(HTTP_GET, std::bind(&MiLightHttpServer::handleListGroupsLH, this, _1))
    .on(HTTP_POST, std::bind(&MiLightHttpServer::handleCreateGroupLH, this, _1));

  server
    .buildHandler("/groups/:group_id")
    .on(HTTP_GET, std::bind(&MiLightHttpServer::handleGetGroupLH, this, _1))
    .on(HTTP_PUT, std::bind(&MiLightHttpServer::handleUpdateGroupLH, this, _1))
    .on(HTTP_DELETE, std::bind(&MiLightHttpServer::handleDeleteGroupLH, this, _1));

  server
    .buildHandler("/groups/:group_id/state")
    .on(HTTP_PUT, std::bind(&MiLightHttpServer::handleGroupState, this, _1));

  server
    .buildHandler("/groups/:group_id/members/:fixture_id")
    .on(HTTP_POST, std::bind(&MiLightHttpServer::handleAddGroupMember, this, _1))
    .on(HTTP_DELETE, std::bind(&MiLightHttpServer::handleRemoveGroupMember, this, _1));
}

// ---- helpers ----

bool MiLightHttpServer::lightHubFixtureNameAvailable(const char* name) {
  return !lightHubRegistry.fixtureNameInUse(name)
    && settings.groupIdAliases.find(String(name)) == settings.groupIdAliases.end();
}

bool MiLightHttpServer::lightHubAddAlias(const char* name, LightHub::Kind kind, uint16_t deviceId, uint8_t group) {
  const MiLightRemoteConfig* config = MiLightRemoteConfig::fromType(LightHub::kindToProtocol(kind));
  if (config == nullptr) {
    return false;
  }
  settings.addAlias(name, BulbId(deviceId, group, config->type));
  return true;
}

void MiLightHttpServer::lightHubDeleteAliasByName(const char* name) {
  auto it = settings.groupIdAliases.find(String(name));
  if (it != settings.groupIdAliases.end()) {
    settings.deleteAlias(it->second.id);  // records deletion so HA discovery prunes the entity
  }
}

void MiLightHttpServer::lightHubWriteError(RequestContext& request, LightHub::Result result) {
  request.response.setCode(result == LightHub::Result::NOT_FOUND ? 404 : 400);
  request.response.json[F("error")] = LightHub::resultToString(result);
}

void MiLightHttpServer::lightHubFixtureJson(const LightHub::Fixture& f, JsonObject out) {
  out[F("id")] = f.id;
  out[F("name")] = f.name;
  out[F("kind")] = LightHub::kindToString(f.kind);
  out[F("protocol")] = LightHub::kindToProtocol(f.kind);
  out[F("device_id")] = f.deviceId;
  out[F("group")] = f.group;
  out[F("status")] = (f.status == LightHub::FixtureStatus::PAIRED) ? "paired" : "unpaired";
  out[F("group_count")] = lightHubRegistry.groupCountForFixture(f.id);
}

// ---- /fixtures ----

void MiLightHttpServer::handleListFixtures() {
  // Streamed chunked like handleListGroups (MiLightHttpServer.cpp:1011) — mirror its
  // chunk framing exactly if this differs.
  server.setContentLength(CONTENT_LENGTH_UNKNOWN);
  server.send(200, "application/json", "");
  WiFiClient client = server.client();

  String chunk;
  chunk.reserve(512);
  auto sendChunk = [&client](const String& s) {
    if (s.length() == 0) return;
    client.printf("%zx\r\n", (size_t)s.length());
    client.print(s);
    client.print("\r\n");
  };

  sendChunk(String(F("{\"fixtures\":[")));
  bool first = true;
  for (const auto& f : lightHubRegistry.fixtures()) {
    StaticJsonDocument<512> item;
    JsonObject obj = item.to<JsonObject>();
    lightHubFixtureJson(f, obj);
    chunk = first ? "" : ",";
    serializeJson(item, chunk);  // ArduinoJson appends to String
    sendChunk(chunk);
    first = false;
    yield();
  }
  sendChunk(String(F("]}")));
  client.print("0\r\n\r\n");  // chunked-body terminator
  client.stop();
}

void MiLightHttpServer::handleCreateFixture(RequestContext& request) {
  JsonObject body = request.getJsonBody().as<JsonObject>();
  if (body.isNull() || !body.containsKey(F("name"))) {
    request.response.setCode(400);
    request.response.json[F("error")] = F("must specify name");
    return;
  }
  const char* name = body[F("name")].as<const char*>();  // explicit .as<>: this ArduinoJson has no implicit const char* conversion
  const LightHub::Kind kind = LightHub::kindFromString(body[F("kind")] | "rgb_cct");
  if (kind == LightHub::Kind::UNKNOWN) {
    request.response.setCode(400);
    request.response.json[F("error")] = F("invalid kind (rgb_cct|dual_white|rgbw|rgb)");
    return;
  }
  if (!lightHubFixtureNameAvailable(name)) {
    request.response.setCode(400);
    request.response.json[F("error")] = F("name already in use");
    return;
  }

  LightHub::Fixture* fixture = nullptr;
  const LightHub::Result result = lightHubRegistry.createFixture(name, kind, &fixture);
  if (result != LightHub::Result::OK) {
    lightHubWriteError(request, result);
    return;
  }

  const bool aliasOk = lightHubAddAlias(fixture->name, kind, fixture->deviceId, fixture->group);
  const bool registryOk = LightHub::saveRegistry(lightHubRegistry);
  saveSettings();  // persists aliases; settingsSavedHandler → applySettings → HA discovery republish; keep in sync regardless

  if (!aliasOk || !registryOk) {
    request.response.setCode(500);
    request.response.json[F("error")] = F("failed to persist fixture");
    return;
  }

  lightHubFixtureJson(*fixture, request.response.json.to<JsonObject>());
}

void MiLightHttpServer::handleGetFixture(RequestContext& request) {
  LightHub::Fixture* fixture = lightHubRegistry.findFixture(atoi(request.pathVariables.get("fixture_id")));
  if (fixture == nullptr) {
    request.response.setCode(404);
    request.response.json[F("error")] = F("fixture not found");
    return;
  }
  JsonObject out = request.response.json.to<JsonObject>();
  lightHubFixtureJson(*fixture, out);
  JsonArray groupIds = out.createNestedArray(F("group_ids"));
  for (const auto& g : lightHubRegistry.groups()) {
    if (g.hasMember(fixture->id)) {
      groupIds.add(g.id);
    }
  }
}

void MiLightHttpServer::handleUpdateFixture(RequestContext& request) {
  LightHub::Fixture* fixture = lightHubRegistry.findFixture(atoi(request.pathVariables.get("fixture_id")));
  if (fixture == nullptr) {
    request.response.setCode(404);
    request.response.json[F("error")] = F("fixture not found");
    return;
  }
  JsonObject body = request.getJsonBody().as<JsonObject>();
  if (body.isNull() || !body.containsKey(F("name"))) {
    request.response.setCode(400);
    request.response.json[F("error")] = F("must specify name");
    return;
  }
  const char* newName = body[F("name")].as<const char*>();
  // validate EVERYTHING before touching the alias store — a failed rename must not lose the alias
  if (!LightHub::isValidSlug(newName)) {
    request.response.setCode(400);
    request.response.json[F("error")] = F("invalid name");
    return;
  }
  if (strcmp(fixture->name, newName) == 0) {
    lightHubFixtureJson(*fixture, request.response.json.to<JsonObject>());
    return;  // rename to self: no-op
  }
  if (!lightHubFixtureNameAvailable(newName)) {
    request.response.setCode(400);
    request.response.json[F("error")] = F("name already in use");
    return;
  }

  lightHubDeleteAliasByName(fixture->name);
  lightHubRegistry.renameFixture(fixture->id, newName);
  const bool aliasOk = lightHubAddAlias(fixture->name, fixture->kind, fixture->deviceId, fixture->group);
  const bool registryOk = LightHub::saveRegistry(lightHubRegistry);
  saveSettings();  // keep alias store persistence + discovery in sync with RAM state regardless

  if (!aliasOk || !registryOk) {
    request.response.setCode(500);
    request.response.json[F("error")] = F("failed to persist fixture");
    return;
  }

  lightHubFixtureJson(*fixture, request.response.json.to<JsonObject>());
}

// ---- fixture RF operations ----

static const size_t LIGHT_HUB_RF_REPEATS = 10;  // short bursts → many land in the pairing window

// resolves :fixture_id to a fixture + its remote config; returns false after writing the error
bool MiLightHttpServer::lightHubResolveFixture(RequestContext& request,
    LightHub::Fixture** fixtureOut, const MiLightRemoteConfig** configOut) {
  LightHub::Fixture* fixture = lightHubRegistry.findFixture(atoi(request.pathVariables.get("fixture_id")));
  if (fixture == nullptr) {
    request.response.setCode(404);
    request.response.json[F("error")] = F("fixture not found");
    return false;
  }
  const MiLightRemoteConfig* config = MiLightRemoteConfig::fromType(LightHub::kindToProtocol(fixture->kind));
  if (config == nullptr) {
    request.response.setCode(500);
    request.response.json[F("error")] = F("fixture kind has no remote config");
    return false;
  }
  *fixtureOut = fixture;
  *configOut = config;
  return true;
}

// drains the packet queue, then keeps servicing it until waitMs has elapsed
void MiLightHttpServer::lightHubDrainAndWait(unsigned long waitMs) {
  const unsigned long start = millis();
  while (packetSender->isSending() || (millis() - start) < waitMs) {
    packetSender->loop();
    yield();
  }
}

void MiLightHttpServer::handleFixtureState(RequestContext& request) {
  LightHub::Fixture* fixture = nullptr;
  const MiLightRemoteConfig* config = nullptr;
  if (!lightHubResolveFixture(request, &fixture, &config)) return;

  JsonObject body = request.getJsonBody().as<JsonObject>();
  if (body.isNull()) {
    request.response.setCode(400);
    request.response.json[F("error")] = F("must send a command body");
    return;
  }
  milightClient->prepare(config, fixture->deviceId, fixture->group);
  handleRequest(body);

  BulbId bulbId(fixture->deviceId, fixture->group, config->type);
  sendGroupState(false, bulbId, request.response);
}

void MiLightHttpServer::handlePairFixture(RequestContext& request) {
  LightHub::Fixture* fixture = nullptr;
  const MiLightRemoteConfig* config = nullptr;
  if (!lightHubResolveFixture(request, &fixture, &config)) return;

  JsonObject body = request.getJsonBody().as<JsonObject>();
  long durationMs = body.isNull() ? 3000 : (body[F("duration_ms")] | 3000);
  if (durationMs < 500) durationMs = 500;
  if (durationMs > 10000) durationMs = 10000;

  milightClient->prepare(config, fixture->deviceId, fixture->group);
  milightClient->setRepeatsOverride(LIGHT_HUB_RF_REPEATS);
  const unsigned long start = millis();
  size_t bursts = 0;
  while ((millis() - start) < (unsigned long) durationMs) {
    milightClient->pair();
    lightHubDrainAndWait(0);
    bursts++;
    yield();
  }
  milightClient->clearRepeatsOverride();

  fixture->status = LightHub::FixtureStatus::PAIRED;
  const bool registryOk = LightHub::saveRegistry(lightHubRegistry);
  if (!registryOk) {
    request.response.setCode(500);
    request.response.json[F("error")] = F("failed to persist fixture status");
    return;
  }
  request.response.json[F("success")] = true;
  request.response.json[F("bursts")] = bursts;
  request.response.json[F("duration_ms")] = durationMs;
}

void MiLightHttpServer::handleBlinkFixture(RequestContext& request) {
  LightHub::Fixture* fixture = nullptr;
  const MiLightRemoteConfig* config = nullptr;
  if (!lightHubResolveFixture(request, &fixture, &config)) return;

  JsonObject body = request.getJsonBody().as<JsonObject>();
  long cycles = body.isNull() ? 2 : (body[F("cycles")] | 2);
  if (cycles < 1) cycles = 1;
  if (cycles > 5) cycles = 5;

  StaticJsonDocument<64> onDoc;
  onDoc[F("status")] = "ON";
  StaticJsonDocument<64> offDoc;
  offDoc[F("status")] = "OFF";

  milightClient->prepare(config, fixture->deviceId, fixture->group);
  milightClient->setRepeatsOverride(LIGHT_HUB_RF_REPEATS);
  for (long i = 0; i < cycles; i++) {
    milightClient->update(offDoc.as<JsonObject>());
    lightHubDrainAndWait(400);
    milightClient->update(onDoc.as<JsonObject>());
    lightHubDrainAndWait(400);
  }
  milightClient->clearRepeatsOverride();

  request.response.json[F("success")] = true;
  request.response.json[F("cycles")] = cycles;
}

void MiLightHttpServer::handleDeleteFixture(RequestContext& request) {
  LightHub::Fixture* fixture = nullptr;
  const MiLightRemoteConfig* config = nullptr;
  if (!lightHubResolveFixture(request, &fixture, &config)) return;

  JsonObject body = request.getJsonBody().as<JsonObject>();
  const bool rfUnpair = body.isNull() ? true : (body[F("rf_unpair")] | true);
  long durationMs = body.isNull() ? 3000 : (body[F("duration_ms")] | 3000);
  if (durationMs < 500) durationMs = 500;
  if (durationMs > 10000) durationMs = 10000;

  if (rfUnpair) {
    // same window mechanics as pair: caller power-cycles the fixture first
    milightClient->prepare(config, fixture->deviceId, fixture->group);
    milightClient->setRepeatsOverride(LIGHT_HUB_RF_REPEATS);
    const unsigned long start = millis();
    while ((millis() - start) < (unsigned long) durationMs) {
      milightClient->unpair();
      lightHubDrainAndWait(0);
      yield();
    }
    milightClient->clearRepeatsOverride();
  }

  lightHubDeleteAliasByName(fixture->name);
  lightHubRegistry.deleteFixture(fixture->id);  // also strips it from every group
  const bool registryOk = LightHub::saveRegistry(lightHubRegistry);
  saveSettings();  // persists alias deletion regardless, keeping the alias store in sync with RAM state

  if (!registryOk) {
    request.response.setCode(500);
    request.response.json[F("error")] = F("failed to persist fixture deletion");
    return;
  }
  request.response.json[F("success")] = true;
  request.response.json[F("rf_unpair")] = rfUnpair;
}

// ---- device groups ----

void MiLightHttpServer::lightHubGroupJson(const LightHub::DeviceGroup& g, JsonObject out, bool includeMembers) {
  out[F("id")] = g.id;
  out[F("name")] = g.name;
  out[F("fixture_count")] = g.fixtureIds.size();
  if (includeMembers) {
    JsonArray members = out.createNestedArray(F("fixture_ids"));
    for (uint16_t fid : g.fixtureIds) {
      members.add(fid);
    }
  }
}

void MiLightHttpServer::handleListGroupsLH(RequestContext& request) {
  JsonArray groups = request.response.json.createNestedArray(F("groups"));
  for (const auto& g : lightHubRegistry.groups()) {
    lightHubGroupJson(g, groups.createNestedObject(), false);
  }
}

void MiLightHttpServer::handleCreateGroupLH(RequestContext& request) {
  JsonObject body = request.getJsonBody().as<JsonObject>();
  if (body.isNull() || !body.containsKey(F("name"))) {
    request.response.setCode(400);
    request.response.json[F("error")] = F("must specify name");
    return;
  }
  LightHub::DeviceGroup* group = nullptr;
  const LightHub::Result result = lightHubRegistry.createGroup(body[F("name")].as<const char*>(), &group);
  if (result != LightHub::Result::OK) {
    lightHubWriteError(request, result);
    return;
  }
  if (body.containsKey(F("fixture_ids"))) {
    std::vector<uint16_t> ids;
    for (JsonVariant v : body[F("fixture_ids")].as<JsonArray>()) {
      ids.push_back(v | 0);
    }
    const LightHub::Result memberResult = lightHubRegistry.setMembers(group->id, ids);
    if (memberResult != LightHub::Result::OK) {
      lightHubRegistry.deleteGroup(group->id);  // nothing half-assigned on abort
      lightHubWriteError(request, memberResult);
      return;
    }
  }
  const bool registryOk = LightHub::saveRegistry(lightHubRegistry);
  if (!registryOk) {
    request.response.setCode(500);
    request.response.json[F("error")] = F("failed to persist group");
    return;
  }
  lightHubGroupJson(*group, request.response.json.to<JsonObject>(), true);
}

void MiLightHttpServer::handleGetGroupLH(RequestContext& request) {
  LightHub::DeviceGroup* group = lightHubRegistry.findGroup(atoi(request.pathVariables.get("group_id")));
  if (group == nullptr) {
    request.response.setCode(404);
    request.response.json[F("error")] = F("group not found");
    return;
  }
  lightHubGroupJson(*group, request.response.json.to<JsonObject>(), true);
}

void MiLightHttpServer::handleUpdateGroupLH(RequestContext& request) {
  LightHub::DeviceGroup* group = lightHubRegistry.findGroup(atoi(request.pathVariables.get("group_id")));
  if (group == nullptr) {
    request.response.setCode(404);
    request.response.json[F("error")] = F("group not found");
    return;
  }
  JsonObject body = request.getJsonBody().as<JsonObject>();
  if (body.isNull() || (!body.containsKey(F("name")) && !body.containsKey(F("fixture_ids")))) {
    request.response.setCode(400);
    request.response.json[F("error")] = F("must specify name and/or fixture_ids");
    return;
  }
  if (body.containsKey(F("name"))) {
    const LightHub::Result result = lightHubRegistry.renameGroup(group->id, body[F("name")].as<const char*>());
    if (result != LightHub::Result::OK) {
      lightHubWriteError(request, result);
      return;
    }
  }
  if (body.containsKey(F("fixture_ids"))) {
    std::vector<uint16_t> ids;
    for (JsonVariant v : body[F("fixture_ids")].as<JsonArray>()) {
      ids.push_back(v | 0);
    }
    const LightHub::Result result = lightHubRegistry.setMembers(group->id, ids);
    if (result != LightHub::Result::OK) {
      lightHubWriteError(request, result);
      return;
    }
  }
  const bool registryOk = LightHub::saveRegistry(lightHubRegistry);
  if (!registryOk) {
    request.response.setCode(500);
    request.response.json[F("error")] = F("failed to persist group");
    return;
  }
  lightHubGroupJson(*group, request.response.json.to<JsonObject>(), true);
}

void MiLightHttpServer::handleDeleteGroupLH(RequestContext& request) {
  const LightHub::Result result = lightHubRegistry.deleteGroup(atoi(request.pathVariables.get("group_id")));
  if (result != LightHub::Result::OK) {
    lightHubWriteError(request, result);
    return;
  }
  const bool registryOk = LightHub::saveRegistry(lightHubRegistry);
  if (!registryOk) {
    request.response.setCode(500);
    request.response.json[F("error")] = F("failed to persist group deletion");
    return;
  }
  request.response.json[F("success")] = true;
}

void MiLightHttpServer::handleGroupState(RequestContext& request) {
  LightHub::DeviceGroup* group = lightHubRegistry.findGroup(atoi(request.pathVariables.get("group_id")));
  if (group == nullptr) {
    request.response.setCode(404);
    request.response.json[F("error")] = F("group not found");
    return;
  }
  JsonObject body = request.getJsonBody().as<JsonObject>();
  if (body.isNull()) {
    request.response.setCode(400);
    request.response.json[F("error")] = F("must send a command body");
    return;
  }
  size_t sent = 0;
  for (uint16_t fid : group->fixtureIds) {
    LightHub::Fixture* fixture = lightHubRegistry.findFixture(fid);
    if (fixture == nullptr) continue;
    const MiLightRemoteConfig* config = MiLightRemoteConfig::fromType(LightHub::kindToProtocol(fixture->kind));
    if (config == nullptr) continue;
    milightClient->prepare(config, fixture->deviceId, fixture->group);
    handleRequest(body);  // one unicast per member — software fan-out
    // packet queue caps at MILIGHT_MAX_QUEUED_PACKETS (20) and drops overflow:
    // drain fully between members so large groups don't silently lose commands
    while (packetSender->isSending()) {
      packetSender->loop();
      yield();
    }
    sent++;
  }
  request.response.json[F("success")] = true;
  request.response.json[F("sent")] = sent;
}

void MiLightHttpServer::handleAddGroupMember(RequestContext& request) {
  const LightHub::Result result = lightHubRegistry.addMember(
    atoi(request.pathVariables.get("group_id")),
    atoi(request.pathVariables.get("fixture_id")));
  if (result != LightHub::Result::OK) {
    lightHubWriteError(request, result);
    return;
  }
  const bool registryOk = LightHub::saveRegistry(lightHubRegistry);
  if (!registryOk) {
    request.response.setCode(500);
    request.response.json[F("error")] = F("failed to persist group membership");
    return;
  }
  request.response.json[F("success")] = true;
}

void MiLightHttpServer::handleRemoveGroupMember(RequestContext& request) {
  const LightHub::Result result = lightHubRegistry.removeMember(
    atoi(request.pathVariables.get("group_id")),
    atoi(request.pathVariables.get("fixture_id")));
  if (result != LightHub::Result::OK) {
    lightHubWriteError(request, result);
    return;
  }
  const bool registryOk = LightHub::saveRegistry(lightHubRegistry);
  if (!registryOk) {
    request.response.setCode(500);
    request.response.json[F("error")] = F("failed to persist group membership");
    return;
  }
  request.response.json[F("success")] = true;
}

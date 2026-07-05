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
    .on(HTTP_PUT, std::bind(&MiLightHttpServer::handleUpdateFixture, this, _1));

  server
    .buildHandler("/light_registry.json")
    .onSimple(HTTP_GET, std::bind(&MiLightHttpServer::serveFile, this, LightHub::REGISTRY_FILE, "application/json"));
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

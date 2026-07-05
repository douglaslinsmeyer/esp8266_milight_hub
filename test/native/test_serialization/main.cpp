#include <unity.h>
#include <LightHubRegistry.h>

using namespace LightHub;

void test_round_trip_preserves_everything() {
  Registry r;
  Fixture* f1; Fixture* f2;
  r.createFixture("gazebo_post_1", Kind::RGB_CCT, &f1);
  r.createFixture("strip_1", Kind::RGB, &f2);
  f1->status = FixtureStatus::PAIRED;
  DeviceGroup* g;
  r.createGroup("Patio & Gazebo", &g);
  r.addMember(g->id, f1->id);
  r.addMember(g->id, f2->id);

  DynamicJsonDocument doc(8192);
  r.toJson(doc);

  Registry r2;
  TEST_ASSERT_TRUE(r2.fromJson(doc.as<JsonVariantConst>()));
  Fixture* f1b = r2.findFixture(f1->id);
  TEST_ASSERT_NOT_NULL(f1b);
  TEST_ASSERT_EQUAL_STRING("gazebo_post_1", f1b->name);
  TEST_ASSERT_EQUAL(Kind::RGB_CCT, f1b->kind);
  TEST_ASSERT_EQUAL_HEX16(0x2000, f1b->deviceId);
  TEST_ASSERT_EQUAL(1, f1b->group);
  TEST_ASSERT_EQUAL(FixtureStatus::PAIRED, f1b->status);
  Fixture* f2b = r2.findFixture(f2->id);
  TEST_ASSERT_EQUAL(0, f2b->group);
  TEST_ASSERT_EQUAL(FixtureStatus::UNPAIRED, f2b->status);
  DeviceGroup* gb = r2.findGroup(g->id);
  TEST_ASSERT_NOT_NULL(gb);
  TEST_ASSERT_EQUAL_STRING("Patio & Gazebo", gb->name);
  TEST_ASSERT_EQUAL(2, gb->fixtureIds.size());
  TEST_ASSERT_TRUE(gb->hasMember(f1->id));
}

void test_counters_survive_round_trip() {
  Registry r;
  Fixture* f;
  r.createFixture("temp_one", Kind::RGB_CCT, &f);
  r.deleteFixture(f->id);  // consumed fixture id 1 + device 0x2000; registry now empty

  DynamicJsonDocument doc(4096);
  r.toJson(doc);
  Registry r2;
  r2.fromJson(doc.as<JsonVariantConst>());
  Fixture* f2;
  r2.createFixture("next_one", Kind::RGB_CCT, &f2);
  TEST_ASSERT_EQUAL(2, f2->id);                    // fixture id 1 not reused
  TEST_ASSERT_EQUAL_HEX16(0x2001, f2->deviceId);   // device id not reused
}

void test_counters_clamped_and_recovered() {
  // corrupt/low counters must be clamped to the floor and to max(existing id)+1
  DynamicJsonDocument doc(4096);
  doc["version"] = 2;
  doc["counters"]["device_id"] = 5;  // below HUB_DEVICE_ID_FLOOR
  JsonObject f = doc["fixtures"].createNestedObject();
  f["id"] = 7;
  f["name"] = "post_7";
  f["kind"] = "rgb_cct";
  f["device_id"] = 0x2005;
  f["group"] = 1;
  JsonObject g = doc["groups"].createNestedObject();
  g["id"] = 4;
  g["name"] = "Patio";
  g["fixture_ids"].createNestedArray();  // ensure key exists; empty members

  Registry r;
  TEST_ASSERT_TRUE(r.fromJson(doc.as<JsonVariantConst>()));
  Fixture* nf;
  r.createFixture("post_8", Kind::RGB_CCT, &nf);
  TEST_ASSERT_EQUAL(8, nf->id);                    // max fixture id 7 + 1
  TEST_ASSERT_EQUAL_HEX16(0x2006, nf->deviceId);   // max device 0x2005 + 1
  DeviceGroup* ng;
  r.createGroup("Gazebo", &ng);
  TEST_ASSERT_EQUAL(5, ng->id);                    // max group id 4 + 1
}

void test_from_json_empty_and_malformed() {
  Registry r;
  DynamicJsonDocument empty(256);
  empty.to<JsonObject>();
  TEST_ASSERT_TRUE(r.fromJson(empty.as<JsonVariantConst>()));  // empty object = empty registry
  TEST_ASSERT_EQUAL(0, r.fixtures().size());
  TEST_ASSERT_EQUAL(0, r.groups().size());
  TEST_ASSERT_EQUAL_HEX16(0x2000, r.peekNextDeviceId());

  DynamicJsonDocument arr(256);
  arr.to<JsonArray>();
  TEST_ASSERT_FALSE(r.fromJson(arr.as<JsonVariantConst>()));   // non-object = malformed
}

void test_from_json_clears_previous_state() {
  Registry r;
  r.createFixture("old_fixture", Kind::RGB_CCT);
  r.createGroup("Old group");
  DynamicJsonDocument empty(256);
  empty.to<JsonObject>();
  TEST_ASSERT_TRUE(r.fromJson(empty.as<JsonVariantConst>()));
  TEST_ASSERT_EQUAL(0, r.fixtures().size());
  TEST_ASSERT_EQUAL(0, r.groups().size());
  TEST_ASSERT_FALSE(r.fixtureNameInUse("old_fixture"));
}

void test_sub_floor_fixture_skipped_on_load() {
  // a hand-restored/corrupt registry could carry a device_id below the hub's
  // reserved-address floor (e.g. production device 0x13CD) — such a fixture
  // must never be loaded, or RF endpoints would transmit on that address.
  DynamicJsonDocument doc(4096);
  doc["version"] = 2;
  JsonObject f = doc["fixtures"].createNestedObject();
  f["id"] = 7;
  f["name"] = "sub_floor";
  f["kind"] = "rgb_cct";
  f["device_id"] = 0x13CD;
  f["group"] = 1;

  Registry r;
  TEST_ASSERT_TRUE(r.fromJson(doc.as<JsonVariantConst>()));
  TEST_ASSERT_NULL(r.findFixture(7));

  Fixture* nf;
  r.createFixture("post_8", Kind::RGB_CCT, &nf);
  TEST_ASSERT_EQUAL(8, nf->id);                    // counter still recovered from skipped id 7
  TEST_ASSERT_EQUAL_HEX16(0x2000, nf->deviceId);   // floor reused; sub-floor device id not honored
}

void test_duplicate_membership_deduped_on_load() {
  DynamicJsonDocument doc(4096);
  doc["version"] = 2;
  JsonArray fixtures = doc.createNestedArray("fixtures");
  JsonObject f1 = fixtures.createNestedObject();
  f1["id"] = 1;
  f1["name"] = "post_1";
  f1["kind"] = "rgb_cct";
  f1["device_id"] = 0x2000;
  f1["group"] = 1;
  JsonObject f2 = fixtures.createNestedObject();
  f2["id"] = 2;
  f2["name"] = "post_2";
  f2["kind"] = "rgb_cct";
  f2["device_id"] = 0x2001;
  f2["group"] = 1;
  JsonObject g = doc["groups"].createNestedObject();
  g["id"] = 1;
  g["name"] = "Patio";
  JsonArray members = g.createNestedArray("fixture_ids");
  members.add(1);
  members.add(1);
  members.add(2);

  Registry r;
  TEST_ASSERT_TRUE(r.fromJson(doc.as<JsonVariantConst>()));
  DeviceGroup* gb = r.findGroup(1);
  TEST_ASSERT_NOT_NULL(gb);
  TEST_ASSERT_EQUAL(2, gb->fixtureIds.size());
}

void test_rgb_kind_round_trips() {
  Registry r;
  Fixture* f;
  r.createFixture("strip_1", Kind::RGB, &f);
  const uint16_t id = f->id;

  DynamicJsonDocument doc(4096);
  r.toJson(doc);

  Registry r2;
  TEST_ASSERT_TRUE(r2.fromJson(doc.as<JsonVariantConst>()));
  Fixture* fb = r2.findFixture(id);
  TEST_ASSERT_NOT_NULL(fb);
  TEST_ASSERT_EQUAL(Kind::RGB, fb->kind);
  TEST_ASSERT_EQUAL(0, fb->group);
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_round_trip_preserves_everything);
  RUN_TEST(test_counters_survive_round_trip);
  RUN_TEST(test_counters_clamped_and_recovered);
  RUN_TEST(test_from_json_empty_and_malformed);
  RUN_TEST(test_from_json_clears_previous_state);
  RUN_TEST(test_sub_floor_fixture_skipped_on_load);
  RUN_TEST(test_duplicate_membership_deduped_on_load);
  RUN_TEST(test_rgb_kind_round_trips);
  return UNITY_END();
}

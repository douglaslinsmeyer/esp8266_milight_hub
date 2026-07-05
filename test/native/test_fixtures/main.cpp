#include <unity.h>
#include <LightHubRegistry.h>

using namespace LightHub;

void test_allocator_one_device_id_per_fixture() {
  Registry r;
  Fixture* f1; Fixture* f2;
  TEST_ASSERT_EQUAL(Result::OK, r.createFixture("gazebo_post_1", Kind::RGB_CCT, &f1));
  TEST_ASSERT_EQUAL(Result::OK, r.createFixture("gazebo_post_2", Kind::RGB_CCT, &f2));
  TEST_ASSERT_EQUAL_HEX16(0x2000, f1->deviceId);
  TEST_ASSERT_EQUAL_HEX16(0x2001, f2->deviceId);  // own ID each — the 8 slots are not packed
  TEST_ASSERT_EQUAL(1, f1->id);
  TEST_ASSERT_EQUAL(2, f2->id);
  TEST_ASSERT_EQUAL(1, f1->group);
  TEST_ASSERT_EQUAL(FixtureStatus::UNPAIRED, f1->status);
}

void test_rgb_fixture_uses_group_zero() {
  Registry r;
  Fixture* f;
  TEST_ASSERT_EQUAL(Result::OK, r.createFixture("strip_1", Kind::RGB, &f));
  TEST_ASSERT_EQUAL(0, f->group);
}

void test_create_fixture_validation() {
  Registry r;
  TEST_ASSERT_EQUAL(Result::OK, r.createFixture("post_1", Kind::RGB_CCT));
  TEST_ASSERT_EQUAL(Result::NAME_TAKEN, r.createFixture("post_1", Kind::RGBW));
  TEST_ASSERT_EQUAL(Result::INVALID_NAME, r.createFixture("Post 1", Kind::RGB_CCT));
  TEST_ASSERT_EQUAL(Result::INVALID_KIND, r.createFixture("ok_name", Kind::UNKNOWN));
}

void test_device_ids_never_reused() {
  Registry r;
  Fixture* f1;
  r.createFixture("temp_fixture", Kind::RGB_CCT, &f1);
  const uint16_t f1Id = f1->id;
  TEST_ASSERT_EQUAL(Result::OK, r.deleteFixture(f1Id));
  Fixture* f2;
  r.createFixture("next_fixture", Kind::RGB_CCT, &f2);
  TEST_ASSERT_EQUAL_HEX16(0x2001, f2->deviceId);  // 0x2000 is not handed out again
  TEST_ASSERT_EQUAL(2, f2->id);                   // fixture ids not reused either
}

void test_rename_fixture() {
  Registry r;
  Fixture* f;
  r.createFixture("old_name", Kind::RGB_CCT, &f);
  r.createFixture("taken", Kind::RGB_CCT);
  TEST_ASSERT_EQUAL(Result::NAME_TAKEN, r.renameFixture(f->id, "taken"));
  TEST_ASSERT_EQUAL(Result::INVALID_NAME, r.renameFixture(f->id, "Bad Name"));
  TEST_ASSERT_EQUAL(Result::OK, r.renameFixture(f->id, "new_name"));
  TEST_ASSERT_EQUAL_STRING("new_name", f->name);
  TEST_ASSERT_EQUAL(Result::OK, r.renameFixture(f->id, "new_name"));  // rename to self is a no-op OK
}

void test_not_found_paths() {
  Registry r;
  TEST_ASSERT_EQUAL(Result::NOT_FOUND, r.deleteFixture(99));
  TEST_ASSERT_EQUAL(Result::NOT_FOUND, r.renameFixture(99, "ok_name"));
  TEST_ASSERT_NULL(r.findFixture(99));
}

void test_fixture_name_lookup() {
  Registry r;
  r.createFixture("post_1", Kind::RGB_CCT);
  TEST_ASSERT_TRUE(r.fixtureNameInUse("post_1"));
  TEST_ASSERT_FALSE(r.fixtureNameInUse("post_2"));
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_allocator_one_device_id_per_fixture);
  RUN_TEST(test_rgb_fixture_uses_group_zero);
  RUN_TEST(test_create_fixture_validation);
  RUN_TEST(test_device_ids_never_reused);
  RUN_TEST(test_rename_fixture);
  RUN_TEST(test_not_found_paths);
  RUN_TEST(test_fixture_name_lookup);
  return UNITY_END();
}

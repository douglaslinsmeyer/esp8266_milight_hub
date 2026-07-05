#include <unity.h>
#include <LightHubRegistry.h>

using namespace LightHub;

// helper: registry pre-loaded with three fixtures, returns their ids via out params
static void threeFixtures(Registry& r, uint16_t& a, uint16_t& b, uint16_t& c) {
  Fixture* f;
  r.createFixture("fix_a", Kind::RGB_CCT, &f); a = f->id;
  r.createFixture("fix_b", Kind::RGB_CCT, &f); b = f->id;
  r.createFixture("fix_c", Kind::RGB, &f);     c = f->id;
}

void test_group_lifecycle_and_validation() {
  Registry r;
  DeviceGroup* g;
  TEST_ASSERT_EQUAL(Result::OK, r.createGroup("Patio", &g));
  TEST_ASSERT_EQUAL(1, g->id);
  TEST_ASSERT_EQUAL_STRING("Patio", g->name);
  TEST_ASSERT_EQUAL(0, g->fixtureIds.size());
  TEST_ASSERT_EQUAL(Result::NAME_TAKEN, r.createGroup("Patio"));
  TEST_ASSERT_EQUAL(Result::INVALID_NAME, r.createGroup(""));
  TEST_ASSERT_EQUAL(Result::OK, r.renameGroup(g->id, "Patio East"));
  TEST_ASSERT_EQUAL_STRING("Patio East", g->name);
  TEST_ASSERT_EQUAL(Result::OK, r.deleteGroup(g->id));
  TEST_ASSERT_NULL(r.findGroup(g->id));
  TEST_ASSERT_EQUAL(Result::NOT_FOUND, r.deleteGroup(99));
  TEST_ASSERT_EQUAL(Result::NOT_FOUND, r.renameGroup(99, "x"));
}

void test_membership_add_remove_idempotent() {
  Registry r;
  uint16_t a, b, c;
  threeFixtures(r, a, b, c);
  DeviceGroup* g;
  r.createGroup("Patio", &g);

  TEST_ASSERT_EQUAL(Result::OK, r.addMember(g->id, a));
  TEST_ASSERT_EQUAL(Result::OK, r.addMember(g->id, a));  // idempotent — no duplicate
  TEST_ASSERT_EQUAL(1, g->fixtureIds.size());
  TEST_ASSERT_EQUAL(Result::OK, r.addMember(g->id, b));
  TEST_ASSERT_EQUAL(2, g->fixtureIds.size());
  TEST_ASSERT_TRUE(g->hasMember(a));

  TEST_ASSERT_EQUAL(Result::OK, r.removeMember(g->id, a));
  TEST_ASSERT_EQUAL(Result::OK, r.removeMember(g->id, a));  // idempotent
  TEST_ASSERT_EQUAL(1, g->fixtureIds.size());
  TEST_ASSERT_FALSE(g->hasMember(a));

  TEST_ASSERT_EQUAL(Result::NOT_FOUND, r.addMember(g->id, 99));   // unknown fixture
  TEST_ASSERT_EQUAL(Result::NOT_FOUND, r.addMember(99, a));       // unknown group
  TEST_ASSERT_EQUAL(Result::NOT_FOUND, r.removeMember(99, a));
}

void test_many_to_many() {
  Registry r;
  uint16_t a, b, c;
  threeFixtures(r, a, b, c);
  DeviceGroup* g1; DeviceGroup* g2;
  r.createGroup("Patio", &g1);
  r.createGroup("All outdoor", &g2);
  r.addMember(g1->id, a);
  r.addMember(g2->id, a);  // same fixture in two groups simultaneously
  r.addMember(g2->id, b);
  TEST_ASSERT_EQUAL(2, r.groupCountForFixture(a));
  TEST_ASSERT_EQUAL(1, r.groupCountForFixture(b));
  TEST_ASSERT_EQUAL(0, r.groupCountForFixture(c));
  // deleting a group never touches fixtures
  r.deleteGroup(g1->id);
  TEST_ASSERT_NOT_NULL(r.findFixture(a));
  TEST_ASSERT_EQUAL(1, r.groupCountForFixture(a));
}

void test_set_members_all_or_nothing() {
  Registry r;
  uint16_t a, b, c;
  threeFixtures(r, a, b, c);
  DeviceGroup* g;
  r.createGroup("Patio", &g);
  r.addMember(g->id, c);

  std::vector<uint16_t> withUnknown = {a, 99, b};
  TEST_ASSERT_EQUAL(Result::NOT_FOUND, r.setMembers(g->id, withUnknown));
  TEST_ASSERT_EQUAL(1, g->fixtureIds.size());  // unchanged on failure
  TEST_ASSERT_TRUE(g->hasMember(c));

  std::vector<uint16_t> withDupes = {a, b, a};
  TEST_ASSERT_EQUAL(Result::OK, r.setMembers(g->id, withDupes));
  TEST_ASSERT_EQUAL(2, g->fixtureIds.size());  // deduped, replaces previous membership
  TEST_ASSERT_TRUE(g->hasMember(a));
  TEST_ASSERT_TRUE(g->hasMember(b));
  TEST_ASSERT_FALSE(g->hasMember(c));
}

void test_delete_fixture_strips_memberships() {
  Registry r;
  uint16_t a, b, c;
  threeFixtures(r, a, b, c);
  DeviceGroup* g1; DeviceGroup* g2;
  r.createGroup("Patio", &g1);
  r.createGroup("All outdoor", &g2);
  r.addMember(g1->id, a);
  r.addMember(g2->id, a);
  r.addMember(g2->id, b);

  TEST_ASSERT_EQUAL(Result::OK, r.deleteFixture(a));
  TEST_ASSERT_FALSE(g1->hasMember(a));
  TEST_ASSERT_FALSE(g2->hasMember(a));
  TEST_ASSERT_TRUE(g2->hasMember(b));
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_group_lifecycle_and_validation);
  RUN_TEST(test_membership_add_remove_idempotent);
  RUN_TEST(test_many_to_many);
  RUN_TEST(test_set_members_all_or_nothing);
  RUN_TEST(test_delete_fixture_strips_memberships);
  return UNITY_END();
}

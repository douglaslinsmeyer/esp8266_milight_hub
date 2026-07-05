#include <unity.h>
#include <LightHubModel.h>

using namespace LightHub;

void test_kind_round_trip() {
  TEST_ASSERT_EQUAL(Kind::RGB_CCT, kindFromString("rgb_cct"));
  TEST_ASSERT_EQUAL(Kind::DUAL_WHITE, kindFromString("dual_white"));
  TEST_ASSERT_EQUAL(Kind::RGBW, kindFromString("rgbw"));
  TEST_ASSERT_EQUAL(Kind::RGB, kindFromString("rgb"));
  TEST_ASSERT_EQUAL(Kind::UNKNOWN, kindFromString("nope"));
  TEST_ASSERT_EQUAL(Kind::UNKNOWN, kindFromString(nullptr));
  TEST_ASSERT_EQUAL_STRING("rgb_cct", kindToString(Kind::RGB_CCT));
  TEST_ASSERT_EQUAL_STRING("dual_white", kindToString(Kind::DUAL_WHITE));
}

void test_kind_to_protocol() {
  TEST_ASSERT_EQUAL_STRING("fut089", kindToProtocol(Kind::RGB_CCT));
  TEST_ASSERT_EQUAL_STRING("fut091", kindToProtocol(Kind::DUAL_WHITE));
  TEST_ASSERT_EQUAL_STRING("rgbw", kindToProtocol(Kind::RGBW));
  TEST_ASSERT_EQUAL_STRING("rgb", kindToProtocol(Kind::RGB));
}

void test_fixture_group_per_kind() {
  TEST_ASSERT_EQUAL(1, fixtureGroup(Kind::RGB_CCT));
  TEST_ASSERT_EQUAL(1, fixtureGroup(Kind::DUAL_WHITE));
  TEST_ASSERT_EQUAL(1, fixtureGroup(Kind::RGBW));
  TEST_ASSERT_EQUAL(0, fixtureGroup(Kind::RGB));  // rgb protocol has no group addressing
}

void test_slug_validation() {
  TEST_ASSERT_TRUE(isValidSlug("gazebo_post_1"));
  TEST_ASSERT_TRUE(isValidSlug("x1"));
  TEST_ASSERT_FALSE(isValidSlug(""));
  TEST_ASSERT_FALSE(isValidSlug("Gazebo"));       // uppercase
  TEST_ASSERT_FALSE(isValidSlug("gazebo 1"));     // space
  TEST_ASSERT_FALSE(isValidSlug("gaz-ebo"));      // hyphen
  TEST_ASSERT_FALSE(isValidSlug(nullptr));
  char too_long[MAX_NAME_LEN + 2];
  for (size_t i = 0; i < MAX_NAME_LEN + 1; i++) too_long[i] = 'a';
  too_long[MAX_NAME_LEN + 1] = '\0';
  TEST_ASSERT_FALSE(isValidSlug(too_long));       // 33 chars
}

void test_group_name_validation() {
  TEST_ASSERT_TRUE(isValidGroupName("Patio"));
  TEST_ASSERT_TRUE(isValidGroupName("All outdoor (2026)"));  // free text, spaces + punctuation OK
  TEST_ASSERT_FALSE(isValidGroupName(""));
  TEST_ASSERT_FALSE(isValidGroupName(nullptr));
  TEST_ASSERT_FALSE(isValidGroupName("has\ttab"));           // control chars rejected
  char too_long[MAX_GROUP_NAME_LEN + 2];
  for (size_t i = 0; i < MAX_GROUP_NAME_LEN + 1; i++) too_long[i] = 'a';
  too_long[MAX_GROUP_NAME_LEN + 1] = '\0';
  TEST_ASSERT_FALSE(isValidGroupName(too_long));             // 49 chars
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_kind_round_trip);
  RUN_TEST(test_kind_to_protocol);
  RUN_TEST(test_fixture_group_per_kind);
  RUN_TEST(test_slug_validation);
  RUN_TEST(test_group_name_validation);
  return UNITY_END();
}

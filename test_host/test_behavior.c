#include "unity.h"

#include "b2_core.h"
#include "b2_settings.h"

#include <string.h>

void setUp(void) {}
void tearDown(void) {}

static b2_core_rule_t valid_relay_rule(void)
{
    return (b2_core_rule_t){
        .enabled = true,
        .condition = B2_RULE_INPUT_ACTIVE,
        .source = 0,
        .action = B2_RULE_ACTION_RELAY_SET,
        .target = 1,
        .action_state = true,
        .threshold = 0.0f,
        .duration_ms = 1000,
        .sms_number = "",
    };
}

static void test_rule_validation_accepts_valid_rule(void)
{
    b2_core_rule_t rule = valid_relay_rule();
    TEST_ASSERT_TRUE(b2_core_rule_is_valid(&rule, 2, 2, 4));
}

static void test_rule_validation_rejects_unsafe_bounds(void)
{
    b2_core_rule_t rule = valid_relay_rule();
    rule.target = 2;
    TEST_ASSERT_FALSE(b2_core_rule_is_valid(&rule, 2, 2, 4));
    rule = valid_relay_rule();
    rule.source = 2;
    TEST_ASSERT_FALSE(b2_core_rule_is_valid(&rule, 2, 2, 4));
    rule = valid_relay_rule();
    rule.duration_ms = 86400001U;
    TEST_ASSERT_FALSE(b2_core_rule_is_valid(&rule, 2, 2, 4));
}

static void test_sms_token_parser_is_fail_closed(void)
{
    char command[32] = {0};
    TEST_ASSERT_EQUAL_INT(0, b2_core_extract_sms_command("secret", "TOKEN:secret relay 1", command, sizeof(command)));
    TEST_ASSERT_EQUAL_STRING("relay 1", command);
    TEST_ASSERT_EQUAL_INT(-2, b2_core_extract_sms_command("secret", "TOKEN:wrong relay 1", command, sizeof(command)));
    TEST_ASSERT_EQUAL_INT(-2, b2_core_extract_sms_command("secret", "relay 1", command, sizeof(command)));
    TEST_ASSERT_EQUAL_INT(-3, b2_core_extract_sms_command("secret", "TOKEN:secret relay 1", command, 5));
}

static void test_sms_token_compare_handles_lengths(void)
{
    TEST_ASSERT_TRUE(b2_core_token_equal("abc", "abc"));
    TEST_ASSERT_FALSE(b2_core_token_equal("abc", "ab"));
    TEST_ASSERT_FALSE(b2_core_token_equal("ab", "abc"));
    TEST_ASSERT_FALSE(b2_core_token_equal(NULL, "abc"));
}

static void test_event_ring_rolls_over_and_reads_newest(void)
{
    b2_core_event_ring_t ring;
    b2_core_event_ring_reset(&ring);
    for (uint32_t i = 0; i < B2_EVENT_LOG_CAPACITY + 2U; ++i) {
        uint32_t sequence = 0;
        TEST_ASSERT_EQUAL_INT(ESP_OK, b2_core_event_ring_append(&ring, (int64_t)i, B2_EVENT_INPUT,
                                                                0, (int32_t)i, "input", &sequence));
        TEST_ASSERT_EQUAL_UINT32(i + 1U, sequence);
    }
    TEST_ASSERT_EQUAL_UINT32(B2_EVENT_LOG_CAPACITY, ring.count);
    b2_event_t event = {0};
    TEST_ASSERT_EQUAL_INT(ESP_OK, b2_core_event_ring_get_newest(&ring, 0, &event));
    TEST_ASSERT_EQUAL_UINT32(B2_EVENT_LOG_CAPACITY + 2U, event.sequence);
    TEST_ASSERT_EQUAL_INT32(B2_EVENT_LOG_CAPACITY + 1U, event.value);
    TEST_ASSERT_EQUAL_INT(ESP_OK, b2_core_event_ring_get_newest(&ring, B2_EVENT_LOG_CAPACITY - 1U, &event));
    TEST_ASSERT_EQUAL_UINT32(3U, event.sequence);
    TEST_ASSERT_EQUAL_INT(ESP_ERR_NOT_FOUND, b2_core_event_ring_get_newest(&ring, B2_EVENT_LOG_CAPACITY, &event));
}

static void test_settings_migration_accepts_only_known_versions(void)
{
    TEST_ASSERT_TRUE(b2_core_settings_version_supported(1, 4));
    TEST_ASSERT_TRUE(b2_core_settings_version_supported(3, 4));
    TEST_ASSERT_TRUE(b2_core_settings_version_supported(4, 4));
    TEST_ASSERT_FALSE(b2_core_settings_version_supported(0, 4));
    TEST_ASSERT_FALSE(b2_core_settings_version_supported(5, 4));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_rule_validation_accepts_valid_rule);
    RUN_TEST(test_rule_validation_rejects_unsafe_bounds);
    RUN_TEST(test_sms_token_parser_is_fail_closed);
    RUN_TEST(test_sms_token_compare_handles_lengths);
    RUN_TEST(test_event_ring_rolls_over_and_reads_newest);
    RUN_TEST(test_settings_migration_accepts_only_known_versions);
    return UNITY_END();
}

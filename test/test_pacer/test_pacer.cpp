#include <unity.h>
#include "usb_audio_host/pacer.h"

using namespace usb_audio_host;

void setUp() {}
void tearDown() {}

static uint32_t sum(Pacer& p, uint32_t packets)
{
    uint32_t total = 0;
    for (uint32_t i = 0; i < packets; i++) total += p.next();
    return total;
}

static void test_nominal_rate_averages_exactly()
{
    Pacer p;
    p.begin(44100, 8000); // high speed, every microframe: 5.5125 frames per packet
    TEST_ASSERT_EQUAL_UINT32((44100u << 16) / 8000u, p.nominalQ16());
    TEST_ASSERT_EQUAL_UINT32(44100, sum(p, 8000));
    TEST_ASSERT_EQUAL_UINT32(44100 * 10, sum(p, 80000));
    for (int i = 0; i < 100; i++) {
        const uint16_t n = p.next();
        TEST_ASSERT_TRUE(n == 5 || n == 6);
    }
}

static void test_full_speed_frames()
{
    Pacer p;
    p.begin(48000, 1000);
    for (int i = 0; i < 100; i++) TEST_ASSERT_EQUAL(48, p.next());
    p.begin(44100, 1000);
    TEST_ASSERT_EQUAL_UINT32(44100, sum(p, 1000));
}

static void test_high_speed_feedback_16_16()
{
    Pacer p;
    p.begin(48000, 8000); // 6.0 frames per microframe
    // The device runs 0.1% fast: 6.006 frames per microframe in 16.16.
    const uint32_t v = static_cast<uint32_t>(6.006 * 65536.0);
    const uint8_t payload[4] = {static_cast<uint8_t>(v), static_cast<uint8_t>(v >> 8),
                                static_cast<uint8_t>(v >> 16), static_cast<uint8_t>(v >> 24)};
    p.feedback(payload, 4);
    TEST_ASSERT_EQUAL_UINT32(v, p.rateQ16());
    TEST_ASSERT_EQUAL_UINT32(0, p.feedbackIgnored());
    // Over 8000 packets that is 48 frames more than nominal.
    const uint32_t total = sum(p, 8000);
    TEST_ASSERT_UINT32_WITHIN(1, 48048, total);
}

static void test_full_speed_feedback_10_14()
{
    Pacer p;
    p.begin(48000, 1000); // 48 frames per frame
    // 47.95 frames per frame in 10.14, three bytes.
    const uint32_t v = static_cast<uint32_t>(47.95 * 16384.0);
    const uint8_t payload[3] = {static_cast<uint8_t>(v), static_cast<uint8_t>(v >> 8), static_cast<uint8_t>(v >> 16)};
    p.feedback(payload, 3);
    TEST_ASSERT_EQUAL_UINT32(v << 2, p.rateQ16());
    TEST_ASSERT_UINT32_WITHIN(1, 47950, sum(p, 1000));
}

static void test_feedback_in_the_wrong_format_is_rescaled()
{
    // A high-speed device reporting frames per frame in 10.14 (a common
    // mistake): 48 << 14 arrives where 6 << 16 was expected. The value is
    // shifted down until it fits and then applied.
    Pacer p;
    p.begin(48000, 8000);
    const uint32_t v = 48u << 14;
    const uint8_t payload[4] = {static_cast<uint8_t>(v), static_cast<uint8_t>(v >> 8),
                                static_cast<uint8_t>(v >> 16), static_cast<uint8_t>(v >> 24)};
    p.feedback(payload, 4);
    TEST_ASSERT_EQUAL_UINT32(6u << 16, p.rateQ16());
    TEST_ASSERT_EQUAL_UINT32(0, p.feedbackIgnored());
}

static void test_feedback_nonsense_is_ignored()
{
    Pacer p;
    p.begin(48000, 8000);
    const uint8_t zero[4] = {0, 0, 0, 0};
    p.feedback(zero, 4);
    TEST_ASSERT_EQUAL_UINT32(1, p.feedbackIgnored());
    // Settle the format with a sane value, then send one 2% off: dropped.
    const uint32_t good = 6u << 16;
    const uint8_t g[4] = {static_cast<uint8_t>(good), static_cast<uint8_t>(good >> 8), static_cast<uint8_t>(good >> 16), 0};
    p.feedback(g, 4);
    const uint32_t bad = static_cast<uint32_t>(6.12 * 65536.0);
    const uint8_t b[4] = {static_cast<uint8_t>(bad), static_cast<uint8_t>(bad >> 8), static_cast<uint8_t>(bad >> 16), 0};
    p.feedback(b, 4);
    TEST_ASSERT_EQUAL_UINT32(2, p.feedbackIgnored());
    TEST_ASSERT_EQUAL_UINT32(good, p.rateQ16());
    p.feedback(g, 2); // too short to mean anything
    TEST_ASSERT_EQUAL_UINT32(good, p.rateQ16());
}

static void test_mirror_takes_precedence_then_falls_back()
{
    Pacer p;
    p.begin(48000, 8000);
    p.mirror(5);
    p.mirror(7);
    TEST_ASSERT_EQUAL(5, p.next());
    TEST_ASSERT_EQUAL(7, p.next());
    TEST_ASSERT_EQUAL(6, p.next()); // nominal again
    // An overfilled mirror queue keeps the newest values.
    for (uint16_t i = 0; i < Pacer::MIRROR_DEPTH + 5; i++) p.mirror(i);
    uint16_t first = p.next();
    TEST_ASSERT_TRUE(first >= 6);
}

int main()
{
    UNITY_BEGIN();
    RUN_TEST(test_nominal_rate_averages_exactly);
    RUN_TEST(test_full_speed_frames);
    RUN_TEST(test_high_speed_feedback_16_16);
    RUN_TEST(test_full_speed_feedback_10_14);
    RUN_TEST(test_feedback_in_the_wrong_format_is_rescaled);
    RUN_TEST(test_feedback_nonsense_is_ignored);
    RUN_TEST(test_mirror_takes_precedence_then_falls_back);
    return UNITY_END();
}

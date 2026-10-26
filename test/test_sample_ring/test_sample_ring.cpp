#include <unity.h>
#include "usb_audio_host/sample_ring.h"

using namespace usb_audio_host;

static SampleRing<8, 4> ring;

void setUp() { ring.begin(2); }
void tearDown() {}

static void test_write_then_read_interleaved()
{
    const int32_t in[6] = {1, 2, 3, 4, 5, 6};
    TEST_ASSERT_EQUAL_UINT32(3, ring.write(in, 3));
    TEST_ASSERT_EQUAL_UINT32(3, ring.available());
    TEST_ASSERT_EQUAL_UINT32(4, ring.space());
    int32_t out[6] = {};
    TEST_ASSERT_EQUAL_UINT32(3, ring.read(out, 3));
    TEST_ASSERT_EQUAL_INT32_ARRAY(in, out, 6);
    TEST_ASSERT_EQUAL_UINT32(0, ring.available());
}

static void test_overrun_keeps_what_fits_and_counts_the_rest()
{
    int32_t in[20];
    for (int i = 0; i < 20; i++) in[i] = i;
    TEST_ASSERT_EQUAL_UINT32(7, ring.write(in, 10));
    TEST_ASSERT_EQUAL_UINT32(3, ring.overruns());
    TEST_ASSERT_EQUAL_UINT32(0, ring.space());
    TEST_ASSERT_EQUAL_UINT32(0, ring.write(in, 1));
    TEST_ASSERT_EQUAL_UINT32(4, ring.overruns());
}

static void test_underrun_zero_fills()
{
    const int32_t in[2] = {9, 8};
    ring.write(in, 1);
    int32_t out[6] = {1, 1, 1, 1, 1, 1};
    TEST_ASSERT_EQUAL_UINT32(1, ring.read(out, 3));
    TEST_ASSERT_EQUAL_INT32(9, out[0]);
    TEST_ASSERT_EQUAL_INT32(8, out[1]);
    for (int i = 2; i < 6; i++) TEST_ASSERT_EQUAL_INT32(0, out[i]);
    TEST_ASSERT_EQUAL_UINT32(2, ring.underruns());
}

static void test_wraps_around()
{
    int32_t in[14];
    for (int i = 0; i < 14; i++) in[i] = 100 + i;
    int32_t out[14];
    for (int round = 0; round < 5; round++) {
        TEST_ASSERT_EQUAL_UINT32(5, ring.write(in, 5));
        TEST_ASSERT_EQUAL_UINT32(5, ring.read(out, 5));
        TEST_ASSERT_EQUAL_INT32_ARRAY(in, out, 10);
    }
    TEST_ASSERT_EQUAL_UINT32(0, ring.overruns());
    TEST_ASSERT_EQUAL_UINT32(0, ring.underruns());
}

static void test_frame_at_a_time_and_discard()
{
    for (int32_t f = 0; f < 4; f++) {
        TEST_ASSERT_TRUE(ring.beginFrame());
        ring.sample(0, f * 10);
        ring.sample(1, f * 10 + 1);
        ring.endFrame();
    }
    TEST_ASSERT_EQUAL_UINT32(4, ring.available());
    TEST_ASSERT_EQUAL_UINT32(2, ring.discard(2));
    int32_t out[4];
    ring.read(out, 2);
    TEST_ASSERT_EQUAL_INT32(20, out[0]);
    TEST_ASSERT_EQUAL_INT32(31, out[3]);
}

static void test_channel_count_is_clamped()
{
    ring.begin(0);
    TEST_ASSERT_EQUAL(1, ring.channels());
    ring.begin(9);
    TEST_ASSERT_EQUAL(4, ring.channels());
}

int main()
{
    UNITY_BEGIN();
    RUN_TEST(test_write_then_read_interleaved);
    RUN_TEST(test_overrun_keeps_what_fits_and_counts_the_rest);
    RUN_TEST(test_underrun_zero_fills);
    RUN_TEST(test_wraps_around);
    RUN_TEST(test_frame_at_a_time_and_discard);
    RUN_TEST(test_channel_count_is_clamped);
    return UNITY_END();
}

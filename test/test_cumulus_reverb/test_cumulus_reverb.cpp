// The CumulusReverb example's plug-in: Clouds' reverb from the unmodified
// Mutable Instruments sources, run on the host machine through the
// audioeffectx.h stand-in. The example's cumulus_reverb.cpp is built into
// this environment for that (platformio.ini).
#include <unity.h>
#include <math.h>
#include "audioeffectx.h"
#include "vst_host/effect.h"
#include "../../examples/CumulusReverb/cumulus_reverb.h"

void setUp() {}
void tearDown() {}

static const int BLOCK = 128;

// 32 KB of delay memory: off the stack.
static CumulusReverb* reverb;

static void fresh()
{
    delete reverb;
    reverb = new CumulusReverb(nullptr);
}

// Runs n blocks of the given input through the plug-in, the input after
// the first block silent unless hold is set, and returns the energy of each
// output channel over the blocks from `from` on.
struct Energy {
    float l;
    float r;
};

static Energy run(const float* l, const float* r, int blocks, int from, bool hold = false)
{
    static float il[BLOCK], ir[BLOCK], ol[BLOCK], orr[BLOCK];
    float* in[2] = {il, ir};
    float* out[2] = {ol, orr};
    Energy e = {0.0f, 0.0f};
    for (int b = 0; b < blocks; b++) {
        for (int i = 0; i < BLOCK; i++) {
            il[i] = (b == 0 || hold) ? l[i] : 0.0f;
            ir[i] = (b == 0 || hold) ? r[i] : 0.0f;
        }
        reverb->processReplacing(in, out, BLOCK);
        if (b < from) continue;
        for (int i = 0; i < BLOCK; i++) {
            TEST_ASSERT_TRUE(isfinite(ol[i]) && isfinite(orr[i]));
            e.l += ol[i] * ol[i];
            e.r += orr[i] * orr[i];
        }
    }
    return e;
}

static float impulse_[BLOCK] = {0.5f};
static float silence_[BLOCK] = {0.0f};

// Blocks of 128 at 44.1 kHz: 345 to the second.
static const int SECOND = 345;

static void test_identifies_and_declares_its_parameters()
{
    fresh();
    char text[kVstMaxProductStrLen + 1];
    TEST_ASSERT_TRUE(reverb->getEffectName(text));
    TEST_ASSERT_EQUAL_STRING("Cumulus", text);
    TEST_ASSERT_EQUAL_INT32(2, reverb->getNumInputs());
    TEST_ASSERT_EQUAL_INT32(2, reverb->getNumOutputs());
    TEST_ASSERT_EQUAL_INT32(CumulusReverb::NUM_PARAMS, reverb->getNumParams());
    TEST_ASSERT_EQUAL(kPlugCategRoomFx, reverb->getPlugCategory());
    const char* const names[] = {"Amount", "Time", "Diffuse", "Tone", "Input"};
    for (int i = 0; i < CumulusReverb::NUM_PARAMS; i++) {
        reverb->getParameterName(i, text);
        TEST_ASSERT_EQUAL_STRING(names[i], text);
        TEST_ASSERT_TRUE(strlen(text) <= kVstMaxParamStrLen);
    }
    reverb->setParameter(CumulusReverb::PARAM_AMOUNT, 0.25f);
    reverb->getParameterDisplay(CumulusReverb::PARAM_AMOUNT, text);
    TEST_ASSERT_EQUAL_STRING("25", text);
    reverb->getParameterLabel(CumulusReverb::PARAM_AMOUNT, text);
    TEST_ASSERT_EQUAL_STRING("%", text);
    reverb->setParameter(CumulusReverb::PARAM_TIME, 7.0f);
    TEST_ASSERT_EQUAL_FLOAT(1.0f, reverb->getParameter(CumulusReverb::PARAM_TIME));
    reverb->setParameter(CumulusReverb::PARAM_TIME, -1.0f);
    TEST_ASSERT_EQUAL_FLOAT(0.0f, reverb->getParameter(CumulusReverb::PARAM_TIME));
    reverb->setParameter(99, 0.5f);  // ignored
    TEST_ASSERT_EQUAL_FLOAT(0.0f, reverb->getParameter(99));
}

static void test_no_amount_is_the_dry_signal_exactly()
{
    fresh();
    reverb->setParameter(CumulusReverb::PARAM_AMOUNT, 0.0f);
    float l[BLOCK], r[BLOCK], ol[BLOCK], orr[BLOCK];
    float* in[2] = {l, r};
    float* out[2] = {ol, orr};
    for (int i = 0; i < BLOCK; i++) {
        l[i] = 0.5f * sinf(0.1f * static_cast<float>(i));
        r[i] = -0.25f * l[i];
    }
    for (int b = 0; b < 4; b++) {
        reverb->processReplacing(in, out, BLOCK);
        TEST_ASSERT_EQUAL_FLOAT_ARRAY(l, ol, BLOCK);
        TEST_ASSERT_EQUAL_FLOAT_ARRAY(r, orr, BLOCK);
    }
}

static void test_an_impulse_rings_and_dies_away()
{
    fresh();
    reverb->setParameter(CumulusReverb::PARAM_AMOUNT, 1.0f);
    reverb->setParameter(CumulusReverb::PARAM_TIME, 0.5f);
    const Energy early = run(impulse_, silence_, SECOND / 4, 8);
    TEST_ASSERT_TRUE(early.l > 1e-4f);
    TEST_ASSERT_TRUE(early.r > 1e-4f);  // the left input reaches both sides
    const Energy late = run(silence_, silence_, SECOND, SECOND - 20);
    TEST_ASSERT_TRUE(late.l < early.l * 1e-3f);
    TEST_ASSERT_TRUE(late.r < early.r * 1e-3f);
}

static void test_a_longer_time_leaves_more_tail()
{
    float tail[2];
    for (int t = 0; t < 2; t++) {
        fresh();
        reverb->setParameter(CumulusReverb::PARAM_AMOUNT, 1.0f);
        reverb->setParameter(CumulusReverb::PARAM_TIME, t ? 1.0f : 0.0f);
        const Energy e = run(impulse_, silence_, SECOND, SECOND / 2);
        tail[t] = e.l + e.r;
    }
    TEST_ASSERT_TRUE(tail[1] > tail[0] * 100.0f);
}

static void test_the_two_sides_are_different_rooms()
{
    fresh();
    reverb->setParameter(CumulusReverb::PARAM_AMOUNT, 1.0f);
    static float il[BLOCK], ir[BLOCK], ol[BLOCK], orr[BLOCK];
    float* in[2] = {il, ir};
    float* out[2] = {ol, orr};
    il[0] = 0.5f;
    float difference = 0.0f;
    for (int b = 0; b < SECOND / 2; b++) {
        reverb->processReplacing(in, out, BLOCK);
        il[0] = 0.0f;
        for (int i = 0; i < BLOCK; i++) difference += fabsf(ol[i] - orr[i]);
    }
    TEST_ASSERT_TRUE(difference > 0.1f);
}

static void test_the_longest_brightest_setting_stays_bounded()
{
    fresh();
    for (int p = 0; p < CumulusReverb::NUM_PARAMS; p++) reverb->setParameter(p, 1.0f);
    float l[BLOCK], r[BLOCK];
    uint32_t noise = 12345;
    for (int i = 0; i < BLOCK; i++) {
        noise = noise * 1664525u + 1013904223u;
        l[i] = static_cast<float>(static_cast<int32_t>(noise)) / 2147483648.0f;
        r[i] = -l[i];
    }
    static float il[BLOCK], ir[BLOCK], ol[BLOCK], orr[BLOCK];
    float* in[2] = {il, ir};
    float* out[2] = {ol, orr};
    float peak = 0.0f;
    for (int b = 0; b < 5 * SECOND; b++) {
        for (int i = 0; i < BLOCK; i++) {
            il[i] = l[(i + b) % BLOCK];
            ir[i] = r[(i * 3 + b) % BLOCK];
        }
        reverb->processReplacing(in, out, BLOCK);
        for (int i = 0; i < BLOCK; i++) {
            TEST_ASSERT_TRUE(isfinite(ol[i]) && isfinite(orr[i]));
            if (fabsf(ol[i]) > peak) peak = fabsf(ol[i]);
            if (fabsf(orr[i]) > peak) peak = fabsf(orr[i]);
        }
    }
    // The 12-bit tank saturates at 8; the output is two taps of it.
    TEST_ASSERT_TRUE(peak < 32.0f);
}

static void test_clear_stops_the_tail_at_the_next_block()
{
    fresh();
    reverb->setParameter(CumulusReverb::PARAM_AMOUNT, 1.0f);
    reverb->setParameter(CumulusReverb::PARAM_TIME, 1.0f);
    const Energy ringing = run(impulse_, silence_, SECOND / 4, SECOND / 4 - 4);
    TEST_ASSERT_TRUE(ringing.l > 1e-6f);
    reverb->clear();
    const Energy after = run(silence_, silence_, 4, 0);
    TEST_ASSERT_EQUAL_FLOAT(0.0f, after.l);
    TEST_ASSERT_EQUAL_FLOAT(0.0f, after.r);
}

static void test_runs_any_block_length()
{
    // A block that is not a multiple of the 32-frame runs, then the same
    // signal in one: the same output.
    static float il[100], ir[100], a[2][100], b[2][100];
    for (int i = 0; i < 100; i++) {
        il[i] = 0.3f * sinf(0.05f * static_cast<float>(i));
        ir[i] = 0.0f;
    }
    float* in[2] = {il, ir};
    fresh();
    reverb->setParameter(CumulusReverb::PARAM_AMOUNT, 1.0f);
    float* outA[2] = {a[0], a[1]};
    reverb->processReplacing(in, outA, 100);
    fresh();
    reverb->setParameter(CumulusReverb::PARAM_AMOUNT, 1.0f);
    float* in2[2] = {il + 37, ir + 37};
    float* outB1[2] = {b[0], b[1]};
    float* outB2[2] = {b[0] + 37, b[1] + 37};
    reverb->processReplacing(in, outB1, 37);
    reverb->processReplacing(in2, outB2, 63);
    TEST_ASSERT_EQUAL_FLOAT_ARRAY(a[0], b[0], 100);
    TEST_ASSERT_EQUAL_FLOAT_ARRAY(a[1], b[1], 100);
}

static void test_the_host_drives_it()
{
    fresh();
    VSTEffect effect(*reverb);
    TEST_ASSERT_TRUE(effect.supported());
    effect.setParameter(CumulusReverb::PARAM_AMOUNT, 1.0f);
    TEST_ASSERT_EQUAL_FLOAT(1.0f, reverb->getParameter(CumulusReverb::PARAM_AMOUNT));
    char text[VSTEffect::TEXT_LEN];
    effect.effectName(text);
    TEST_ASSERT_EQUAL_STRING("Cumulus", text);
}

int main()
{
    UNITY_BEGIN();
    RUN_TEST(test_identifies_and_declares_its_parameters);
    RUN_TEST(test_no_amount_is_the_dry_signal_exactly);
    RUN_TEST(test_an_impulse_rings_and_dies_away);
    RUN_TEST(test_a_longer_time_leaves_more_tail);
    RUN_TEST(test_the_two_sides_are_different_rooms);
    RUN_TEST(test_the_longest_brightest_setting_stays_bounded);
    RUN_TEST(test_clear_stops_the_tail_at_the_next_block);
    RUN_TEST(test_runs_any_block_length);
    RUN_TEST(test_the_host_drives_it);
    delete reverb;
    return UNITY_END();
}

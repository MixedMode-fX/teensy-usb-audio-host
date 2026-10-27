// The FerricTape example's plug-in, run on the host machine through the
// audioeffectx.h stand-in. The example's ferric_tape.cpp is built into this
// environment for that (platformio.ini).
#include <unity.h>
#include <math.h>
#include "audioeffectx.h"
#include "vst_host/effect.h"
#include "../../examples/FerricTape/ferric_tape.h"

void setUp() {}
void tearDown() {}

static const int BLOCK = 128;
static const float RATE = 44100.0f;
static const int SECOND = 345;  // blocks of 128 at 44.1 kHz

// 80 KB of delay memory: off the stack.
static FerricTape* tape;

// A fresh plug-in with every texture off: no drive to speak of, no
// compression, 16 bits, open, a steady transport, no chorus, no slap.
static void clean()
{
    delete tape;
    tape = new FerricTape(nullptr);
    const float settings[FerricTape::NUM_PARAMS] = {0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 1};
    for (int i = 0; i < FerricTape::NUM_PARAMS; i++) tape->setParameter(i, settings[i]);
}

static float il[BLOCK], ir[BLOCK], ol[BLOCK], orr[BLOCK];
static float* in[2] = {il, ir};
static float* out[2] = {ol, orr};

static float sineAt(long n, float hz, float amplitude)
{
    return amplitude * sinf(2.0f * 3.14159265f * hz * static_cast<float>(n) / RATE);
}

// Runs a block of a sine on both sides, starting at sample n.
static void sineBlock(long n, float hz, float amplitude)
{
    for (int i = 0; i < BLOCK; i++) il[i] = ir[i] = sineAt(n + i, hz, amplitude);
    tape->processReplacing(in, out, BLOCK);
}

static void silentBlock()
{
    for (int i = 0; i < BLOCK; i++) il[i] = ir[i] = 0.0f;
    tape->processReplacing(in, out, BLOCK);
}

static float energy(const float* x)
{
    float e = 0.0f;
    for (int i = 0; i < BLOCK; i++) e += x[i] * x[i];
    return e;
}

static void test_identifies_and_declares_its_parameters()
{
    clean();
    char text[kVstMaxProductStrLen + 1];
    TEST_ASSERT_TRUE(tape->getEffectName(text));
    TEST_ASSERT_EQUAL_STRING("Ferric", text);
    TEST_ASSERT_EQUAL_INT32(2, tape->getNumInputs());
    TEST_ASSERT_EQUAL_INT32(2, tape->getNumOutputs());
    TEST_ASSERT_EQUAL_INT32(FerricTape::NUM_PARAMS, tape->getNumParams());
    const char* const names[] = {"Drive",   "Comp",   "Bits", "Tone",     "Wow",  "Flutter",
                                 "Dropout", "Chorus", "Time", "Feedback", "Slap", "Mix"};
    for (int i = 0; i < FerricTape::NUM_PARAMS; i++) {
        tape->getParameterName(i, text);
        TEST_ASSERT_EQUAL_STRING(names[i], text);
        TEST_ASSERT_TRUE(strlen(text) <= kVstMaxParamStrLen);
        tape->getParameterDisplay(i, text);
        TEST_ASSERT_TRUE(strlen(text) > 0 && strlen(text) <= kVstMaxParamStrLen);
        tape->getParameterLabel(i, text);
        TEST_ASSERT_TRUE(strlen(text) <= kVstMaxParamStrLen);
    }
    tape->setParameter(FerricTape::PARAM_DRIVE, 0.5f);
    tape->getParameterDisplay(FerricTape::PARAM_DRIVE, text);
    TEST_ASSERT_EQUAL_STRING("12", text);
    tape->setParameter(FerricTape::PARAM_BITS, 1.0f);
    tape->getParameterDisplay(FerricTape::PARAM_BITS, text);
    TEST_ASSERT_EQUAL_STRING("4.00", text);
    tape->setParameter(FerricTape::PARAM_TIME, 0.0f);
    tape->getParameterDisplay(FerricTape::PARAM_TIME, text);
    TEST_ASSERT_EQUAL_STRING("40", text);
    tape->setParameter(FerricTape::PARAM_TIME, 1.0f);
    tape->getParameterDisplay(FerricTape::PARAM_TIME, text);
    TEST_ASSERT_EQUAL_STRING("350", text);
    tape->setParameter(FerricTape::PARAM_WOW, 7.0f);
    TEST_ASSERT_EQUAL_FLOAT(1.0f, tape->getParameter(FerricTape::PARAM_WOW));
    tape->setParameter(99, 0.5f);  // ignored
    TEST_ASSERT_EQUAL_FLOAT(0.0f, tape->getParameter(99));
}

static void test_no_mix_is_the_dry_signal_exactly()
{
    clean();
    for (int i = 0; i < FerricTape::NUM_PARAMS; i++) tape->setParameter(i, 0.7f);
    tape->setParameter(FerricTape::PARAM_MIX, 0.0f);
    for (int b = 0; b < 8; b++) {
        sineBlock(b * BLOCK, 440.0f, 0.5f);
        TEST_ASSERT_EQUAL_FLOAT_ARRAY(il, ol, BLOCK);
        TEST_ASSERT_EQUAL_FLOAT_ARRAY(ir, orr, BLOCK);
    }
}

static void test_clean_settings_pass_a_tone_at_about_its_level()
{
    clean();
    float e_in = 0.0f, e_out = 0.0f;
    for (int b = 0; b < SECOND / 2; b++) {
        sineBlock(b * BLOCK, 440.0f, 0.1f);
        if (b < 10) continue;
        e_in += energy(il);
        e_out += energy(ol);
    }
    // arctan's small-signal slope, normalised, is 1 / atan(1): +2 dB.
    const float db = 10.0f * log10f(e_out / e_in);
    TEST_ASSERT_FLOAT_WITHIN(1.0f, 2.1f, db);
}

// Zero crossings of the left output while a steady 1 kHz plays, and the
// spread of the intervals between them, in samples.
static float crossingSpread(float wow, float flutter)
{
    clean();
    tape->setParameter(FerricTape::PARAM_WOW, wow);
    tape->setParameter(FerricTape::PARAM_FLUTTER, flutter);
    float last = 0.0f, previous_crossing = -1.0f;
    double sum = 0.0, sum2 = 0.0;  // a float's E[x^2] - mean^2 is all rounding
    int count = 0;
    for (int b = 0; b < 2 * SECOND; b++) {
        sineBlock(b * BLOCK, 1000.0f, 0.3f);
        for (int i = 0; i < BLOCK; i++) {
            const float x = ol[i];
            if (b > 10 && last < 0.0f && x >= 0.0f) {
                const float at = static_cast<float>(b * BLOCK + i) - x / (x - last);
                if (previous_crossing >= 0.0f) {
                    const float interval = at - previous_crossing;
                    sum += interval;
                    sum2 += interval * interval;
                    count++;
                }
                previous_crossing = at;
            }
            last = x;
        }
    }
    const double mean = sum / count;
    const double variance = sum2 / count - mean * mean;
    return static_cast<float>(sqrt(variance > 0.0 ? variance : 0.0));
}

static void test_wow_and_flutter_bend_the_pitch()
{
    const float steady = crossingSpread(0.0f, 0.0f);
    const float wow = crossingSpread(1.0f, 0.0f);
    const float flutter = crossingSpread(0.0f, 1.0f);
    TEST_ASSERT_TRUE(steady < 0.01f);
    TEST_ASSERT_TRUE(wow > 0.2f);
    TEST_ASSERT_TRUE(flutter > 0.2f);
}

static void test_four_bits_swallow_a_quiet_signal()
{
    clean();
    tape->setParameter(FerricTape::PARAM_BITS, 1.0f);
    float e = 0.0f;
    for (int b = 0; b < 40; b++) {
        sineBlock(b * BLOCK, 440.0f, 0.02f);
        e += energy(ol) + energy(orr);
    }
    TEST_ASSERT_EQUAL_FLOAT(0.0f, e);
}

// The quietest and loudest block of a steady tone, after a warm-up: two
// whole cycles to a block, so a steady level reads the same in each.
static void levelRange(float dropout, float& quietest, float& loudest)
{
    clean();
    tape->setParameter(FerricTape::PARAM_DROPOUT, dropout);
    quietest = 1e9f;
    loudest = 0.0f;
    for (int b = 0; b < 8 * SECOND; b++) {
        sineBlock(b * BLOCK, 2.0f * RATE / BLOCK, 0.3f);
        if (b < 10) continue;
        const float e = energy(ol);
        if (e < quietest) quietest = e;
        if (e > loudest) loudest = e;
    }
}

static void test_dropouts_dip_the_level_only_when_asked()
{
    float quietest, loudest;
    levelRange(0.0f, quietest, loudest);
    TEST_ASSERT_TRUE(quietest > 0.9f * loudest);
    levelRange(1.0f, quietest, loudest);
    TEST_ASSERT_TRUE(quietest < 0.25f * loudest);
}

static void test_chorus_widens_a_mono_source()
{
    float difference[2];
    for (int c = 0; c < 2; c++) {
        clean();
        tape->setParameter(FerricTape::PARAM_CHORUS, c ? 1.0f : 0.0f);
        difference[c] = 0.0f;
        for (int b = 0; b < SECOND; b++) {
            sineBlock(b * BLOCK, 440.0f, 0.3f);
            for (int i = 0; i < BLOCK; i++) difference[c] += fabsf(ol[i] - orr[i]);
        }
    }
    TEST_ASSERT_EQUAL_FLOAT(0.0f, difference[0]);
    TEST_ASSERT_TRUE(difference[1] > 10.0f);
}

// The block in which the left output is loudest, from block `from` on,
// over `blocks` blocks after an impulse.
static int loudestBlock(int blocks, int from)
{
    il[0] = ir[0] = 0.5f;
    for (int i = 1; i < BLOCK; i++) il[i] = ir[i] = 0.0f;
    tape->processReplacing(in, out, BLOCK);
    int loudest = -1;
    float peak = 0.0f;
    for (int b = 1; b < blocks; b++) {
        silentBlock();
        if (b < from) continue;
        const float e = energy(ol);
        if (e > peak) {
            peak = e;
            loudest = b;
        }
    }
    return loudest;
}

static void test_the_slap_lands_at_its_time()
{
    clean();
    tape->setParameter(FerricTape::PARAM_SLAP, 1.0f);
    tape->setParameter(FerricTape::PARAM_TIME, 1.0f);  // 350 ms, 15435 samples
    // The tape head's 6.5 ms then 350 ms: 15722 samples, block 122.
    const int b = loudestBlock(SECOND / 2, 4);
    TEST_ASSERT_INT_WITHIN(1, 122, b);
}

static void test_feedback_brings_repeats()
{
    float late[2];
    for (int f = 0; f < 2; f++) {
        clean();
        tape->setParameter(FerricTape::PARAM_SLAP, 1.0f);
        tape->setParameter(FerricTape::PARAM_TIME, 0.0f);  // 40 ms
        tape->setParameter(FerricTape::PARAM_FEEDBACK, f ? 0.8f : 0.0f);
        il[0] = ir[0] = 0.5f;
        for (int i = 1; i < BLOCK; i++) il[i] = ir[i] = 0.0f;
        tape->processReplacing(in, out, BLOCK);
        late[f] = 0.0f;
        for (int b = 1; b < SECOND / 2; b++) {
            silentBlock();
            if (b >= 50) late[f] += energy(ol);  // past the first two repeats
        }
    }
    // Without feedback only the tone filters' tails are left.
    TEST_ASSERT_TRUE(late[0] < 1e-12f);
    TEST_ASSERT_TRUE(late[1] > 1e-6f);
}

static void test_everything_at_the_top_stays_bounded()
{
    clean();
    for (int p = 0; p < FerricTape::NUM_PARAMS; p++) tape->setParameter(p, 1.0f);
    uint32_t noise = 12345;
    float peak = 0.0f;
    for (int b = 0; b < 5 * SECOND; b++) {
        for (int i = 0; i < BLOCK; i++) {
            noise = noise * 1664525u + 1013904223u;
            il[i] = static_cast<float>(static_cast<int32_t>(noise)) / 2147483648.0f;
            ir[i] = 0.5f * il[i];
        }
        tape->processReplacing(in, out, BLOCK);
        for (int i = 0; i < BLOCK; i++) {
            TEST_ASSERT_TRUE(isfinite(ol[i]) && isfinite(orr[i]));
            if (fabsf(ol[i]) > peak) peak = fabsf(ol[i]);
            if (fabsf(orr[i]) > peak) peak = fabsf(orr[i]);
        }
    }
    // The tape, its chorus head added, is at most 1.5; the slap's loop is
    // held by the curve to its input plus pi / 2, and both reach the output.
    TEST_ASSERT_TRUE(peak < 5.0f);
}

static void test_clear_erases_the_tape_and_the_echoes()
{
    clean();
    tape->setParameter(FerricTape::PARAM_SLAP, 1.0f);
    tape->setParameter(FerricTape::PARAM_FEEDBACK, 1.0f);
    for (int b = 0; b < 20; b++) sineBlock(b * BLOCK, 440.0f, 0.5f);
    silentBlock();
    TEST_ASSERT_TRUE(energy(ol) > 1e-3f);
    tape->clear();
    for (int b = 0; b < 4; b++) {
        silentBlock();
        TEST_ASSERT_EQUAL_FLOAT(0.0f, energy(ol));
        TEST_ASSERT_EQUAL_FLOAT(0.0f, energy(orr));
    }
}

static void test_runs_any_block_length()
{
    // A block of 100 in one, then in 37 and 63: the same output, wobbles,
    // dropouts and all.
    static float sl[100], sr[100], a[2][100], b[2][100];
    for (int i = 0; i < 100; i++) {
        sl[i] = 0.3f * sinf(0.05f * static_cast<float>(i));
        sr[i] = -sl[i];
    }
    float* whole[2] = {sl, sr};
    float* tail[2] = {sl + 37, sr + 37};
    delete tape;
    tape = new FerricTape(nullptr);
    float* outA[2] = {a[0], a[1]};
    for (int n = 0; n < 50; n++) tape->processReplacing(whole, outA, 100);
    delete tape;
    tape = new FerricTape(nullptr);
    float* outB1[2] = {b[0], b[1]};
    float* outB2[2] = {b[0] + 37, b[1] + 37};
    for (int n = 0; n < 50; n++) {
        tape->processReplacing(whole, outB1, 37);
        tape->processReplacing(tail, outB2, 63);
    }
    TEST_ASSERT_EQUAL_FLOAT_ARRAY(a[0], b[0], 100);
    TEST_ASSERT_EQUAL_FLOAT_ARRAY(a[1], b[1], 100);
}

static void test_the_host_drives_it()
{
    clean();
    VSTEffect effect(*tape);
    TEST_ASSERT_TRUE(effect.supported());
    effect.setParameter(FerricTape::PARAM_WOW, 0.5f);
    TEST_ASSERT_EQUAL_FLOAT(0.5f, tape->getParameter(FerricTape::PARAM_WOW));
    char text[VSTEffect::TEXT_LEN];
    effect.effectName(text);
    TEST_ASSERT_EQUAL_STRING("Ferric", text);
}

int main()
{
    UNITY_BEGIN();
    RUN_TEST(test_identifies_and_declares_its_parameters);
    RUN_TEST(test_no_mix_is_the_dry_signal_exactly);
    RUN_TEST(test_clean_settings_pass_a_tone_at_about_its_level);
    RUN_TEST(test_wow_and_flutter_bend_the_pitch);
    RUN_TEST(test_four_bits_swallow_a_quiet_signal);
    RUN_TEST(test_dropouts_dip_the_level_only_when_asked);
    RUN_TEST(test_chorus_widens_a_mono_source);
    RUN_TEST(test_the_slap_lands_at_its_time);
    RUN_TEST(test_feedback_brings_repeats);
    RUN_TEST(test_everything_at_the_top_stays_bounded);
    RUN_TEST(test_clear_erases_the_tape_and_the_echoes);
    RUN_TEST(test_runs_any_block_length);
    RUN_TEST(test_the_host_drives_it);
    delete tape;
    return UNITY_END();
}

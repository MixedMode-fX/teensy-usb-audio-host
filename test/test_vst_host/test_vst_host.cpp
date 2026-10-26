// The VST host's framework-free parts: the sample conversion, the
// audioeffectx.h stand-in, and that the Airwindows sources the VSTHost
// example carries compile against it and process audio. The example's
// plugins.cpp is built into this environment for that (platformio.ini).
#include <unity.h>
#include <math.h>
#include "audioeffectx.h"
#include "vst_host/sample_format.h"
#include "vst_host/effect.h"
#include "vst_host/rack.h"
#include "../../examples/VSTHost/plugins.h"

void setUp() {}
void tearDown() {}

/* ------------------------------------------------------------------------- */
/* Sample format                                                             */
/* ------------------------------------------------------------------------- */

static void test_int16_survives_the_round_trip()
{
    const int16_t in[7] = {-32768, -32767, -1, 0, 1, 32766, 32767};
    float f[7];
    int16_t out[7];
    vst_host::toFloat(in, f, 7);
    TEST_ASSERT_FLOAT_WITHIN(1e-9f, -1.0f, f[0]);
    TEST_ASSERT_FLOAT_WITHIN(1e-9f, 0.0f, f[3]);
    vst_host::toInt16(f, out, 7);
    TEST_ASSERT_EQUAL_INT16_ARRAY(in, out, 7);
}

static void test_float_beyond_full_scale_clips()
{
    TEST_ASSERT_EQUAL_INT16(32767, vst_host::toSample(1.0f));
    TEST_ASSERT_EQUAL_INT16(32767, vst_host::toSample(7.5f));
    TEST_ASSERT_EQUAL_INT16(-32768, vst_host::toSample(-1.0f));
    TEST_ASSERT_EQUAL_INT16(-32768, vst_host::toSample(-3.0f));
    TEST_ASSERT_EQUAL_INT16(16384, vst_host::toSample(0.5f));
}

static void test_conversion_rounds_to_nearest()
{
    TEST_ASSERT_EQUAL_INT16(1, vst_host::toSample(0.6f / 32768.0f));
    TEST_ASSERT_EQUAL_INT16(0, vst_host::toSample(0.4f / 32768.0f));
    TEST_ASSERT_EQUAL_INT16(-1, vst_host::toSample(-0.6f / 32768.0f));
}

/* ------------------------------------------------------------------------- */
/* The interface stand-in                                                    */
/* ------------------------------------------------------------------------- */

// A plug-in written the way the SDK's examples are.
class Gain : public AudioEffectX {
public:
    explicit Gain(audioMasterCallback master) : AudioEffectX(master, 1, 1), gain_(0.5f)
    {
        setNumInputs(2);
        setNumOutputs(2);
        setUniqueID(0x6761696e);  // 'gain'
        canProcessReplacing();
    }
    virtual void setParameter(VstInt32, float value) { gain_ = value; }
    virtual float getParameter(VstInt32) { return gain_; }
    virtual void getParameterName(VstInt32, char* text) { vst_strncpy(text, "Gain", kVstMaxParamStrLen); }
    virtual void getParameterDisplay(VstInt32, char* text) { dB2string(gain_, text, kVstMaxParamStrLen); }
    virtual void getParameterLabel(VstInt32, char* text) { vst_strncpy(text, "dB", kVstMaxParamStrLen); }
    virtual bool getEffectName(char* name)
    {
        vst_strncpy(name, "Gain", kVstMaxEffectNameLen);
        return true;
    }
    virtual void processReplacing(float** inputs, float** outputs, VstInt32 frames)
    {
        for (int ch = 0; ch < 2; ch++)
            for (VstInt32 i = 0; i < frames; i++) outputs[ch][i] = inputs[ch][i] * gain_;
    }

private:
    float gain_;
};

static void test_plugin_declares_itself_through_the_base()
{
    Gain gain(nullptr);
    TEST_ASSERT_EQUAL_INT32(2, gain.getNumInputs());
    TEST_ASSERT_EQUAL_INT32(2, gain.getNumOutputs());
    TEST_ASSERT_EQUAL_INT32(1, gain.getNumParams());
    TEST_ASSERT_EQUAL_INT32(0x6761696e, gain.getUniqueID());
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 44100.0f, gain.getSampleRate());
    gain.setSampleRate(48000.0f);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 48000.0f, gain.getSampleRate());
    char text[kVstMaxEffectNameLen + 1];
    TEST_ASSERT_TRUE(gain.getEffectName(text));
    TEST_ASSERT_EQUAL_STRING("Gain", text);
}

static void test_plugin_processes_a_block()
{
    Gain gain(nullptr);
    float l[4] = {0.1f, 0.2f, -0.3f, 0.4f};
    float r[4] = {1.0f, -1.0f, 0.5f, 0.0f};
    float ol[4], orr[4];
    float* in[2] = {l, r};
    float* out[2] = {ol, orr};
    gain.processReplacing(in, out, 4);
    TEST_ASSERT_FLOAT_WITHIN(1e-6f, 0.05f, ol[0]);
    TEST_ASSERT_FLOAT_WITHIN(1e-6f, -0.5f, orr[1]);
}

static void test_strncpy_truncates_and_terminates()
{
    char text[kVstMaxParamStrLen + 1];
    memset(text, 'x', sizeof text);
    vst_strncpy(text, "Dry/Wet", kVstMaxParamStrLen);
    TEST_ASSERT_EQUAL_STRING("Dry/Wet", text);
    vst_strncpy(text, "a name too long", kVstMaxParamStrLen);
    TEST_ASSERT_EQUAL_STRING("a name t", text);
    TEST_ASSERT_EQUAL_UINT(kVstMaxParamStrLen, strlen(text));
}

static void test_value_to_text_fits_the_sdk_lengths()
{
    char text[kVstMaxParamStrLen + 1];
    AudioEffect::float2string(0.5f, text, kVstMaxParamStrLen);
    TEST_ASSERT_EQUAL_STRING("0.500000", text);
    AudioEffect::float2string(-12.25f, text, kVstMaxParamStrLen);
    TEST_ASSERT_EQUAL_STRING("-12.2500", text);
    AudioEffect::float2string(123456789.0f, text, kVstMaxParamStrLen);
    TEST_ASSERT_EQUAL_UINT(kVstMaxParamStrLen, strlen(text));
    AudioEffect::int2string(42, text, kVstMaxParamStrLen);
    TEST_ASSERT_EQUAL_STRING("42", text);
    AudioEffect::dB2string(1.0f, text, kVstMaxParamStrLen);
    TEST_ASSERT_EQUAL_STRING("0.000000", text);
    AudioEffect::dB2string(0.0f, text, kVstMaxParamStrLen);
    TEST_ASSERT_EQUAL_STRING("-oo", text);
}

/* ------------------------------------------------------------------------- */
/* The control surface a host puts around a plug-in                          */
/* ------------------------------------------------------------------------- */

static void test_effect_clamps_parameters_and_bounds_indices()
{
    Gain gain(nullptr);
    VSTEffect effect(gain);
    TEST_ASSERT_FALSE(effect.empty());
    TEST_ASSERT_TRUE(effect.supported());
    TEST_ASSERT_EQUAL_INT(1, effect.numParams());
    effect.setParameter(0, 1.7f);
    TEST_ASSERT_FLOAT_WITHIN(1e-6f, 1.0f, effect.getParameter(0));
    effect.setParameter(0, -0.2f);
    TEST_ASSERT_FLOAT_WITHIN(1e-6f, 0.0f, effect.getParameter(0));
    effect.setParameter(5, 0.5f);  // no such parameter: ignored
    TEST_ASSERT_FLOAT_WITHIN(1e-6f, 0.0f, effect.getParameter(0));
    TEST_ASSERT_FLOAT_WITHIN(1e-6f, 0.0f, effect.getParameter(5));
    char text[VSTEffect::TEXT_LEN];
    effect.parameterName(0, text);
    TEST_ASSERT_EQUAL_STRING("Gain", text);
    effect.parameterName(5, text);
    TEST_ASSERT_EQUAL_STRING("", text);
    effect.effectName(text);
    TEST_ASSERT_EQUAL_STRING("Gain", text);
    effect.sampleRate(48000.0f);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 48000.0f, gain.getSampleRate());
}

static void test_effect_processes_or_passes_through()
{
    Gain gain(nullptr);
    VSTEffect effect(gain);
    float l[4] = {0.1f, 0.2f, -0.3f, 0.4f};
    float r[4] = {1.0f, -1.0f, 0.5f, 0.0f};
    float ol[4], orr[4];
    float* in[2] = {l, r};
    float* out[2] = {ol, orr};
    effect.process(in, out, 4);  // gain 0.5
    TEST_ASSERT_FLOAT_WITHIN(1e-6f, 0.05f, ol[0]);
    TEST_ASSERT_FLOAT_WITHIN(1e-6f, -0.5f, orr[1]);
    effect.bypass(true);
    TEST_ASSERT_TRUE(effect.bypassed());
    effect.process(in, out, 4);
    TEST_ASSERT_EQUAL_FLOAT_ARRAY(l, ol, 4);
    TEST_ASSERT_EQUAL_FLOAT_ARRAY(r, orr, 4);
}

// A plug-in that wants more channels than the host carries.
class Quad : public Gain {
public:
    explicit Quad(audioMasterCallback master) : Gain(master)
    {
        setNumInputs(4);
        setNumOutputs(4);
    }
};

static void test_effect_passes_an_unsupported_plugin_through()
{
    Quad quad(nullptr);
    VSTEffect effect(quad);
    TEST_ASSERT_FALSE(effect.supported());
    float l[2] = {0.25f, -0.25f};
    float r[2] = {0.5f, -0.5f};
    float ol[2], orr[2];
    float* in[2] = {l, r};
    float* out[2] = {ol, orr};
    effect.process(in, out, 2);
    TEST_ASSERT_EQUAL_FLOAT_ARRAY(l, ol, 2);
    TEST_ASSERT_EQUAL_FLOAT_ARRAY(r, orr, 2);
}

static void test_empty_slot_is_a_wire()
{
    VSTEffect effect;
    TEST_ASSERT_TRUE(effect.empty());
    TEST_ASSERT_FALSE(effect.supported());
    TEST_ASSERT_EQUAL_INT(0, effect.numParams());
    char text[VSTEffect::TEXT_LEN];
    effect.effectName(text);
    TEST_ASSERT_EQUAL_STRING("wire", text);
    float l[2] = {0.25f, -0.25f};
    float r[2] = {0.5f, -0.5f};
    float ol[2], orr[2];
    float* in[2] = {l, r};
    float* out[2] = {ol, orr};
    effect.process(in, out, 2);
    TEST_ASSERT_EQUAL_FLOAT_ARRAY(l, ol, 2);
    TEST_ASSERT_EQUAL_FLOAT_ARRAY(r, orr, 2);
    Gain gain(nullptr);
    effect.attach(gain);
    TEST_ASSERT_TRUE(effect.supported());
    effect.process(in, out, 2);
    TEST_ASSERT_FLOAT_WITHIN(1e-6f, 0.125f, ol[0]);
}

static void test_effect_mix_blends_and_gain_scales()
{
    Gain gain(nullptr);  // halves
    VSTEffect effect(gain);
    float l[2] = {0.8f, -0.4f};
    float r[2] = {0.2f, 0.0f};
    float ol[2], orr[2];
    float* in[2] = {l, r};
    float* out[2] = {ol, orr};
    effect.mix(0.5f);  // half the plug-in's 0.4, half the input's 0.8
    effect.process(in, out, 2);
    TEST_ASSERT_FLOAT_WITHIN(1e-6f, 0.6f, ol[0]);
    TEST_ASSERT_FLOAT_WITHIN(1e-6f, -0.3f, ol[1]);
    TEST_ASSERT_FLOAT_WITHIN(1e-6f, 0.15f, orr[0]);
    effect.gain(2.0f);
    effect.process(in, out, 2);
    TEST_ASSERT_FLOAT_WITHIN(1e-6f, 1.2f, ol[0]);
    effect.mix(0.0f);  // all input, so the gain alone shows
    effect.process(in, out, 2);
    TEST_ASSERT_FLOAT_WITHIN(1e-6f, 1.6f, ol[0]);
    effect.bypass(true);  // the input at the gain, as a wire would be
    effect.gain(-1.0f);
    effect.process(in, out, 2);
    TEST_ASSERT_FLOAT_WITHIN(1e-6f, -0.8f, ol[0]);
    TEST_ASSERT_FLOAT_WITHIN(1e-6f, -0.2f, orr[0]);
    effect.mix(3.0f);
    effect.gain(100.0f);
    TEST_ASSERT_FLOAT_WITHIN(1e-6f, 1.0f, effect.mix());
    TEST_ASSERT_FLOAT_WITHIN(1e-6f, 8.0f, effect.gain());
}

/* ------------------------------------------------------------------------- */
/* A rack                                                                    */
/* ------------------------------------------------------------------------- */

static const int FRAMES = 4;
static float rack_in[2][FRAMES] = {{0.8f, -0.4f, 0.2f, 0.0f}, {0.1f, 0.2f, 0.3f, 0.4f}};
static float rack_a[2][FRAMES];
static float rack_b[2][FRAMES];
static float rack_c[2][FRAMES];

// Runs the rack over rack_in and returns the buffer holding the result.
static float** runRack(VSTRack& rack)
{
    static float* current[2];
    static float* next[2] = {rack_b[0], rack_b[1]};
    static float* spare[2] = {rack_c[0], rack_c[1]};
    for (int ch = 0; ch < 2; ch++) {
        current[ch] = rack_a[ch];
        memcpy(rack_a[ch], rack_in[ch], sizeof rack_a[ch]);
    }
    return rack.process(current, next, spare, FRAMES);
}

static void test_empty_rack_returns_its_input()
{
    VSTRack rack;
    TEST_ASSERT_EQUAL_INT(0, rack.size());
    TEST_ASSERT_EQUAL_INT(0, rack.stages());
    TEST_ASSERT_NULL(rack.slot(0));
    float** result = runRack(rack);
    TEST_ASSERT_EQUAL_PTR(rack_a[0], result[0]);
    TEST_ASSERT_EQUAL_FLOAT_ARRAY(rack_in[0], result[0], FRAMES);
}

static void test_rack_in_series_multiplies_through()
{
    Gain a(nullptr), b(nullptr), c(nullptr);  // each halves
    VSTRack rack;
    TEST_ASSERT_NOT_NULL(rack.add(a));
    TEST_ASSERT_NOT_NULL(rack.add(b));
    TEST_ASSERT_NOT_NULL(rack.add(c));
    TEST_ASSERT_EQUAL_INT(3, rack.size());
    TEST_ASSERT_EQUAL_INT(3, rack.stages());
    TEST_ASSERT_EQUAL_INT(2, rack.stageOf(2));
    float** result = runRack(rack);
    for (int i = 0; i < FRAMES; i++) {
        TEST_ASSERT_FLOAT_WITHIN(1e-6f, rack_in[0][i] / 8.0f, result[0][i]);
        TEST_ASSERT_FLOAT_WITHIN(1e-6f, rack_in[1][i] / 8.0f, result[1][i]);
    }
}

static void test_rack_in_parallel_sums_at_each_gain()
{
    Gain a(nullptr), b(nullptr);
    VSTRack rack;
    rack.add(a)->gain(1.0f);          // stage 0: 0.5 x
    rack.addParallel(b)->gain(0.5f);  //   + 0.25 x
    rack.addParallel()->gain(0.25f);  //   + 0.25 x, the dry signal
    TEST_ASSERT_EQUAL_INT(3, rack.size());
    TEST_ASSERT_EQUAL_INT(1, rack.stages());
    TEST_ASSERT_EQUAL_INT(0, rack.stageOf(2));
    float** result = runRack(rack);
    for (int i = 0; i < FRAMES; i++) {
        TEST_ASSERT_FLOAT_WITHIN(1e-6f, rack_in[0][i], result[0][i]);
        TEST_ASSERT_FLOAT_WITHIN(1e-6f, rack_in[1][i], result[1][i]);
    }
}

static void test_rack_mixes_series_and_parallel_stages()
{
    Gain a(nullptr), b(nullptr), c(nullptr);
    VSTRack rack;
    rack.add(a);                      // stage 0: 0.5 x
    rack.add(b)->gain(2.0f);          // stage 1: 0.5 x at 2, so x...
    rack.addParallel()->gain(1.0f);   //   ...plus x: 2x of the stage input, x of the rack's
    rack.add(c)->mix(0.5f);           // stage 2: half of 0.5 x and half of x: 0.75 x
    TEST_ASSERT_EQUAL_INT(3, rack.stages());
    TEST_ASSERT_EQUAL_INT(1, rack.stageOf(1));
    TEST_ASSERT_EQUAL_INT(1, rack.stageOf(2));
    TEST_ASSERT_EQUAL_INT(2, rack.stageOf(3));
    float** result = runRack(rack);
    for (int i = 0; i < FRAMES; i++) {
        TEST_ASSERT_FLOAT_WITHIN(1e-6f, rack_in[0][i] * 0.75f, result[0][i]);
        TEST_ASSERT_FLOAT_WITHIN(1e-6f, rack_in[1][i] * 0.75f, result[1][i]);
    }
}

static void test_rack_is_full_at_its_limit_and_tells_plugins_the_rate()
{
    Gain plugins[VSTRack::MAX_SLOTS + 1] = {Gain(nullptr), Gain(nullptr), Gain(nullptr), Gain(nullptr),
                                            Gain(nullptr), Gain(nullptr), Gain(nullptr), Gain(nullptr),
                                            Gain(nullptr)};
    VSTRack rack;
    rack.sampleRate(48000.0f);
    for (int n = 0; n < VSTRack::MAX_SLOTS; n++) TEST_ASSERT_NOT_NULL(rack.add(plugins[n]));
    TEST_ASSERT_NULL(rack.add(plugins[VSTRack::MAX_SLOTS]));
    TEST_ASSERT_EQUAL_INT(VSTRack::MAX_SLOTS, rack.size());
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 48000.0f, plugins[0].getSampleRate());
    rack.sampleRate(96000.0f);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 96000.0f, plugins[VSTRack::MAX_SLOTS - 1].getSampleRate());
}

static uint32_t fake_clock_ticks = 0;
static uint32_t fakeClock()
{
    fake_clock_ticks += 10;
    return fake_clock_ticks;
}

static void test_rack_times_each_slot_with_the_clock_it_is_given()
{
    Gain a(nullptr), b(nullptr);
    VSTRack rack;
    rack.add(a);
    rack.add(b);
    runRack(rack);
    TEST_ASSERT_EQUAL_UINT32(0, rack.cycles(0));
    rack.clock(fakeClock);
    runRack(rack);
    TEST_ASSERT_EQUAL_UINT32(10, rack.cycles(0));
    TEST_ASSERT_EQUAL_UINT32(10, rack.cycles(1));
    TEST_ASSERT_EQUAL_UINT32(10, rack.cyclesMax(1));
    TEST_ASSERT_EQUAL_UINT32(0, rack.cycles(2));
}

/* ------------------------------------------------------------------------- */
/* The Airwindows plug-ins the example carries                               */
/* ------------------------------------------------------------------------- */

static void test_airwindows_plugins_construct_and_identify()
{
    airwindows::capacitor::Capacitor capacitor(nullptr);
    airwindows::density::Density density(nullptr);
    airwindows::pressure4::Pressure4 pressure(nullptr);
    airwindows::tape::Tape tape(nullptr);
    airwindows::tremolo::Tremolo tremolo(nullptr);
    AudioEffectX* const all[] = {&capacitor, &density, &pressure, &tape, &tremolo};
    const char* const names[] = {"Capacitor", "Density", "Pressure4", "Tape", "Tremolo"};
    char text[kVstMaxProductStrLen + 1];
    for (int i = 0; i < 5; i++) {
        TEST_ASSERT_TRUE(all[i]->getEffectName(text));
        TEST_ASSERT_EQUAL_STRING(names[i], text);
        TEST_ASSERT_EQUAL_INT32(2, all[i]->getNumInputs());
        TEST_ASSERT_EQUAL_INT32(2, all[i]->getNumOutputs());
        TEST_ASSERT_TRUE(all[i]->getNumParams() >= 2);
        TEST_ASSERT_EQUAL(kPlugCategEffect, all[i]->getPlugCategory());
    }
    TEST_ASSERT_EQUAL_INT32(3, capacitor.getNumParams());
    capacitor.getParameterName(0, text);
    TEST_ASSERT_EQUAL_STRING("Lowpass", text);
    capacitor.getParameterName(2, text);
    TEST_ASSERT_EQUAL_STRING("Dry/Wet", text);
    capacitor.setParameter(0, 0.25f);
    TEST_ASSERT_FLOAT_WITHIN(1e-6f, 0.25f, capacitor.getParameter(0));
}

// A tone at a quarter of the sample rate, stereo, in blocks of 128.
static void tone(float* l, float* r, int frames)
{
    for (int i = 0; i < frames; i++) {
        l[i] = 0.5f * sinf(static_cast<float>(i) * static_cast<float>(M_PI) / 2.0f);
        r[i] = -l[i];
    }
}

static float peakToPeak(const float* x, int frames)
{
    float lo = x[0], hi = x[0];
    for (int i = 1; i < frames; i++) {
        if (x[i] < lo) lo = x[i];
        if (x[i] > hi) hi = x[i];
    }
    return hi - lo;
}

static void test_capacitor_dry_passes_the_signal_through()
{
    airwindows::capacitor::Capacitor capacitor(nullptr);
    capacitor.setSampleRate(44100.0f);
    capacitor.setParameter(0, 0.0f);  // lowpass shut
    capacitor.setParameter(2, 0.0f);  // but all dry
    float l[128], r[128], ol[128], orr[128];
    float* in[2] = {l, r};
    float* out[2] = {ol, orr};
    tone(l, r, 128);
    // The dry/wet control chases its setting over the first samples.
    capacitor.processReplacing(in, out, 128);
    capacitor.processReplacing(in, out, 128);
    for (int i = 0; i < 128; i++) {
        TEST_ASSERT_FLOAT_WITHIN(1e-4f, l[i], ol[i]);
        TEST_ASSERT_FLOAT_WITHIN(1e-4f, r[i], orr[i]);
    }
}

static void test_capacitor_shut_lowpass_removes_the_tone()
{
    airwindows::capacitor::Capacitor capacitor(nullptr);
    capacitor.setSampleRate(44100.0f);
    capacitor.setParameter(0, 0.0f);  // lowpass shut
    capacitor.setParameter(2, 1.0f);  // all wet
    float l[128], r[128], ol[128], orr[128];
    float* in[2] = {l, r};
    float* out[2] = {ol, orr};
    tone(l, r, 128);
    for (int block = 0; block < 20; block++) capacitor.processReplacing(in, out, 128);
    TEST_ASSERT_FLOAT_WITHIN(1e-6f, 1.0f, peakToPeak(l, 128));
    TEST_ASSERT_TRUE(peakToPeak(ol, 128) < 0.05f);
    TEST_ASSERT_TRUE(peakToPeak(orr, 128) < 0.05f);
}

static void test_density_clean_setting_is_near_unity()
{
    airwindows::density::Density density(nullptr);
    density.setSampleRate(44100.0f);
    density.setParameter(0, 0.2f);  // density 0: no saturation
    density.setParameter(1, 0.0f);  // no highpass
    density.setParameter(2, 1.0f);  // output 1
    density.setParameter(3, 1.0f);  // wet
    float l[128], r[128], ol[128], orr[128];
    float* in[2] = {l, r};
    float* out[2] = {ol, orr};
    tone(l, r, 128);
    density.processReplacing(in, out, 128);
    for (int i = 0; i < 128; i++) TEST_ASSERT_FLOAT_WITHIN(0.02f, l[i], ol[i]);
}

int main()
{
    UNITY_BEGIN();
    RUN_TEST(test_int16_survives_the_round_trip);
    RUN_TEST(test_float_beyond_full_scale_clips);
    RUN_TEST(test_conversion_rounds_to_nearest);
    RUN_TEST(test_plugin_declares_itself_through_the_base);
    RUN_TEST(test_plugin_processes_a_block);
    RUN_TEST(test_strncpy_truncates_and_terminates);
    RUN_TEST(test_value_to_text_fits_the_sdk_lengths);
    RUN_TEST(test_effect_clamps_parameters_and_bounds_indices);
    RUN_TEST(test_effect_processes_or_passes_through);
    RUN_TEST(test_effect_passes_an_unsupported_plugin_through);
    RUN_TEST(test_empty_slot_is_a_wire);
    RUN_TEST(test_effect_mix_blends_and_gain_scales);
    RUN_TEST(test_empty_rack_returns_its_input);
    RUN_TEST(test_rack_in_series_multiplies_through);
    RUN_TEST(test_rack_in_parallel_sums_at_each_gain);
    RUN_TEST(test_rack_mixes_series_and_parallel_stages);
    RUN_TEST(test_rack_is_full_at_its_limit_and_tells_plugins_the_rate);
    RUN_TEST(test_rack_times_each_slot_with_the_clock_it_is_given);
    RUN_TEST(test_airwindows_plugins_construct_and_identify);
    RUN_TEST(test_capacitor_dry_passes_the_signal_through);
    RUN_TEST(test_capacitor_shut_lowpass_removes_the_tone);
    RUN_TEST(test_density_clean_setting_is_near_unity);
    return UNITY_END();
}

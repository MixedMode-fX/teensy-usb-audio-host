// The VST host's framework-free parts: the sample conversion, the
// audioeffectx.h stand-in, and that the Airwindows sources the VSTHost
// example carries compile against it and process audio. The example's
// plugins.cpp is built into this environment for that (platformio.ini).
#include <unity.h>
#include <math.h>
#include "audioeffectx.h"
#include "vst_host/sample_format.h"
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
    RUN_TEST(test_airwindows_plugins_construct_and_identify);
    RUN_TEST(test_capacitor_dry_passes_the_signal_through);
    RUN_TEST(test_capacitor_shut_lowpass_removes_the_tone);
    RUN_TEST(test_density_clean_setting_is_near_unity);
    return UNITY_END();
}

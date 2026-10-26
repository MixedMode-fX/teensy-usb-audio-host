#ifndef VST_HOST_AUDIO_EFFECT_VST_H
#define VST_HOST_AUDIO_EFFECT_VST_H

#include <Arduino.h>
#include <AudioStream.h>
#include "audioeffectx.h"

// An effect written to the VST 2.x plug-in interface (the AudioEffectX class
// of audioeffectx.h) as a Teensy Audio Library object. Inputs 0 and 1 go to
// the plug-in's two inputs, its two outputs come out as outputs 0 and 1, a
// block at a time, in float.
//
//     AudioEffectVST<capacitor::Capacitor> filter;    // owns the plug-in
//     AudioConnection c1(in, 0, filter, 0);
//     AudioConnection c2(in, 1, filter, 1);
//     filter.setParameter(0, 0.5f);                    // 0..1, as VST does
//
// AudioEffectVST constructs the plug-in; AudioEffectVSTHost, its base, is the
// same object around a plug-in instance that lives elsewhere.
//
// Stereo in, stereo out is what the host carries, which is what every
// Airwindows plug-in is; a plug-in declaring more channels than that is
// passed through unprocessed and says so in supported(). A plug-in declaring
// fewer gets the first channels.
//
// Parameters are set from loop() and read by the plug-in at the start of
// each block it processes in the audio interrupt; a float store is one
// instruction, so no locking is needed. The plug-in is told
// AUDIO_SAMPLE_RATE_EXACT at construction; tell it the actual rate with
// sampleRate() once the interface has negotiated one.
//
// An unconnected input is silence to the plug-in, which still runs, so a
// reverb's tail rings out after its source stops. Bypass passes the input
// blocks through untouched and does not run the plug-in.
class AudioEffectVSTHost : public AudioStream {
public:
    static const int MAX_CHANNELS = 2;
    // Room for every string a plug-in writes, with its terminator; plug-ins
    // have been seen to write a product-length string as their name.
    static const int TEXT_LEN = kVstMaxProductStrLen + 1;

    explicit AudioEffectVSTHost(AudioEffectX& plugin);
    virtual void update(void);

    AudioEffectX& plugin() { return plugin_; }
    bool supported() const { return supported_; }

    void sampleRate(float hz) { plugin_.setSampleRate(hz); }
    float sampleRate() const { return plugin_.getSampleRate(); }

    void bypass(bool on) { bypass_ = on; }
    bool bypassed() const { return bypass_; }

    int numParams() const { return plugin_.getNumParams(); }
    void setParameter(int index, float value);
    float getParameter(int index);
    void parameterName(int index, char* text);     // text[TEXT_LEN]
    void parameterDisplay(int index, char* text);  // the value as the plug-in shows it
    void parameterLabel(int index, char* text);    // its unit
    void effectName(char* text);

    // One line for the effect and one per parameter, for a serial monitor.
    void printParameters(Print& out);

private:
    AudioEffectX& plugin_;
    audio_block_t* inputQueue_[MAX_CHANNELS];
    bool supported_;
    volatile bool bypass_;
};

// Holds the plug-in so it is constructed before the AudioStream base, which
// asks it how many inputs it has.
template <class Plugin>
struct AudioEffectVSTStorage {
    AudioEffectVSTStorage() : owned(nullptr) {}
    Plugin owned;
};

template <class Plugin>
class AudioEffectVST : private AudioEffectVSTStorage<Plugin>, public AudioEffectVSTHost {
public:
    AudioEffectVST() : AudioEffectVSTStorage<Plugin>(), AudioEffectVSTHost(this->owned) {}
    Plugin& plugin() { return this->owned; }
};

#endif

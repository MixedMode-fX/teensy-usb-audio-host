#ifndef VST_HOST_AUDIO_EFFECT_VST_H
#define VST_HOST_AUDIO_EFFECT_VST_H

#include <Arduino.h>
#include <AudioStream.h>
#include "audioeffectx.h"
#include "vst_host/effect.h"

// Effects written to the VST 2.x plug-in interface (the AudioEffectX class
// of audioeffectx.h) as Teensy Audio Library objects. Inputs 0 and 1 go to
// a plug-in's two inputs, its two outputs come out as outputs 0 and 1, a
// block at a time, in float.
//
// One plug-in, owned by the object:
//
//     AudioEffectVST<airwindows::capacitor::Capacitor> filter;
//     AudioConnection c1(in, 0, filter, 0);
//     filter.setParameter(0, 0.5f);                    // 0..1, as VST does
//
// A chain of them, which keeps the audio in float from one plug-in to the
// next instead of going through the Audio Library's 16-bit blocks between
// every two:
//
//     VSTPlugin<airwindows::capacitor::Capacitor> capacitor;
//     VSTPlugin<airwindows::tape::Tape> tape;
//     AudioEffectVSTChain chain;
//     ... chain.add(capacitor); chain.add(tape);        // in setup(), in signal order
//     chain.slot(1)->setParameter(0, 0.7f);
//
// The control surface of a plug-in in either (parameters, bypass, names) is
// VSTEffect (vst_host/effect.h): AudioEffectVSTHost is one, and the chain's
// slots are. Every plug-in is told AUDIO_SAMPLE_RATE_EXACT to begin with;
// tell it the interface's actual rate with sampleRate() once it is known.

// A plug-in instance a sketch can declare without the SDK's constructor
// argument: VSTPlugin<P> is a P constructed with no host callback.
template <class Plugin>
class VSTPlugin : public Plugin {
public:
    VSTPlugin() : Plugin(nullptr) {}
};

// One plug-in as an Audio Library object. An unconnected input is silence
// to the plug-in, which still runs, so a reverb's tail rings out after its
// source stops. Bypass passes the input blocks through untouched and does
// not run the plug-in.
class AudioEffectVSTHost : public AudioStream, public VSTEffect {
public:
    explicit AudioEffectVSTHost(AudioEffectX& plugin);
    virtual void update(void);

    // One line for the effect and one per parameter, for a serial monitor.
    void printParameters(Print& out);

private:
    audio_block_t* inputQueue_[VSTEffect::MAX_CHANNELS];
};

// Holds the plug-in so it is constructed before the AudioStream base, which
// asks it how many inputs it has.
template <class Plugin>
struct AudioEffectVSTStorage {
    AudioEffectVSTStorage() : owned() {}
    VSTPlugin<Plugin> owned;
};

template <class Plugin>
class AudioEffectVST : private AudioEffectVSTStorage<Plugin>, public AudioEffectVSTHost {
public:
    AudioEffectVST() : AudioEffectVSTStorage<Plugin>(), AudioEffectVSTHost(this->owned) {}
    Plugin& plugin() { return this->owned; }
};

// Slots in a chain. Overridable from the build.
#ifndef VST_HOST_MAX_CHAIN
#define VST_HOST_MAX_CHAIN 8
#endif

// A row of plug-ins as one Audio Library object: the input blocks are
// converted to float once, run through every slot in order, and converted
// back once. Add plug-ins in setup(); a slot added while audio runs joins
// at the next block. Per-slot processor use is measured with the cycle
// counter, as the Audio Library measures its objects.
class AudioEffectVSTChain : public AudioStream {
public:
    static const int MAX_SLOTS = VST_HOST_MAX_CHAIN;

    AudioEffectVSTChain();
    virtual void update(void);

    // Appends a plug-in and returns its slot, or nullptr when the chain is full.
    VSTEffect* add(AudioEffectX& plugin);
    int size() const { return size_; }
    VSTEffect* slot(int n) { return n >= 0 && n < size_ ? &slots_[n] : nullptr; }

    void sampleRate(float hz);  // tells every slot

    float processorUsage(int n) const;
    float processorUsageMax(int n) const;
    void processorUsageMaxReset();

    // Every slot, numbered, each as AudioEffectVSTHost::printParameters.
    void printParameters(Print& out);

private:
    VSTEffect slots_[MAX_SLOTS];
    volatile int size_;
    audio_block_t* inputQueue_[VSTEffect::MAX_CHANNELS];
    uint32_t cycles_[MAX_SLOTS];
    uint32_t cycles_max_[MAX_SLOTS];
};

#endif

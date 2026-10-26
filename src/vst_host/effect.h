#ifndef VST_HOST_EFFECT_H
#define VST_HOST_EFFECT_H

#include <string.h>
#include "audioeffectx.h"
#include "vst_host/sample_format.h"

// Channels a host carries through a plug-in: stereo, which is what every
// Airwindows plug-in is. Overridable from the build.
#ifndef VST_HOST_MAX_CHANNELS
#define VST_HOST_MAX_CHANNELS 2
#endif

// One plug-in and what a host does with it that has nothing to do with the
// Audio Library: its parameters and their text, bypass, the sample rate it
// is told, and processing a block of float channels. AudioEffectVSTHost puts
// the Audio Library around one of these; AudioEffectVSTChain holds a row of
// them. Framework-free.
//
// Parameters are 0..1 floats, set from loop() and read by the plug-in at the
// start of each block it processes; a float store is one instruction, so no
// locking is needed. A plug-in declaring more channels than MAX_CHANNELS is
// passed through unprocessed and says so in supported().
class VSTEffect {
public:
    static const int MAX_CHANNELS = VST_HOST_MAX_CHANNELS;
    // Room for every string a plug-in writes, with its terminator; plug-ins
    // have been seen to write a product-length string as their name.
    static const int TEXT_LEN = kVstMaxProductStrLen + 1;

    VSTEffect() : plugin_(nullptr), bypass_(false) {}
    explicit VSTEffect(AudioEffectX& plugin) : plugin_(&plugin), bypass_(false) {}
    VSTEffect(const VSTEffect&) = delete;
    VSTEffect& operator=(const VSTEffect&) = delete;

    void attach(AudioEffectX& plugin) { plugin_ = &plugin; }
    bool empty() const { return plugin_ == nullptr; }
    AudioEffectX& plugin() { return *plugin_; }  // only when not empty()
    bool supported() const
    {
        return plugin_ && plugin_->getNumInputs() <= MAX_CHANNELS && plugin_->getNumOutputs() <= MAX_CHANNELS;
    }

    void sampleRate(float hz)
    {
        if (plugin_) plugin_->setSampleRate(hz);
    }
    float sampleRate() const { return plugin_ ? plugin_->getSampleRate() : 0.0f; }

    void bypass(bool on) { bypass_ = on; }
    bool bypassed() const { return bypass_; }

    int numParams() const { return plugin_ ? plugin_->getNumParams() : 0; }
    void setParameter(int index, float value)
    {
        if (index < 0 || index >= numParams()) return;
        if (value < 0.0f) value = 0.0f;
        if (value > 1.0f) value = 1.0f;
        plugin_->setParameter(index, value);
    }
    float getParameter(int index) { return index >= 0 && index < numParams() ? plugin_->getParameter(index) : 0.0f; }
    void parameterName(int index, char* text)  // text[TEXT_LEN]
    {
        text[0] = 0;
        if (index >= 0 && index < numParams()) plugin_->getParameterName(index, text);
    }
    void parameterDisplay(int index, char* text)  // the value as the plug-in shows it
    {
        text[0] = 0;
        if (index >= 0 && index < numParams()) plugin_->getParameterDisplay(index, text);
    }
    void parameterLabel(int index, char* text)  // its unit
    {
        text[0] = 0;
        if (index >= 0 && index < numParams()) plugin_->getParameterLabel(index, text);
    }
    void effectName(char* text)
    {
        text[0] = 0;
        if (plugin_) plugin_->getEffectName(text);
    }

    // Runs the plug-in over MAX_CHANNELS buffers of frames each, in to out,
    // which must be distinct. Bypassed, unsupported or empty, it copies in
    // to out instead; a plug-in with fewer outputs leaves silence in the
    // rest.
    void process(float** in, float** out, int frames)
    {
        if (bypass_ || !supported()) {
            for (int ch = 0; ch < MAX_CHANNELS; ch++) memcpy(out[ch], in[ch], frames * sizeof(float));
            return;
        }
        plugin_->processReplacing(in, out, frames);
        for (int ch = plugin_->getNumOutputs(); ch < MAX_CHANNELS; ch++) vst_host::silence(out[ch], frames);
    }

private:
    AudioEffectX* plugin_;
    volatile bool bypass_;
};

#endif

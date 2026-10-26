#if defined(__MK66FX1M0__) || defined(__IMXRT1052__) || defined(__IMXRT1062__)

#include "vst_host/audio_effect_vst.h"
#include "vst_host/sample_format.h"

// One block of float buffers each way. Updates run one at a time in the
// Audio Library's software interrupt, so every host object can share them.
static float input_scratch[AudioEffectVSTHost::MAX_CHANNELS][AUDIO_BLOCK_SAMPLES];
static float output_scratch[AudioEffectVSTHost::MAX_CHANNELS][AUDIO_BLOCK_SAMPLES];

static unsigned char clampInputs(const AudioEffectX& plugin)
{
    const VstInt32 n = plugin.getNumInputs();
    if (n <= 0) return 0;
    return n > AudioEffectVSTHost::MAX_CHANNELS ? AudioEffectVSTHost::MAX_CHANNELS
                                                : static_cast<unsigned char>(n);
}

AudioEffectVSTHost::AudioEffectVSTHost(AudioEffectX& plugin)
    : AudioStream(clampInputs(plugin), inputQueue_),
      plugin_(plugin),
      inputQueue_(),
      supported_(plugin.getNumInputs() <= MAX_CHANNELS && plugin.getNumOutputs() <= MAX_CHANNELS),
      bypass_(false)
{
    plugin_.setSampleRate(AUDIO_SAMPLE_RATE_EXACT);
    plugin_.setBlockSize(AUDIO_BLOCK_SAMPLES);
    plugin_.resume();
}

void AudioEffectVSTHost::setParameter(int index, float value)
{
    if (index < 0 || index >= plugin_.getNumParams()) return;
    if (value < 0.0f) value = 0.0f;
    if (value > 1.0f) value = 1.0f;
    plugin_.setParameter(index, value);
}

float AudioEffectVSTHost::getParameter(int index)
{
    if (index < 0 || index >= plugin_.getNumParams()) return 0.0f;
    return plugin_.getParameter(index);
}

void AudioEffectVSTHost::parameterName(int index, char* text)
{
    text[0] = 0;
    if (index >= 0 && index < plugin_.getNumParams()) plugin_.getParameterName(index, text);
}

void AudioEffectVSTHost::parameterDisplay(int index, char* text)
{
    text[0] = 0;
    if (index >= 0 && index < plugin_.getNumParams()) plugin_.getParameterDisplay(index, text);
}

void AudioEffectVSTHost::parameterLabel(int index, char* text)
{
    text[0] = 0;
    if (index >= 0 && index < plugin_.getNumParams()) plugin_.getParameterLabel(index, text);
}

void AudioEffectVSTHost::effectName(char* text)
{
    text[0] = 0;
    plugin_.getEffectName(text);
}

void AudioEffectVSTHost::printParameters(Print& out)
{
    char text[TEXT_LEN];
    effectName(text);
    out.print(text);
    out.print(bypass_ ? " (bypassed)" : "");
    if (!supported_) out.print(" (unsupported channel count, passed through)");
    out.println();
    for (int i = 0; i < numParams(); i++) {
        out.print("  [");
        out.print(i);
        out.print("] ");
        parameterName(i, text);
        out.print(text);
        for (int pad = static_cast<int>(strlen(text)); pad < kVstMaxParamStrLen + 1; pad++) out.print(' ');
        out.print(getParameter(i), 3);
        out.print("  ");
        parameterDisplay(i, text);
        out.print(text);
        parameterLabel(i, text);
        if (text[0] != 0 && text[0] != ' ') {
            out.print(' ');
            out.print(text);
        }
        out.println();
    }
}

void AudioEffectVSTHost::update(void)
{
    audio_block_t* in[MAX_CHANNELS];
    for (int ch = 0; ch < MAX_CHANNELS; ch++) in[ch] = receiveReadOnly(ch);

    if (bypass_ || !supported_) {
        for (int ch = 0; ch < MAX_CHANNELS; ch++) {
            if (!in[ch]) continue;
            transmit(in[ch], ch);
            release(in[ch]);
        }
        return;
    }

    float* ins[MAX_CHANNELS];
    float* outs[MAX_CHANNELS];
    for (int ch = 0; ch < MAX_CHANNELS; ch++) {
        if (in[ch]) {
            vst_host::toFloat(in[ch]->data, input_scratch[ch], AUDIO_BLOCK_SAMPLES);
            release(in[ch]);
        } else {
            vst_host::silence(input_scratch[ch], AUDIO_BLOCK_SAMPLES);
        }
        ins[ch] = input_scratch[ch];
        outs[ch] = output_scratch[ch];
    }

    plugin_.processReplacing(ins, outs, AUDIO_BLOCK_SAMPLES);

    const int outputs = plugin_.getNumOutputs() < MAX_CHANNELS ? plugin_.getNumOutputs() : MAX_CHANNELS;
    for (int ch = 0; ch < outputs; ch++) {
        audio_block_t* block = allocate();
        if (!block) return;
        vst_host::toInt16(output_scratch[ch], block->data, AUDIO_BLOCK_SAMPLES);
        transmit(block, ch);
        release(block);
    }
}

#endif

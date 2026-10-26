#if defined(__MK66FX1M0__) || defined(__IMXRT1052__) || defined(__IMXRT1062__)

#include "vst_host/audio_effect_vst.h"
#include "vst_host/sample_format.h"

static const int CHANNELS = VSTEffect::MAX_CHANNELS;

// Two blocks of float buffers, one block each way, or the two sides of a
// chain's ping-pong. Updates run one at a time in the Audio Library's
// software interrupt, so every host object can share them.
static float scratch_a[CHANNELS][AUDIO_BLOCK_SAMPLES];
static float scratch_b[CHANNELS][AUDIO_BLOCK_SAMPLES];

static unsigned char clampInputs(const AudioEffectX& plugin)
{
    const VstInt32 n = plugin.getNumInputs();
    if (n <= 0) return 0;
    return n > CHANNELS ? CHANNELS : static_cast<unsigned char>(n);
}

// The input blocks as float; a missing block is silence.
static void blocksToFloat(audio_block_t* const* in, float** out)
{
    for (int ch = 0; ch < CHANNELS; ch++) {
        if (in[ch]) {
            vst_host::toFloat(in[ch]->data, out[ch], AUDIO_BLOCK_SAMPLES);
        } else {
            vst_host::silence(out[ch], AUDIO_BLOCK_SAMPLES);
        }
    }
}

static void printEffect(Print& out, VSTEffect& effect)
{
    char text[VSTEffect::TEXT_LEN];
    effect.effectName(text);
    out.print(text);
    out.print(effect.bypassed() ? " (bypassed)" : "");
    if (!effect.supported()) out.print(" (unsupported channel count, passed through)");
    out.println();
    for (int i = 0; i < effect.numParams(); i++) {
        out.print("  [");
        out.print(i);
        out.print("] ");
        effect.parameterName(i, text);
        out.print(text);
        for (int pad = static_cast<int>(strlen(text)); pad < kVstMaxParamStrLen + 1; pad++) out.print(' ');
        out.print(effect.getParameter(i), 3);
        out.print("  ");
        effect.parameterDisplay(i, text);
        out.print(text);
        effect.parameterLabel(i, text);
        if (text[0] != 0 && text[0] != ' ') {
            out.print(' ');
            out.print(text);
        }
        out.println();
    }
}

static float percentOfProcessor(uint32_t cycles)
{
#if defined(F_CPU_ACTUAL)
    const float clock = static_cast<float>(F_CPU_ACTUAL);
#else
    const float clock = static_cast<float>(F_CPU);
#endif
    return static_cast<float>(cycles) * (AUDIO_SAMPLE_RATE_EXACT / AUDIO_BLOCK_SAMPLES) * 100.0f / clock;
}

/* ------------------------------------------------------------------------- */
/* One plug-in                                                               */
/* ------------------------------------------------------------------------- */

AudioEffectVSTHost::AudioEffectVSTHost(AudioEffectX& plugin)
    : AudioStream(clampInputs(plugin), inputQueue_), VSTEffect(plugin), inputQueue_()
{
    plugin.setSampleRate(AUDIO_SAMPLE_RATE_EXACT);
    plugin.setBlockSize(AUDIO_BLOCK_SAMPLES);
    plugin.resume();
}

void AudioEffectVSTHost::printParameters(Print& out)
{
    printEffect(out, *this);
}

void AudioEffectVSTHost::update(void)
{
    audio_block_t* in[CHANNELS];
    for (int ch = 0; ch < CHANNELS; ch++) in[ch] = receiveReadOnly(ch);

    if (bypassed() || !supported()) {
        for (int ch = 0; ch < CHANNELS; ch++) {
            if (!in[ch]) continue;
            transmit(in[ch], ch);
            release(in[ch]);
        }
        return;
    }

    float* ins[CHANNELS];
    float* outs[CHANNELS];
    for (int ch = 0; ch < CHANNELS; ch++) {
        ins[ch] = scratch_a[ch];
        outs[ch] = scratch_b[ch];
    }
    blocksToFloat(in, ins);
    for (int ch = 0; ch < CHANNELS; ch++) {
        if (in[ch]) release(in[ch]);
    }
    process(ins, outs, AUDIO_BLOCK_SAMPLES);

    for (int ch = 0; ch < CHANNELS; ch++) {
        audio_block_t* block = allocate();
        if (!block) return;
        vst_host::toInt16(outs[ch], block->data, AUDIO_BLOCK_SAMPLES);
        transmit(block, ch);
        release(block);
    }
}

/* ------------------------------------------------------------------------- */
/* A chain                                                                   */
/* ------------------------------------------------------------------------- */

AudioEffectVSTChain::AudioEffectVSTChain()
    : AudioStream(CHANNELS, inputQueue_), slots_(), size_(0), inputQueue_(), cycles_(), cycles_max_()
{
}

VSTEffect* AudioEffectVSTChain::add(AudioEffectX& plugin)
{
    if (size_ >= MAX_SLOTS) return nullptr;
    VSTEffect& slot = slots_[size_];
    slot.attach(plugin);
    plugin.setSampleRate(AUDIO_SAMPLE_RATE_EXACT);
    plugin.setBlockSize(AUDIO_BLOCK_SAMPLES);
    plugin.resume();
    // The slot is complete before the audio interrupt can see it.
    size_ = size_ + 1;
    return &slot;
}

void AudioEffectVSTChain::sampleRate(float hz)
{
    for (int n = 0; n < size_; n++) slots_[n].sampleRate(hz);
}

float AudioEffectVSTChain::processorUsage(int n) const
{
    return n >= 0 && n < size_ ? percentOfProcessor(cycles_[n]) : 0.0f;
}

float AudioEffectVSTChain::processorUsageMax(int n) const
{
    return n >= 0 && n < size_ ? percentOfProcessor(cycles_max_[n]) : 0.0f;
}

void AudioEffectVSTChain::processorUsageMaxReset()
{
    for (int n = 0; n < MAX_SLOTS; n++) cycles_max_[n] = cycles_[n];
}

void AudioEffectVSTChain::printParameters(Print& out)
{
    for (int n = 0; n < size_; n++) {
        out.print(n);
        out.print(": ");
        printEffect(out, slots_[n]);
    }
}

void AudioEffectVSTChain::update(void)
{
    audio_block_t* in[CHANNELS];
    for (int ch = 0; ch < CHANNELS; ch++) in[ch] = receiveReadOnly(ch);

    const int count = size_;
    if (count == 0) {
        for (int ch = 0; ch < CHANNELS; ch++) {
            if (!in[ch]) continue;
            transmit(in[ch], ch);
            release(in[ch]);
        }
        return;
    }

    float* ping[CHANNELS];
    float* pong[CHANNELS];
    for (int ch = 0; ch < CHANNELS; ch++) {
        ping[ch] = scratch_a[ch];
        pong[ch] = scratch_b[ch];
    }
    float** current = ping;
    float** next = pong;
    blocksToFloat(in, current);
    for (int ch = 0; ch < CHANNELS; ch++) {
        if (in[ch]) release(in[ch]);
    }

    for (int n = 0; n < count; n++) {
        const uint32_t start = ARM_DWT_CYCCNT;
        slots_[n].process(current, next, AUDIO_BLOCK_SAMPLES);
        const uint32_t cycles = ARM_DWT_CYCCNT - start;
        cycles_[n] = cycles;
        if (cycles > cycles_max_[n]) cycles_max_[n] = cycles;
        float** done = next;
        next = current;
        current = done;
    }

    for (int ch = 0; ch < CHANNELS; ch++) {
        audio_block_t* block = allocate();
        if (!block) return;
        vst_host::toInt16(current[ch], block->data, AUDIO_BLOCK_SAMPLES);
        transmit(block, ch);
        release(block);
    }
}

#endif

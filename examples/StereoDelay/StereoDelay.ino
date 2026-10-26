// Ping-pong delay: a class-compliant USB audio interface on the Teensy's
// host port, each of its two inputs into a delay line whose output feeds
// back into the other side's line, so every repeat crosses the stereo
// field. Dry and wet are mixed on the way to the interface's outputs.
//
// The delay lines take their memory from the Audio Library's pool, so
// AudioMemory() is sized for the longest time offered: a second on a Teensy
// 4.x, 300 ms on a 3.6, whose RAM the USB host already takes most of. The feedback goes through a mixer in front of each
// line; a loop in the graph costs one block of extra delay per round, which
// the time setting does not include.
//
// Wiring: the interface into the host port (see the Passthrough example).
//
// Serial monitor keys:   t / T   delay time down / up
//                        f / F   feedback down / up
//                        m / M   wet mix down / up
//                        s       print the stream status

#include <Audio.h>
#include <USBHost_t36.h>
#include <USBAudioHost.h>

#if defined(__IMXRT1062__)
static const float MAX_DELAY_MS = 1000.0f;
static const int AUDIO_BLOCKS = 740;
#else
static const float MAX_DELAY_MS = 300.0f;
static const int AUDIO_BLOCKS = 240;
#endif

USBHost usb;
USBHub hub(usb);
USBAudioDevice interface(usb);

AudioInputUSBHost in(interface, 2);
AudioMixer4 feedL;  // 0: the input, 1: the other side's repeats
AudioMixer4 feedR;
AudioEffectDelay delayL;
AudioEffectDelay delayR;
AudioMixer4 mixL;  // 0: dry, 1: wet
AudioMixer4 mixR;
AudioOutputUSBHost out(interface, 2);

AudioConnection c1(in, 0, feedL, 0);
AudioConnection c2(delayR, 0, feedL, 1);
AudioConnection c3(feedL, 0, delayL, 0);
AudioConnection c4(in, 1, feedR, 0);
AudioConnection c5(delayL, 0, feedR, 1);
AudioConnection c6(feedR, 0, delayR, 0);
AudioConnection c7(in, 0, mixL, 0);
AudioConnection c8(delayL, 0, mixL, 1);
AudioConnection c9(in, 1, mixR, 0);
AudioConnection c10(delayR, 0, mixR, 1);
AudioConnection c11(mixL, 0, out, 0);
AudioConnection c12(mixR, 0, out, 1);

static float time_ms = 300.0f;
static float feedback = 0.4f;
static float mix = 0.4f;
static USBAudioDevice::State shown = USBAudioDevice::State::Idle;

static float clampf(float value, float lo, float hi)
{
    if (value < lo) return lo;
    if (value > hi) return hi;
    return value;
}

static void apply()
{
    delayL.delay(0, time_ms);
    delayR.delay(0, time_ms);
    feedL.gain(0, 1.0f);
    feedL.gain(1, feedback);
    feedR.gain(0, 1.0f);
    feedR.gain(1, feedback);
    mixL.gain(0, 1.0f - mix);
    mixL.gain(1, mix);
    mixR.gain(0, 1.0f - mix);
    mixR.gain(1, mix);
    Serial.print("time ");
    Serial.print(time_ms, 0);
    Serial.print(" ms  feedback ");
    Serial.print(feedback, 2);
    Serial.print("  mix ");
    Serial.println(mix, 1);
}

void setup()
{
    Serial.begin(115200);
    AudioMemory(AUDIO_BLOCKS);
    interface.setSampleRate(44100);  // the delay lines count samples at the Audio Library's rate
    interface.setChannels(2, 2);
    apply();
    usb.begin();
    in.begin();
    out.begin();
    Serial.println("USB audio ping-pong delay: t/T time, f/F feedback, m/M mix, s status");
}

void loop()
{
    usb.Task();

    if (interface.state() != shown) {
        shown = interface.state();
        interface.printStatus(Serial);
    }

    while (Serial.available()) {
        const int key = Serial.read();
        if (key == 't') time_ms = clampf(time_ms / 1.25f, 20.0f, MAX_DELAY_MS);
        if (key == 'T') time_ms = clampf(time_ms * 1.25f, 20.0f, MAX_DELAY_MS);
        if (key == 'f') feedback = clampf(feedback - 0.1f, 0.0f, 0.9f);
        if (key == 'F') feedback = clampf(feedback + 0.1f, 0.0f, 0.9f);
        if (key == 'm') mix = clampf(mix - 0.1f, 0.0f, 1.0f);
        if (key == 'M') mix = clampf(mix + 0.1f, 0.0f, 1.0f);
        if (strchr("tTfFmM", key)) apply();
        if (key == 's') {
            interface.printStatus(Serial);
            Serial.print("  audio library: ");
            Serial.print(AudioProcessorUsageMax());
            Serial.print("% cpu, ");
            Serial.print(AudioMemoryUsageMax());
            Serial.print(" of ");
            Serial.print(AUDIO_BLOCKS);
            Serial.println(" blocks");
        }
    }
}

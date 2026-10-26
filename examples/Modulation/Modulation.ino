// Chorus and flanger: a class-compliant USB audio interface on the Teensy's
// host port, each of its inputs through the Audio Library's chorus and its
// flanger side by side, with a mixer choosing which one (or neither) reaches
// the interface's outputs. Both effects run all the time so switching
// between them is a gain change, without a click.
//
// Wiring: the interface into the host port (see the Passthrough example).
//
// Serial monitor keys:   c       chorus
//                        f       flanger
//                        d       dry
//                        v       chorus voices: 2, 3, 4, then 2 again
//                        r / R   flanger sweep rate down / up
//                        s       print the stream status

#include <Audio.h>
#include <USBHost_t36.h>
#include <USBAudioHost.h>

// The delay lines the two effects modulate over, as in the Audio Library's
// own examples: 16 blocks for the chorus, 6 for the flanger.
static const int CHORUS_LINE = 16 * AUDIO_BLOCK_SAMPLES;
static const int FLANGE_LINE = 6 * AUDIO_BLOCK_SAMPLES;
static short chorusLineL[CHORUS_LINE];
static short chorusLineR[CHORUS_LINE];
static short flangeLineL[FLANGE_LINE];
static short flangeLineR[FLANGE_LINE];

USBHost usb;
USBHub hub(usb);
USBAudioDevice interface(usb);

AudioInputUSBHost in(interface, 2);
AudioEffectChorus chorusL;
AudioEffectChorus chorusR;
AudioEffectFlange flangeL;
AudioEffectFlange flangeR;
AudioMixer4 mixL;  // 0: chorus, 1: flanger, 2: dry
AudioMixer4 mixR;
AudioOutputUSBHost out(interface, 2);

AudioConnection c1(in, 0, chorusL, 0);
AudioConnection c2(in, 0, flangeL, 0);
AudioConnection c3(chorusL, 0, mixL, 0);
AudioConnection c4(flangeL, 0, mixL, 1);
AudioConnection c5(in, 0, mixL, 2);
AudioConnection c6(in, 1, chorusR, 0);
AudioConnection c7(in, 1, flangeR, 0);
AudioConnection c8(chorusR, 0, mixR, 0);
AudioConnection c9(flangeR, 0, mixR, 1);
AudioConnection c10(in, 1, mixR, 2);
AudioConnection c11(mixL, 0, out, 0);
AudioConnection c12(mixR, 0, out, 1);

enum Effect { CHORUS = 0, FLANGER = 1, DRY = 2 };
static Effect selected = CHORUS;
static int voices = 3;
static float flange_rate = 0.5f;  // Hz
static const int FLANGE_OFFSET = FLANGE_LINE / 4;
static const int FLANGE_DEPTH = FLANGE_LINE / 4;
static USBAudioDevice::State shown = USBAudioDevice::State::Idle;

static void apply()
{
    for (int channel = 0; channel < 3; channel++) {
        mixL.gain(channel, channel == selected ? 1.0f : 0.0f);
        mixR.gain(channel, channel == selected ? 1.0f : 0.0f);
    }
    chorusL.voices(voices);
    chorusR.voices(voices);
    flangeL.voices(FLANGE_OFFSET, FLANGE_DEPTH, flange_rate);
    flangeR.voices(FLANGE_OFFSET, FLANGE_DEPTH, flange_rate);
    static const char* const names[] = {"chorus", "flanger", "dry"};
    Serial.print(names[selected]);
    Serial.print("  (chorus voices ");
    Serial.print(voices);
    Serial.print(", flanger rate ");
    Serial.print(flange_rate, 2);
    Serial.println(" Hz)");
}

void setup()
{
    Serial.begin(115200);
    AudioMemory(40);
    interface.setSampleRate(44100);  // the sweeps are timed at the Audio Library's rate
    interface.setChannels(2, 2);
    chorusL.begin(chorusLineL, CHORUS_LINE, voices);
    chorusR.begin(chorusLineR, CHORUS_LINE, voices);
    flangeL.begin(flangeLineL, FLANGE_LINE, FLANGE_OFFSET, FLANGE_DEPTH, flange_rate);
    flangeR.begin(flangeLineR, FLANGE_LINE, FLANGE_OFFSET, FLANGE_DEPTH, flange_rate);
    apply();
    usb.begin();
    in.begin();
    out.begin();
    Serial.println("USB audio modulation: c chorus, f flanger, d dry, v voices, r/R flanger rate, s status");
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
        if (key == 'c') selected = CHORUS;
        if (key == 'f') selected = FLANGER;
        if (key == 'd') selected = DRY;
        if (key == 'v') voices = voices >= 4 ? 2 : voices + 1;
        if (key == 'r' && flange_rate > 0.1f) flange_rate /= 1.5f;
        if (key == 'R' && flange_rate < 8.0f) flange_rate *= 1.5f;
        if (strchr("cfdvrR", key)) apply();
        if (key == 's') {
            interface.printStatus(Serial);
            Serial.print("  audio library: ");
            Serial.print(AudioProcessorUsageMax());
            Serial.print("% cpu, ");
            Serial.print(AudioMemoryUsageMax());
            Serial.println(" blocks");
        }
    }
}

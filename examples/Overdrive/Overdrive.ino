// Overdrive: a class-compliant USB audio interface on the Teensy's host
// port, its inputs through a guitar-pedal chain of Audio Library objects
// and back out. Per channel: an amplifier for the drive, a waveshaper with
// a soft-clipping curve, a low-pass for the tone, a bit crusher for the
// lo-fi option, and an amplifier for the output level.
//
// Wiring: the interface into the host port (see the Passthrough example).
//
// Serial monitor keys:   d / D   drive down / up
//                        t / T   tone (low-pass cutoff) down / up
//                        b / B   bits down / up (16 is clean)
//                        l / L   level down / up
//                        s       print the stream status

#include <Audio.h>
#include <USBHost_t36.h>
#include <USBAudioHost.h>

USBHost usb;
USBHub hub(usb);
USBAudioDevice interface(usb);

AudioInputUSBHost in(interface, 2);
AudioAmplifier driveL;
AudioAmplifier driveR;
AudioEffectWaveshaper shaperL;
AudioEffectWaveshaper shaperR;
AudioFilterBiquad toneL;
AudioFilterBiquad toneR;
AudioEffectBitcrusher crushL;
AudioEffectBitcrusher crushR;
AudioAmplifier levelL;
AudioAmplifier levelR;
AudioOutputUSBHost out(interface, 2);

AudioConnection c1(in, 0, driveL, 0);
AudioConnection c2(driveL, 0, shaperL, 0);
AudioConnection c3(shaperL, 0, toneL, 0);
AudioConnection c4(toneL, 0, crushL, 0);
AudioConnection c5(crushL, 0, levelL, 0);
AudioConnection c6(levelL, 0, out, 0);
AudioConnection c7(in, 1, driveR, 0);
AudioConnection c8(driveR, 0, shaperR, 0);
AudioConnection c9(shaperR, 0, toneR, 0);
AudioConnection c10(toneR, 0, crushR, 0);
AudioConnection c11(crushR, 0, levelR, 0);
AudioConnection c12(levelR, 0, out, 1);

// The clipping curve: a tanh over the full 16-bit range, so the amplifier's
// gain sets how far into the bend the signal is pushed. The waveshaper wants
// a power of two plus one points.
static const int CURVE_POINTS = 257;
static float curve[CURVE_POINTS];

static float drive = 4.0f;      // gain into the curve
static float cutoff = 4000.0f;    // Hz
static int bits = 16;
static float level = 0.5f;
static USBAudioDevice::State shown = USBAudioDevice::State::Idle;

static float clampf(float value, float lo, float hi)
{
    if (value < lo) return lo;
    if (value > hi) return hi;
    return value;
}

static void apply()
{
    driveL.gain(drive);
    driveR.gain(drive);
    toneL.setLowpass(0, cutoff, 0.7f);
    toneR.setLowpass(0, cutoff, 0.7f);
    crushL.bits(bits);
    crushR.bits(bits);
    levelL.gain(level);
    levelR.gain(level);
    Serial.print("drive ");
    Serial.print(drive, 1);
    Serial.print("  tone ");
    Serial.print(cutoff, 0);
    Serial.print(" Hz  bits ");
    Serial.print(bits);
    Serial.print("  level ");
    Serial.println(level, 2);
}

void setup()
{
    Serial.begin(115200);
    AudioMemory(40);
    interface.setSampleRate(44100);  // what the Audio Library's filters assume
    interface.setChannels(2, 2);
    for (int i = 0; i < CURVE_POINTS; i++) {
        const float x = (2.0f * i) / (CURVE_POINTS - 1) - 1.0f;
        curve[i] = tanhf(3.0f * x) / tanhf(3.0f);
    }
    shaperL.shape(curve, CURVE_POINTS);
    shaperR.shape(curve, CURVE_POINTS);
    apply();
    usb.begin();
    in.begin();
    out.begin();
    Serial.println("USB audio overdrive: d/D drive, t/T tone, b/B bits, l/L level, s status");
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
        if (key == 'd') drive = clampf(drive / 1.5f, 1.0f, 40.0f);
        if (key == 'D') drive = clampf(drive * 1.5f, 1.0f, 40.0f);
        if (key == 't') cutoff = clampf(cutoff / 1.25f, 300.0f, 16000.0f);
        if (key == 'T') cutoff = clampf(cutoff * 1.25f, 300.0f, 16000.0f);
        if (key == 'b' && bits > 2) bits--;
        if (key == 'B' && bits < 16) bits++;
        if (key == 'l') level = clampf(level / 1.25f, 0.05f, 1.0f);
        if (key == 'L') level = clampf(level * 1.25f, 0.05f, 1.0f);
        if (strchr("dDtTbBlL", key)) apply();
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

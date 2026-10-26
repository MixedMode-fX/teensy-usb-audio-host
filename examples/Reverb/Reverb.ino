// Reverb: a class-compliant USB audio interface on the Teensy's host port,
// its two inputs summed into the Audio Library's stereo Freeverb and mixed
// back with the dry signal on the way to its outputs. The serial monitor
// turns the three knobs.
//
// Wiring: the interface into the host port (see the Passthrough example).
//
// Serial monitor keys:   r / R   room size down / up
//                        d / D   damping down / up
//                        m / M   wet mix down / up
//                        s       print the stream status

#include <Audio.h>
#include <USBHost_t36.h>
#include <USBAudioHost.h>

USBHost usb;
USBHub hub(usb);
USBAudioDevice interface(usb);

AudioInputUSBHost in(interface, 2);
AudioMixer4 sum;  // the reverb takes one channel: both inputs, halved
AudioEffectFreeverbStereo reverb;
AudioMixer4 mixL;  // 0: dry, 1: wet
AudioMixer4 mixR;
AudioOutputUSBHost out(interface, 2);

AudioConnection c1(in, 0, sum, 0);
AudioConnection c2(in, 1, sum, 1);
AudioConnection c3(sum, 0, reverb, 0);
AudioConnection c4(in, 0, mixL, 0);
AudioConnection c5(reverb, 0, mixL, 1);
AudioConnection c6(in, 1, mixR, 0);
AudioConnection c7(reverb, 1, mixR, 1);
AudioConnection c8(mixL, 0, out, 0);
AudioConnection c9(mixR, 0, out, 1);

static float room = 0.7f;
static float damp = 0.4f;
static float mix = 0.3f;
static USBAudioDevice::State shown = USBAudioDevice::State::Idle;

static float nudge(float value, bool up)
{
    value += up ? 0.1f : -0.1f;
    if (value < 0.0f) value = 0.0f;
    if (value > 1.0f) value = 1.0f;
    return value;
}

static void apply()
{
    reverb.roomsize(room);
    reverb.damping(damp);
    mixL.gain(0, 1.0f - mix);
    mixL.gain(1, mix);
    mixR.gain(0, 1.0f - mix);
    mixR.gain(1, mix);
    Serial.print("room ");
    Serial.print(room, 1);
    Serial.print("  damping ");
    Serial.print(damp, 1);
    Serial.print("  mix ");
    Serial.println(mix, 1);
}

void setup()
{
    Serial.begin(115200);
    AudioMemory(40);
    interface.setSampleRate(44100);  // what the Audio Library's effects assume
    interface.setChannels(2, 2);
    sum.gain(0, 0.5f);
    sum.gain(1, 0.5f);
    apply();
    usb.begin();
    in.begin();
    out.begin();
    Serial.println("USB audio reverb: r/R room, d/D damping, m/M mix, s status");
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
        if (key == 'r' || key == 'R') room = nudge(room, key == 'R');
        if (key == 'd' || key == 'D') damp = nudge(damp, key == 'D');
        if (key == 'm' || key == 'M') mix = nudge(mix, key == 'M');
        if (strchr("rRdDmM", key)) apply();
        if (key == 's') {
            interface.printStatus(Serial);
            Serial.print("  audio library: ");
            Serial.print(AudioProcessorUsageMax());
            Serial.print("% cpu, reverb ");
            Serial.print(reverb.processorUsageMax());
            Serial.print("%, ");
            Serial.print(AudioMemoryUsageMax());
            Serial.println(" blocks");
        }
    }
}

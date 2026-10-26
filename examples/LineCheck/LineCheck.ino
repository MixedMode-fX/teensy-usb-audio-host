// Bring-up: tells a bad output from a bad input on a new interface. The
// first two outputs play silence, a test tone or the first two inputs, and
// the inputs' peak levels are printed every second.
//
// What the output does in each mode says where a fault is:
//   silence  noise here means the packets themselves are wrong, whatever
//            the samples in them
//   tone     a clean 440 Hz tone means samples reach the interface as
//            meant; noise means their layout in the slot is not what the
//            interface reads
//   through  noise only here, with a clean tone, puts the fault on the input
// An input peak that never passes about 0.004 with a hot signal on it means
// the interface fills its slots from the bottom rather than the top.
//
// Serial monitor keys:   m  silence   t  tone   p  through
//                        s  print the stream status

#include <Audio.h>
#include <USBHost_t36.h>
#include <USBAudioHost.h>

USBHost usb;
USBHub hub(usb);
USBAudioDevice interface(usb);

AudioInputUSBHost in(interface, 2);
AudioSynthWaveformSine sine;
AudioMixer4 mixL;
AudioMixer4 mixR;
AudioAnalyzePeak peakL;
AudioAnalyzePeak peakR;
AudioOutputUSBHost out(interface, 2);

AudioConnection c1(in, 0, mixL, 0);
AudioConnection c2(in, 1, mixR, 0);
AudioConnection c3(sine, 0, mixL, 1);
AudioConnection c4(sine, 0, mixR, 1);
AudioConnection c5(in, 0, peakL, 0);
AudioConnection c6(in, 1, peakR, 0);
AudioConnection c7(mixL, 0, out, 0);
AudioConnection c8(mixR, 0, out, 1);

static USBAudioDevice::State shown = USBAudioDevice::State::Idle;
static elapsedMillis since_peaks;

// Gain of the input and the tone on both mixers.
static void route(float input, float test_tone, const char* name)
{
    mixL.gain(0, input);
    mixR.gain(0, input);
    mixL.gain(1, test_tone);
    mixR.gain(1, test_tone);
    Serial.print("output: ");
    Serial.println(name);
}

void setup()
{
    Serial.begin(115200);
    AudioMemory(40);
    interface.setSampleRate(44100);
    interface.setChannels(2, 2);
    sine.frequency(440.0f);
    sine.amplitude(0.25f);
    route(0.0f, 0.0f, "silence");
    usb.begin();
    in.begin();
    out.begin();
    Serial.println("USB audio line check: m silence, t tone, p through, s status");
}

void loop()
{
    usb.Task();

    if (interface.state() != shown) {
        shown = interface.state();
        interface.printStatus(Serial);
    }

    if (since_peaks >= 1000) {
        since_peaks = 0;
        if (interface.streaming() && peakL.available() && peakR.available()) {
            Serial.print("input peaks: ");
            Serial.print(peakL.read(), 4);
            Serial.print("  ");
            Serial.println(peakR.read(), 4);
        }
    }

    while (Serial.available()) {
        const int key = Serial.read();
        if (key == 'm') route(0.0f, 0.0f, "silence");
        if (key == 't') route(0.0f, 1.0f, "tone");
        if (key == 'p') route(1.0f, 0.0f, "through");
        if (key == 's') interface.printStatus(Serial);
    }
}

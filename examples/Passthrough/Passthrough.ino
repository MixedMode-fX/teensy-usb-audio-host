// Audio through: a class-compliant USB audio interface on the Teensy's host
// port, into the Teensy Audio Library and back out to the same interface,
// with a low-pass filter on the way that the serial monitor can sweep.
//
// Wiring: the interface's USB cable into the Teensy 4.1 host port (the 5-pin
// header; a USB host cable from PJRC) or the Teensy 3.6 host header. The
// interface is powered from its own supply or a powered hub: the Teensy
// cannot feed a bus-powered interface what it draws.
//
// Serial monitor keys:   f  lower the filter cutoff   g  raise it
//                        s  print the stream status

#include <Audio.h>
#include <USBHost_t36.h>
#include <USBAudioHost.h>

USBHost usb;
USBHub hub(usb);
USBAudioDevice interface(usb);

AudioInputUSBHost in(interface, 2);
AudioFilterStateVariable filterL;
AudioFilterStateVariable filterR;
AudioOutputUSBHost out(interface, 2);

AudioConnection c1(in, 0, filterL, 0);
AudioConnection c2(in, 1, filterR, 0);
AudioConnection c3(filterL, 0, out, 0);
AudioConnection c4(filterR, 0, out, 1);

// The state-variable filter turns unstable and sits at full scale near the
// top of its range at the default resonance, so the sweep stops well below.
static const float MAX_CUTOFF = 10000.0f;
static float cutoff = MAX_CUTOFF;
static USBAudioDevice::State shown = USBAudioDevice::State::Idle;

void setup()
{
    Serial.begin(115200);
    AudioMemory(40);
    // The Audio Library's filters, oscillators and effects assume 44.1 kHz;
    // ask the interface for that rate so they stay in tune. The graph
    // itself runs at whatever rate the interface ends up streaming.
    interface.setSampleRate(44100);
    interface.setChannels(2, 2);
    filterL.frequency(cutoff);
    filterR.frequency(cutoff);
    usb.begin();
    in.begin();
    out.begin();
    Serial.println("USB audio passthrough: plug a class-compliant interface into the host port");
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
        if (key == 'f' && cutoff > 100.0f) cutoff *= 0.8f;
        if (key == 'g' && cutoff < MAX_CUTOFF) cutoff *= 1.25f;
        if (key == 'f' || key == 'g') {
            filterL.frequency(cutoff);
            filterR.frequency(cutoff);
            Serial.print("cutoff ");
            Serial.print(cutoff);
            Serial.println(" Hz");
        }
        if (key == 's') {
            interface.printStatus(Serial);
            Serial.print("  audio library: ");
            Serial.print(AudioProcessorUsageMax());
            Serial.print("% cpu, ");
            Serial.print(AudioMemoryUsageMax());
            Serial.print(" blocks, input underruns ");
            Serial.print(in.underruns());
            Serial.print(", dropped ");
            Serial.println(in.dropped());
        }
    }
}

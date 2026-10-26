// A VST-style effect host: five of the open-source Airwindows plug-ins,
// compiled into the sketch unmodified against the library's audioeffectx.h,
// run as a rack between the inputs and outputs of a class-compliant USB
// audio interface on the Teensy's host port. The serial monitor is the
// rack's front panel.
//
// A plug-in binary from a computer cannot be loaded on a microcontroller;
// its source can be compiled in, and the Airwindows catalogue is written to
// exactly the interface this host provides. plugins.h is where they are
// listed; airwindows/README.md says where they come from.
//
// The rack is one Audio Library object: the audio is converted to float
// once, goes through every stage in float, and comes back to 16 bits once.
// Stages run in series; the slots within a stage run in parallel on the
// same input and are summed, each at its gain. The rack here:
//
//   stage 0  Capacitor                          a filter
//   stage 1  Density, half wet                  saturation, blended with its input
//   stage 2  Pressure4 at 0.7 + a wire at 0.5   parallel compression
//   stage 3  Tape                               tape saturation
//   stage 4  Tremolo                            depth off until asked for
//
// Wiring: the interface into the host port (see the Passthrough example).
//
// Boards: a Teensy 4.x. The sketch builds for a Teensy 3.6 too, but
// Airwindows is double-precision arithmetic throughout and the 3.6 has no
// double-precision FPU: one plug-in exceeds its processor.
//
// Serial monitor, one command per line:
//   l                list the slots, their stages and their parameters
//   p <n> <i> <v>    set parameter i of slot n to v (0 to 1)
//   m <n> <v>        slot n's mix: 1 all plug-in, 0 all input
//   g <n> <v>        slot n's gain, its weight in its stage's sum
//   b <n>            bypass slot n, or bring it back
//   s                stream status and processor use per slot

#include <Audio.h>
#include <USBHost_t36.h>
#include <USBAudioHost.h>
#include <AudioEffectVST.h>
#include "plugins.h"

USBHost usb;
USBHub hub(usb);
USBAudioDevice interface(usb);

VSTPlugin<airwindows::capacitor::Capacitor> capacitor;
VSTPlugin<airwindows::density::Density> density;
VSTPlugin<airwindows::pressure4::Pressure4> pressure;
VSTPlugin<airwindows::tape::Tape> tape;
VSTPlugin<airwindows::tremolo::Tremolo> tremolo;

AudioInputUSBHost in(interface, 2);
AudioEffectVSTChain chain;
AudioOutputUSBHost out(interface, 2);

AudioConnection c1(in, 0, chain, 0);
AudioConnection c2(in, 1, chain, 1);
AudioConnection c3(chain, 0, out, 0);
AudioConnection c4(chain, 1, out, 1);

static char line[48];
static size_t line_len = 0;
static USBAudioDevice::State shown = USBAudioDevice::State::Idle;

static void printStatus()
{
    interface.printStatus(Serial);
    Serial.print("  audio library: ");
    Serial.print(AudioProcessorUsageMax());
    Serial.print("% cpu, ");
    Serial.print(AudioMemoryUsageMax());
    Serial.print(" blocks, input underruns ");
    Serial.print(in.underruns());
    Serial.print(", dropped ");
    Serial.println(in.dropped());
    char name[VSTEffect::TEXT_LEN];
    for (int n = 0; n < chain.size(); n++) {
        chain.slot(n)->effectName(name);
        Serial.print("  ");
        Serial.print(n);
        Serial.print(": ");
        Serial.print(name);
        Serial.print(' ');
        Serial.print(chain.processorUsageMax(n));
        Serial.println(chain.slot(n)->bypassed() ? "% cpu, bypassed" : "% cpu");
    }
}

static VSTEffect* slotAt(long n)
{
    VSTEffect* slot = chain.slot(n);
    if (!slot) Serial.println("no such slot; l lists them");
    return slot;
}

static void printSlotLine(VSTEffect* slot, const char* what, float value)
{
    char text[VSTEffect::TEXT_LEN];
    slot->effectName(text);
    Serial.print(text);
    Serial.print(' ');
    Serial.print(what);
    Serial.print(' ');
    Serial.println(value, 2);
}

static void handleLine(char* s)
{
    while (*s == ' ') s++;
    const char command = *s;
    char* p = s + 1;
    if (command == 'l') {
        chain.printParameters(Serial);
    } else if (command == 'p') {
        const long n = strtol(p, &p, 10);
        const long i = strtol(p, &p, 10);
        const float v = strtof(p, &p);
        VSTEffect* slot = slotAt(n);
        if (!slot) return;
        if (i < 0 || i >= slot->numParams()) {
            Serial.println("no such parameter; l lists them");
            return;
        }
        slot->setParameter(i, v);
        char text[VSTEffect::TEXT_LEN];
        slot->parameterName(i, text);
        Serial.print(text);
        Serial.print(" = ");
        slot->parameterDisplay(i, text);
        Serial.print(text);
        slot->parameterLabel(i, text);
        Serial.print(' ');
        Serial.println(text);
    } else if (command == 'm' || command == 'g') {
        const long n = strtol(p, &p, 10);
        const float v = strtof(p, &p);
        VSTEffect* slot = slotAt(n);
        if (!slot) return;
        if (command == 'm') {
            slot->mix(v);
            printSlotLine(slot, "mix", slot->mix());
        } else {
            slot->gain(v);
            printSlotLine(slot, "gain", slot->gain());
        }
    } else if (command == 'b') {
        VSTEffect* slot = slotAt(strtol(p, &p, 10));
        if (!slot) return;
        slot->bypass(!slot->bypassed());
        char text[VSTEffect::TEXT_LEN];
        slot->effectName(text);
        Serial.print(text);
        Serial.println(slot->bypassed() ? " bypassed" : " active");
    } else if (command == 's') {
        printStatus();
    } else if (command != 0) {
        Serial.println("commands: l, p <n> <i> <v>, m <n> <v>, g <n> <v>, b <n>, s");
    }
}

static void readSerial()
{
    while (Serial.available()) {
        const int c = Serial.read();
        if (c == '\n' || c == '\r') {
            line[line_len] = 0;
            line_len = 0;
            handleLine(line);
        } else if (line_len < sizeof line - 1) {
            line[line_len++] = static_cast<char>(c);
        }
    }
}

void setup()
{
    Serial.begin(115200);
    AudioMemory(40);
    interface.setSampleRate(44100);
    interface.setChannels(2, 2);

    // The rack, in signal order. Airwindows parameters are 0..1 and start
    // where the plug-in's author left them, which is mostly transparent;
    // a little of each, to hear.
    chain.add(capacitor);                                   // 0
    VSTEffect* saturation = chain.add(density);             // 1
    saturation->setParameter(0, 0.4f);                      //    some saturation...
    saturation->mix(0.5f);                                  //    ...blended half and half
    VSTEffect* compressed = chain.add(pressure);            // 2
    compressed->setParameter(0, 0.5f);                      //    squashed hard...
    compressed->gain(0.7f);
    chain.addParallel()->gain(0.5f);                        // 3  ...next to the dry signal
    chain.add(tape);                                        // 4
    chain.add(tremolo)->setParameter(1, 0.0f);              // 5  depth off until asked for

    usb.begin();
    in.begin();
    out.begin();
    Serial.println("VST host: l lists the rack; p/m/g/b set a slot's parameter, mix, gain, bypass");
}

void loop()
{
    usb.Task();

    if (interface.state() != shown) {
        shown = interface.state();
        interface.printStatus(Serial);
        // The plug-ins scale their time constants by the rate they are told.
        if (shown == USBAudioDevice::State::Streaming) chain.sampleRate(interface.sampleRate());
    }

    readSerial();
}

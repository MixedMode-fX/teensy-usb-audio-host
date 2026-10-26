// A VST-style effect host: five of the open-source Airwindows plug-ins,
// compiled into the sketch unmodified against the library's audioeffectx.h,
// run in a chain between the inputs and outputs of a class-compliant USB
// audio interface on the Teensy's host port. The serial monitor is the
// rack's front panel.
//
// A plug-in binary from a computer cannot be loaded on a microcontroller;
// its source can be compiled in, and the Airwindows catalogue is written to
// exactly the interface this host provides. plugins.h is where they are
// listed; airwindows/README.md says where they come from.
//
// Wiring: the interface into the host port (see the Passthrough example).
//
// Boards: a Teensy 4.x. The sketch builds for a Teensy 3.6 too, but
// Airwindows is double-precision arithmetic throughout and the 3.6 has no
// double-precision FPU: one plug-in exceeds its processor.
//
// Serial monitor, one command per line:
//   l                list the plug-ins and their parameters
//   p <n> <i> <v>    set parameter i of plug-in n to v (0 to 1)
//   b <n>            bypass plug-in n, or bring it back
//   s                stream status and processor use

#include <Audio.h>
#include <USBHost_t36.h>
#include <USBAudioHost.h>
#include <AudioEffectVST.h>
#include "plugins.h"

USBHost usb;
USBHub hub(usb);
USBAudioDevice interface(usb);

AudioInputUSBHost in(interface, 2);
AudioEffectVST<airwindows::capacitor::Capacitor> filter;
AudioEffectVST<airwindows::density::Density> drive;
AudioEffectVST<airwindows::pressure4::Pressure4> compressor;
AudioEffectVST<airwindows::tape::Tape> tape;
AudioEffectVST<airwindows::tremolo::Tremolo> tremolo;
AudioOutputUSBHost out(interface, 2);

AudioConnection c1(in, 0, filter, 0);
AudioConnection c2(in, 1, filter, 1);
AudioConnection c3(filter, 0, drive, 0);
AudioConnection c4(filter, 1, drive, 1);
AudioConnection c5(drive, 0, compressor, 0);
AudioConnection c6(drive, 1, compressor, 1);
AudioConnection c7(compressor, 0, tape, 0);
AudioConnection c8(compressor, 1, tape, 1);
AudioConnection c9(tape, 0, tremolo, 0);
AudioConnection c10(tape, 1, tremolo, 1);
AudioConnection c11(tremolo, 0, out, 0);
AudioConnection c12(tremolo, 1, out, 1);

// The rack, in signal order. Everything below works on this list.
static AudioEffectVSTHost* const rack[] = {&filter, &drive, &compressor, &tape, &tremolo};
static const int RACK_SIZE = sizeof rack / sizeof rack[0];

static char line[48];
static size_t line_len = 0;
static USBAudioDevice::State shown = USBAudioDevice::State::Idle;

static void listRack()
{
    for (int n = 0; n < RACK_SIZE; n++) {
        Serial.print(n);
        Serial.print(": ");
        rack[n]->printParameters(Serial);
    }
}

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
    char name[AudioEffectVSTHost::TEXT_LEN];
    for (int n = 0; n < RACK_SIZE; n++) {
        rack[n]->effectName(name);
        Serial.print("  ");
        Serial.print(name);
        Serial.print(": ");
        Serial.print(rack[n]->processorUsageMax());
        Serial.println(rack[n]->bypassed() ? "% cpu, bypassed" : "% cpu");
    }
}

static AudioEffectVSTHost* pluginAt(long n)
{
    if (n < 0 || n >= RACK_SIZE) {
        Serial.println("no such plug-in; l lists them");
        return nullptr;
    }
    return rack[n];
}

static void handleLine(char* s)
{
    while (*s == ' ') s++;
    const char command = *s;
    char* p = s + 1;
    if (command == 'l') {
        listRack();
    } else if (command == 'p') {
        const long n = strtol(p, &p, 10);
        const long i = strtol(p, &p, 10);
        const float v = strtof(p, &p);
        AudioEffectVSTHost* plugin = pluginAt(n);
        if (!plugin) return;
        if (i < 0 || i >= plugin->numParams()) {
            Serial.println("no such parameter; l lists them");
            return;
        }
        plugin->setParameter(i, v);
        char text[AudioEffectVSTHost::TEXT_LEN];
        plugin->parameterName(i, text);
        Serial.print(text);
        Serial.print(" = ");
        plugin->parameterDisplay(i, text);
        Serial.print(text);
        plugin->parameterLabel(i, text);
        Serial.print(' ');
        Serial.println(text);
    } else if (command == 'b') {
        AudioEffectVSTHost* plugin = pluginAt(strtol(p, &p, 10));
        if (!plugin) return;
        plugin->bypass(!plugin->bypassed());
        char text[AudioEffectVSTHost::TEXT_LEN];
        plugin->effectName(text);
        Serial.print(text);
        Serial.println(plugin->bypassed() ? " bypassed" : " active");
    } else if (command == 's') {
        printStatus();
    } else if (command != 0) {
        Serial.println("commands: l, p <n> <i> <v>, b <n>, s");
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
    // Airwindows parameters are 0..1 and start where the plug-in's author
    // left them, which is mostly transparent. A little of each, to hear:
    drive.setParameter(0, 0.4f);       // Density: some saturation
    compressor.setParameter(0, 0.3f);  // Pressure4: a touch of compression
    tremolo.setParameter(1, 0.0f);     // Tremolo: depth off until asked for
    usb.begin();
    in.begin();
    out.begin();
    Serial.println("VST host: l lists the rack, p <plug-in> <parameter> <0..1> sets, b <plug-in> bypasses");
}

void loop()
{
    usb.Task();

    if (interface.state() != shown) {
        shown = interface.state();
        interface.printStatus(Serial);
        if (shown == USBAudioDevice::State::Streaming) {
            // The plug-ins scale their time constants by the rate they are told.
            for (int n = 0; n < RACK_SIZE; n++) rack[n]->sampleRate(interface.sampleRate());
        }
    }

    readSerial();
}

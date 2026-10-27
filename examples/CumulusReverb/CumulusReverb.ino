// CumulusReverb: the reverb of Mutable Instruments' Clouds, by Emilie
// Gillet, between a class-compliant USB audio interface's first two inputs
// and outputs, played from a USB MIDI controller. Each of its controls
// answers a control change number, by a default map or by MIDI learn, and
// the serial monitor shows what moved.
//
// Wiring: a powered USB hub into the Teensy's host port, with the audio
// interface and the MIDI controller both on the hub, as for the VSTHostMIDI
// example, which says why there are two MIDI and two hub drivers. Without a
// controller the serial monitor sets the controls.
//
// Boards: a Teensy 4.x or 3.6. The reverb is single-precision float and
// carries 32 KB of delay memory.
//
// The plug-in is cumulus_reverb.h/.cpp next to this sketch, compiled from
// the unmodified Clouds sources under clouds/ and stmlib/ (see
// clouds/README.md); cumulus_reverb.h says what the controls do. Clouds is
// tuned for 32 kHz, and its space is smaller and its shimmer faster at the
// 44.1 kHz asked for here.
//
// The default map, general MIDI's effect depth and sound controllers where
// one fits, the undefined block from CC 20 elsewhere, and the
// general-purpose buttons from CC 80:
//
//   CC 91  Amount    (effects 1 depth: reverb send)
//   CC 75  Time      (sound controller 6: decay time)
//   CC 20  Diffuse
//   CC 74  Tone      (sound controller 5: brightness)
//   CC 21  Input
//   CC 22  Clock
//   CC 23  Warp
//   CC 24  Speed
//   CC 25  Glitch
//   CC 80  bypass, on at 64 and above, as a latching button sends it
//   CC 81  clear the tail, when pressed (64 and above)
//
// A CC's 0..127 is the control's 0..1.
//
// Serial monitor, one command per line; t names a control: its number
// (0 Amount .. 8 Glitch), b (bypass) or e (clear, as in empty):
//   l              the controls and their values
//   c              the CC map
//   v <t> <0..127> set a control as a control change would
//   a <cc> <t>     map control change cc to t
//   x <cc>         unmap it
//   L <t>          learn: the next control change to arrive is mapped to t
//   h <ch>         listen on MIDI channel ch (1 to 16), or 0 for all of them
//   s              stream status, MIDI devices and processor use

#include <Audio.h>
#include <USBHost_t36.h>
#include <USBAudioHost.h>
#include <AudioEffectVST.h>
#include "cumulus_reverb.h"

USBHost usb;
USBHub hub1(usb);
USBHub hub2(usb);
USBAudioDevice interface(usb);
MIDIDevice_BigBuffer midi1(usb);
MIDIDevice_BigBuffer midi2(usb);

AudioInputUSBHost in(interface, 2);
AudioEffectVST<CumulusReverb> reverb;
AudioOutputUSBHost out(interface, 2);

AudioConnection c1(in, 0, reverb, 0);
AudioConnection c2(in, 1, reverb, 1);
AudioConnection c3(reverb, 0, out, 0);
AudioConnection c4(reverb, 1, out, 1);

// What a control change drives: a parameter of the reverb, or one of these.
static const int8_t UNMAPPED = -1;
static const int8_t CONTROL_BYPASS = -2;
static const int8_t CONTROL_CLEAR = -3;
static int8_t cc_map[128];

static uint8_t midi_channel = 0;  // 1..16, 0 for every channel
static bool learning = false;
static int8_t learn_target = UNMAPPED;

static char line[48];
static size_t line_len = 0;
static USBAudioDevice::State shown = USBAudioDevice::State::Idle;
static bool midi_shown[2] = {false, false};

static void defaultMap()
{
    for (int8_t& t : cc_map) t = UNMAPPED;
    cc_map[91] = CumulusReverb::PARAM_AMOUNT;
    cc_map[75] = CumulusReverb::PARAM_TIME;
    cc_map[20] = CumulusReverb::PARAM_DIFFUSION;
    cc_map[74] = CumulusReverb::PARAM_TONE;
    cc_map[21] = CumulusReverb::PARAM_INPUT;
    cc_map[22] = CumulusReverb::PARAM_CLOCK;
    cc_map[23] = CumulusReverb::PARAM_WARP;
    cc_map[24] = CumulusReverb::PARAM_SPEED;
    cc_map[25] = CumulusReverb::PARAM_GLITCH;
    cc_map[80] = CONTROL_BYPASS;
    cc_map[81] = CONTROL_CLEAR;
}

static bool valid(int8_t t)
{
    return t == CONTROL_BYPASS || t == CONTROL_CLEAR || (t >= 0 && t < reverb.numParams());
}

static void printTarget(int8_t t)
{
    if (t == CONTROL_BYPASS) {
        Serial.print("bypass");
    } else if (t == CONTROL_CLEAR) {
        Serial.print("clear");
    } else {
        char text[VSTEffect::TEXT_LEN];
        reverb.parameterName(t, text);
        Serial.print(text);
    }
}

static void printValue(int8_t t)
{
    if (t == CONTROL_BYPASS) {
        Serial.print(reverb.bypassed() ? "on" : "off");
    } else if (t == CONTROL_CLEAR) {
        Serial.print("done");
    } else {
        char text[VSTEffect::TEXT_LEN];
        reverb.parameterDisplay(t, text);
        Serial.print(text);
        reverb.parameterLabel(t, text);
        Serial.print(text);
    }
}

static void apply(int8_t t, uint8_t value)
{
    if (t == CONTROL_BYPASS) reverb.bypass(value >= 64);
    else if (t == CONTROL_CLEAR) {
        if (value >= 64) reverb.plugin().clear();
    } else reverb.setParameter(t, value / 127.0f);
}

static void printMapping(uint8_t cc)
{
    Serial.print("  CC ");
    Serial.print(cc);
    Serial.print(" -> ");
    printTarget(cc_map[cc]);
    Serial.println();
}

static void set(int8_t t, uint8_t value)
{
    apply(t, value);
    printTarget(t);
    Serial.print(" = ");
    printValue(t);
    Serial.println();
}

// Both MIDI drivers call this from their read(), in loop(); channel is 1..16.
static void onControlChange(uint8_t channel, uint8_t control, uint8_t value)
{
    if (midi_channel != 0 && channel != midi_channel) return;
    if (control >= 128) return;
    if (learning) {
        learning = false;
        cc_map[control] = learn_target;
        Serial.print("learned:");
        printMapping(control);
    }
    const int8_t t = cc_map[control];
    if (t == UNMAPPED || !valid(t)) return;
    set(t, value);
}

static void printMap()
{
    Serial.print("CC map, MIDI channel ");
    if (midi_channel) Serial.println(midi_channel);
    else Serial.println("all");
    for (uint8_t cc = 0; cc < 128; cc++) {
        if (cc_map[cc] != UNMAPPED) printMapping(cc);
    }
}

static void printMidiDevice(MIDIDeviceBase& midi)
{
    const uint8_t* product = midi.product();
    Serial.print("  MIDI: ");
    if (!midi) {
        Serial.println("none");
        return;
    }
    Serial.print(product && *product ? reinterpret_cast<const char*>(product) : "a device");
    Serial.print(", ");
    Serial.print(midi.idVendor(), HEX);
    Serial.print(':');
    Serial.println(midi.idProduct(), HEX);
}

static void printStatus()
{
    interface.printStatus(Serial);
    printMidiDevice(midi1);
    printMidiDevice(midi2);
    Serial.print("  audio library: ");
    Serial.print(AudioProcessorUsageMax());
    Serial.print("% cpu, ");
    Serial.print(AudioMemoryUsageMax());
    Serial.print(" blocks, input underruns ");
    Serial.print(in.underruns());
    Serial.print(", dropped ");
    Serial.println(in.dropped());
    Serial.print("  reverb: ");
    Serial.print(reverb.processorUsageMax());
    Serial.println(reverb.bypassed() ? "% cpu, bypassed" : "% cpu");
}

// Reads a control's name from p: a parameter's number, b or e. Says what is
// wrong and returns false if it names nothing the reverb has.
static bool parseTarget(char*& p, int8_t& t)
{
    while (*p == ' ') p++;
    if (*p == 'b') {
        t = CONTROL_BYPASS;
        p++;
    } else if (*p == 'e') {
        t = CONTROL_CLEAR;
        p++;
    } else if (*p >= '0' && *p <= '9') {
        const long n = strtol(p, &p, 10);
        t = static_cast<int8_t>(n < 127 ? n : 127);
    } else {
        Serial.println("name a control: a parameter's number, b or e");
        return false;
    }
    if (!valid(t)) {
        Serial.println("no such parameter; l lists them");
        return false;
    }
    return true;
}

static bool parse7Bit(char*& p, uint8_t& value, const char* what)
{
    const long n = strtol(p, &p, 10);
    if (n < 0 || n > 127) {
        Serial.print(what);
        Serial.println(" is 0 to 127");
        return false;
    }
    value = static_cast<uint8_t>(n);
    return true;
}

static void handleLine(char* s)
{
    while (*s == ' ') s++;
    const char command = *s;
    char* p = s + 1;
    uint8_t cc, value;
    int8_t t;
    if (command == 'l') {
        reverb.printParameters(Serial);
    } else if (command == 'c') {
        printMap();
    } else if (command == 'v') {
        if (!parseTarget(p, t) || !parse7Bit(p, value, "a value")) return;
        set(t, value);
    } else if (command == 'a') {
        if (!parse7Bit(p, cc, "a CC number") || !parseTarget(p, t)) return;
        cc_map[cc] = t;
        printMapping(cc);
    } else if (command == 'x') {
        if (!parse7Bit(p, cc, "a CC number")) return;
        cc_map[cc] = UNMAPPED;
        Serial.print("CC ");
        Serial.print(cc);
        Serial.println(" unmapped");
    } else if (command == 'L') {
        if (!parseTarget(p, t)) return;
        learn_target = t;
        learning = true;
        Serial.print("move a control to map it to ");
        printTarget(t);
        Serial.println();
    } else if (command == 'h') {
        const long channel = strtol(p, &p, 10);
        if (channel < 0 || channel > 16) {
            Serial.println("a MIDI channel is 1 to 16, or 0 for all");
            return;
        }
        midi_channel = static_cast<uint8_t>(channel);
        printMap();
    } else if (command == 's') {
        printStatus();
    } else if (command != 0) {
        Serial.println("commands: l, c, v <t> <value>, a <cc> <t>, x <cc>, L <t>, h <ch>, s");
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

static void showMidi(MIDIDeviceBase& midi, bool& was)
{
    const bool now = midi;
    if (now == was) return;
    was = now;
    if (now) printMidiDevice(midi);
    else Serial.println("  MIDI: a device left");
}

void setup()
{
    Serial.begin(115200);
    AudioMemory(40);
    interface.setSampleRate(44100);
    interface.setChannels(2, 2);

    defaultMap();
    midi1.setHandleControlChange(onControlChange);
    midi2.setHandleControlChange(onControlChange);

    usb.begin();
    in.begin();
    out.begin();
    Serial.println("Cumulus reverb with MIDI: c lists the CC map, l the controls; a/x/L map, unmap and learn");
}

void loop()
{
    usb.Task();

    if (interface.state() != shown) {
        shown = interface.state();
        interface.printStatus(Serial);
        if (shown == USBAudioDevice::State::Streaming) reverb.sampleRate(interface.sampleRate());
    }
    showMidi(midi1, midi_shown[0]);
    showMidi(midi2, midi_shown[1]);

    // Each read() takes one message from the driver's queue and calls the
    // handler for it; drain the queue every pass, so a knob swept fast is
    // never more than a loop behind.
    while (midi1.read()) {
    }
    while (midi2.read()) {
    }

    readSerial();
}

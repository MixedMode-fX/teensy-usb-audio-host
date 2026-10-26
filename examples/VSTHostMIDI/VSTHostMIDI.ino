// The VSTHost example's rack of Airwindows plug-ins, played from a USB MIDI
// controller: each control change number can be mapped to one control of
// one slot (a plug-in parameter, the slot's mix or gain, or its bypass), and
// moving the knob sets it. The serial monitor shows what moved and edits the
// map, by number or by MIDI learn.
//
// Wiring: a powered USB hub into the Teensy's host port, with the audio
// interface and the MIDI controller both on the hub. USBHost_t36 runs the
// hub, the MIDI driver and this library's audio driver side by side on the
// one port; a high-speed interface behind a high-speed hub streams as it
// does on the root port, and a full-speed controller goes through the hub's
// transaction translator. Two MIDI drivers are declared, because an
// interface with MIDI ports of its own (the Scarlett 4i4 has a pair)
// offers them as a MIDI function, which takes one; control changes from
// either are handled alike. Two hub drivers cover a 7-port hub, which is
// two hubs inside.
//
// Boards: a Teensy 4.x, for the reason the VSTHost example gives.
//
// The plug-ins are the VSTHost example's: this sketch includes its
// plugins.h, and its build compiles that example's plugins.cpp (see
// platformio.ini). In the Arduino IDE, copy plugins.h, plugins.cpp and
// airwindows/ from examples/VSTHost into this sketch's folder.
//
// The rack, with its default mapping (general MIDI's sound controllers
// where one fits, the undefined block from CC 20 elsewhere, and the
// general-purpose buttons from CC 80 for bypass):
//
//   slot 0  Capacitor   CC 74 Lowpass, CC 71 Highpass                 CC 80 bypass
//   slot 1  Density     CC 20 Density, CC 21 mix                      CC 81 bypass
//   slot 2  Pressure4   CC 22 Pressure, CC 23 Speed, CC 24 gain       CC 82 bypass
//   slot 3  a wire      CC 25 gain: the dry half of the parallel compression
//   slot 4  Tape        CC 26 Slam, CC 27 Bump                        CC 83 bypass
//   slot 5  Tremolo     CC 76 Speed, CC 1 (the mod wheel) Depth       CC 84 bypass
//
// A CC's 0..127 is the control's 0..1. A bypass is on at 64 and above,
// which is how a latching button sends it.
//
// Serial monitor, one command per line; t names a control of slot n:
// a parameter's number, or m (mix), g (gain) or b (bypass):
//   l                list the slots, their stages and their parameters
//   c                list the CC map
//   a <cc> <n> <t>   map control change cc to slot n's control t
//   x <cc>           unmap it
//   L <n> <t>        learn: the next control change to arrive is mapped there
//   h <ch>           listen on MIDI channel ch (1 to 16), or 0 for all of them
//   s                stream status, MIDI devices and processor use per slot

#include <Audio.h>
#include <USBHost_t36.h>
#include <USBAudioHost.h>
#include <AudioEffectVST.h>
#include "plugins.h"

USBHost usb;
USBHub hub1(usb);
USBHub hub2(usb);
USBAudioDevice interface(usb);
MIDIDevice_BigBuffer midi1(usb);
MIDIDevice_BigBuffer midi2(usb);

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

// What a control change drives: a slot, and a parameter of its plug-in or
// one of the host's own controls of the slot. One per CC number.
static const int8_t UNMAPPED = -1;
static const int8_t CONTROL_MIX = -1;
static const int8_t CONTROL_GAIN = -2;
static const int8_t CONTROL_BYPASS = -3;
struct CCTarget {
    int8_t slot;
    int8_t control;  // a parameter index, or CONTROL_*
};
static CCTarget cc_map[128];

static uint8_t midi_channel = 0;  // 1..16, 0 for every channel
static bool learning = false;
static CCTarget learn_target = {UNMAPPED, 0};

static char line[48];
static size_t line_len = 0;
static USBAudioDevice::State shown = USBAudioDevice::State::Idle;
static bool midi_shown[2] = {false, false};

static void assign(uint8_t cc, int8_t slot, int8_t control)
{
    if (cc < 128) cc_map[cc] = {slot, control};
}

static void defaultMap()
{
    for (CCTarget& t : cc_map) t = {UNMAPPED, 0};
    assign(74, 0, 0);  // Capacitor: Lowpass
    assign(71, 0, 1);  //            Highpass
    assign(80, 0, CONTROL_BYPASS);
    assign(20, 1, 0);  // Density: Density
    assign(21, 1, CONTROL_MIX);
    assign(81, 1, CONTROL_BYPASS);
    assign(22, 2, 0);  // Pressure4: Pressure
    assign(23, 2, 1);  //            Speed
    assign(24, 2, CONTROL_GAIN);
    assign(82, 2, CONTROL_BYPASS);
    assign(25, 3, CONTROL_GAIN);  // the dry wire beside it
    assign(26, 4, 0);  // Tape: Slam
    assign(27, 4, 1);  //       Bump
    assign(83, 4, CONTROL_BYPASS);
    assign(76, 5, 0);  // Tremolo: Speed
    assign(1, 5, 1);   //          Depth
    assign(84, 5, CONTROL_BYPASS);
}

// "Capacitor Lowpass", "Density mix", "wire gain": the target, by name.
static void printTarget(CCTarget t)
{
    VSTEffect* slot = chain.slot(t.slot);
    char text[VSTEffect::TEXT_LEN];
    slot->effectName(text);
    Serial.print(text);
    Serial.print(' ');
    if (t.control == CONTROL_MIX) Serial.print("mix");
    else if (t.control == CONTROL_GAIN) Serial.print("gain");
    else if (t.control == CONTROL_BYPASS) Serial.print("bypass");
    else {
        slot->parameterName(t.control, text);
        Serial.print(text);
    }
}

// Whether t names a control the rack has, so a mapped CC never reaches for
// a slot or a parameter that is not there.
static bool valid(CCTarget t)
{
    VSTEffect* slot = chain.slot(t.slot);
    if (!slot) return false;
    return t.control == CONTROL_MIX || t.control == CONTROL_GAIN || t.control == CONTROL_BYPASS ||
           (t.control >= 0 && t.control < slot->numParams());
}

// The target's value as it is now, as the serial monitor shows it.
static void printValue(CCTarget t)
{
    VSTEffect* slot = chain.slot(t.slot);
    if (t.control == CONTROL_MIX) Serial.print(slot->mix(), 2);
    else if (t.control == CONTROL_GAIN) Serial.print(slot->gain(), 2);
    else if (t.control == CONTROL_BYPASS) Serial.print(slot->bypassed() ? "on" : "off");
    else {
        char text[VSTEffect::TEXT_LEN];
        slot->parameterDisplay(t.control, text);
        Serial.print(text);
        slot->parameterLabel(t.control, text);
        Serial.print(' ');
        Serial.print(text);
    }
}

static void apply(CCTarget t, uint8_t value)
{
    VSTEffect* slot = chain.slot(t.slot);
    const float v = value / 127.0f;
    if (t.control == CONTROL_MIX) slot->mix(v);
    else if (t.control == CONTROL_GAIN) slot->gain(v);
    else if (t.control == CONTROL_BYPASS) slot->bypass(value >= 64);
    else slot->setParameter(t.control, v);
}

static void printMapping(uint8_t cc)
{
    Serial.print("  CC ");
    Serial.print(cc);
    Serial.print(" -> ");
    Serial.print(cc_map[cc].slot);
    Serial.print(": ");
    printTarget(cc_map[cc]);
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
    const CCTarget t = cc_map[control];
    if (t.slot == UNMAPPED || !valid(t)) return;
    apply(t, value);
    printTarget(t);
    Serial.print(" = ");
    printValue(t);
    Serial.println();
}

static void printMap()
{
    Serial.print("CC map, MIDI channel ");
    if (midi_channel) Serial.println(midi_channel);
    else Serial.println("all");
    for (uint8_t cc = 0; cc < 128; cc++) {
        if (cc_map[cc].slot != UNMAPPED) printMapping(cc);
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

// Reads "<n> <t>" from p into a target: a slot number, then a parameter's
// number or m, g or b. Says what is wrong and returns false if it names
// nothing the rack has.
static bool parseTarget(char* p, CCTarget& t)
{
    const long n = strtol(p, &p, 10);
    while (*p == ' ') p++;
    long control;
    if (*p == 'm') control = CONTROL_MIX;
    else if (*p == 'g') control = CONTROL_GAIN;
    else if (*p == 'b') control = CONTROL_BYPASS;
    else if (*p >= '0' && *p <= '9') control = strtol(p, &p, 10);
    else {
        Serial.println("name a control: a parameter's number, m, g or b");
        return false;
    }
    if (n < 0 || n >= chain.size()) {
        Serial.println("no such slot; l lists them");
        return false;
    }
    t = {static_cast<int8_t>(n), static_cast<int8_t>(control < 127 ? control : 127)};
    if (!valid(t)) {
        Serial.println("no such parameter; l lists them");
        return false;
    }
    return true;
}

static bool parseCC(char*& p, uint8_t& cc)
{
    const long value = strtol(p, &p, 10);
    if (value < 0 || value > 127) {
        Serial.println("a CC number is 0 to 127");
        return false;
    }
    cc = static_cast<uint8_t>(value);
    return true;
}

static void handleLine(char* s)
{
    while (*s == ' ') s++;
    const char command = *s;
    char* p = s + 1;
    uint8_t cc;
    CCTarget t;
    if (command == 'l') {
        chain.printParameters(Serial);
    } else if (command == 'c') {
        printMap();
    } else if (command == 'a') {
        if (!parseCC(p, cc) || !parseTarget(p, t)) return;
        cc_map[cc] = t;
        printMapping(cc);
    } else if (command == 'x') {
        if (!parseCC(p, cc)) return;
        cc_map[cc] = {UNMAPPED, 0};
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
        Serial.println("commands: l, c, a <cc> <n> <t>, x <cc>, L <n> <t>, h <ch>, s");
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

    // The VSTHost example's rack, in signal order.
    chain.add(capacitor);                                   // 0
    VSTEffect* saturation = chain.add(density);             // 1
    saturation->setParameter(0, 0.4f);
    saturation->mix(0.5f);
    VSTEffect* compressed = chain.add(pressure);            // 2
    compressed->setParameter(0, 0.5f);
    compressed->gain(0.7f);
    chain.addParallel()->gain(0.5f);                        // 3
    chain.add(tape);                                        // 4
    chain.add(tremolo)->setParameter(1, 0.0f);              // 5  depth off until the mod wheel moves

    defaultMap();
    midi1.setHandleControlChange(onControlChange);
    midi2.setHandleControlChange(onControlChange);

    usb.begin();
    in.begin();
    out.begin();
    Serial.println("VST host with MIDI: c lists the CC map, l the rack; a/x/L map, unmap and learn");
}

void loop()
{
    usb.Task();

    if (interface.state() != shown) {
        shown = interface.state();
        interface.printStatus(Serial);
        if (shown == USBAudioDevice::State::Streaming) chain.sampleRate(interface.sampleRate());
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

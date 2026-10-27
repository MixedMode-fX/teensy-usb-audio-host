// FerricTape: a worn tape machine with a slap-back head between a
// class-compliant USB audio interface's first two inputs and outputs, with
// a send to Clouds' reverb through a low cut, played from a USB MIDI
// controller. Each of its controls answers a control change number, by a
// default map or by MIDI learn, and the serial monitor shows what moved.
//
//   in -> tape -+-------------------------------+-> mixer -> volume -> out
//               +-> low cut -> Cumulus reverb --+
//
// The tape reaches the output whole; the reverb hears it without its low
// end, so the tail stays clear of the bass, and returns fully wet at the
// Verb level. Volume is the last thing before the outputs.
//
// Wiring: a powered USB hub into the Teensy's host port, with the audio
// interface and the MIDI controller both on the hub, as for the VSTHostMIDI
// example, which says why there are two MIDI and two hub drivers. Without a
// controller the serial monitor sets the controls.
//
// Boards: a Teensy 4.x or 3.6. Both plug-ins are single-precision float;
// the tape carries 72 KB of delay memory and the reverb 32 KB. On a 3.6
// this build gives the slap 16 KB, for 93 ms at most (platformio.ini).
//
// The tape is ferric_tape.h/.cpp next to this sketch; ferric_tape.h says
// what its controls do and in what order the signal meets them. The reverb
// is the CumulusReverb example's: this sketch includes its cumulus_reverb.h,
// and its build compiles that example's cumulus_reverb.cpp (see
// platformio.ini). In the Arduino IDE, copy cumulus_reverb.h,
// cumulus_reverb.cpp, mutable_reverb.h, clouds/ and stmlib/ from
// examples/CumulusReverb into this sketch's folder.
//
// The default map, general MIDI's sound and effect controllers where one
// fits, the undefined blocks from CC 20 and CC 85 elsewhere, and the
// general-purpose buttons from CC 80:
//
//   CC 20  Drive               CC 75  verb Time  (sound controller 6: decay)
//   CC 21  Comp                CC 30  verb Diffuse
//   CC 22  Bits                CC 31  verb Tone
//   CC 74  Tone (brightness)   CC 85  verb Input
//   CC 23  Wow                 CC 87  verb Clock
//   CC 24  Flutter             CC 88  verb Warp
//   CC 25  Dropout             CC 89  verb Speed
//   CC 93  Chorus (effects 3)  CC 90  verb Glitch
//   CC 26  Time                CC 86  Low cut, 40 Hz to 2 kHz
//   CC 27  Feedback            CC 91  Verb, its level (effects 1: reverb)
//   CC 28  Slap                CC 7   Volume (channel volume), -inf to +6 dB, 0 at 101
//   CC 29  Mix
//   CC 80  bypass the tape, on at 64 and above, as a latching button sends it
//   CC 81  erase the tape and the echoes, when pressed (64 and above)
//   CC 82  clear the reverb's tail, when pressed
//
// A CC's 0..127 is the control's 0..1.
//
// Serial monitor, one command per line; t names a control: its number
// (0 Drive .. 22 Volume, as l lists them), b (bypass), e (erase) or r (clear
// the reverb):
//   l              the controls and their values
//   c              the CC map
//   v <t> <0..127> set a control as a control change would
//   a <cc> <t>     map control change cc to t
//   x <cc>         unmap it
//   L <t>          learn: the next control change to arrive is mapped to t
//   h <ch>         listen on MIDI channel ch (1 to 16), or 0 for all of them
//   s              stream status, MIDI devices and processor use
//
// panel/index.html is a front panel for these commands in the browser, over
// Web Serial (Chrome or Edge): open it, or run `make panel`, and plug in.

#include <Audio.h>
#include <USBHost_t36.h>
#include <USBAudioHost.h>
#include <AudioEffectVST.h>
#include "ferric_tape.h"
#include "cumulus_reverb.h"

USBHost usb;
USBHub hub1(usb);
USBHub hub2(usb);
USBAudioDevice interface(usb);
MIDIDevice_BigBuffer midi1(usb);
MIDIDevice_BigBuffer midi2(usb);

AudioInputUSBHost in(interface, 2);
AudioEffectVST<FerricTape> tape;
AudioFilterBiquad lowcutL;
AudioFilterBiquad lowcutR;
AudioEffectVST<CumulusReverb> reverb;
AudioMixer4 mixL;
AudioMixer4 mixR;
AudioAmplifier volumeL;
AudioAmplifier volumeR;
AudioOutputUSBHost out(interface, 2);

AudioConnection c1(in, 0, tape, 0);
AudioConnection c2(in, 1, tape, 1);
AudioConnection c3(tape, 0, mixL, 0);
AudioConnection c4(tape, 1, mixR, 0);
AudioConnection c5(tape, 0, lowcutL, 0);
AudioConnection c6(tape, 1, lowcutR, 0);
AudioConnection c7(lowcutL, 0, reverb, 0);
AudioConnection c8(lowcutR, 0, reverb, 1);
AudioConnection c9(reverb, 0, mixL, 1);
AudioConnection c10(reverb, 1, mixR, 1);
AudioConnection c11(mixL, 0, volumeL, 0);
AudioConnection c12(mixR, 0, volumeR, 0);
AudioConnection c13(volumeL, 0, out, 0);
AudioConnection c14(volumeR, 0, out, 1);

// What a control change drives: the tape's parameters, numbered as the
// plug-in numbers them, then the reverb's but its Amount (the send runs it
// fully wet), then the low cut, the reverb's level and the volume; or one
// of the buttons.
static const int8_t VERB_FIRST = FerricTape::NUM_PARAMS;
static const int8_t VERB_COUNT = CumulusReverb::NUM_PARAMS - 1;
static const int8_t CONTROL_LOW_CUT = VERB_FIRST + VERB_COUNT;
static const int8_t CONTROL_VERB = CONTROL_LOW_CUT + 1;
static const int8_t CONTROL_VOLUME = CONTROL_VERB + 1;
static const int8_t NUM_CONTROLS = CONTROL_VOLUME + 1;
static const int8_t UNMAPPED = -1;
static const int8_t CONTROL_BYPASS = -2;
static const int8_t CONTROL_ERASE = -3;
static const int8_t CONTROL_CLEAR = -4;
static int8_t cc_map[128];

// The low cut's, the reverb level's and the volume's 0..1, as a CC sets
// them. The volume's gain is a cube, 0 dB at CC 101 and +6 dB at 127.
static float low_cut = 0.35f;
static float verb_level = 0.35f;
static float volume = 101.0f / 127.0f;

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
    cc_map[20] = FerricTape::PARAM_DRIVE;
    cc_map[21] = FerricTape::PARAM_COMP;
    cc_map[22] = FerricTape::PARAM_BITS;
    cc_map[74] = FerricTape::PARAM_TONE;
    cc_map[23] = FerricTape::PARAM_WOW;
    cc_map[24] = FerricTape::PARAM_FLUTTER;
    cc_map[25] = FerricTape::PARAM_DROPOUT;
    cc_map[93] = FerricTape::PARAM_CHORUS;
    cc_map[26] = FerricTape::PARAM_TIME;
    cc_map[27] = FerricTape::PARAM_FEEDBACK;
    cc_map[28] = FerricTape::PARAM_SLAP;
    cc_map[29] = FerricTape::PARAM_MIX;
    cc_map[75] = VERB_FIRST + CumulusReverb::PARAM_TIME - 1;
    cc_map[30] = VERB_FIRST + CumulusReverb::PARAM_DIFFUSION - 1;
    cc_map[31] = VERB_FIRST + CumulusReverb::PARAM_TONE - 1;
    cc_map[85] = VERB_FIRST + CumulusReverb::PARAM_INPUT - 1;
    cc_map[87] = VERB_FIRST + CumulusReverb::PARAM_CLOCK - 1;
    cc_map[88] = VERB_FIRST + CumulusReverb::PARAM_WARP - 1;
    cc_map[89] = VERB_FIRST + CumulusReverb::PARAM_SPEED - 1;
    cc_map[90] = VERB_FIRST + CumulusReverb::PARAM_GLITCH - 1;
    cc_map[86] = CONTROL_LOW_CUT;
    cc_map[91] = CONTROL_VERB;
    cc_map[7] = CONTROL_VOLUME;
    cc_map[80] = CONTROL_BYPASS;
    cc_map[81] = CONTROL_ERASE;
    cc_map[82] = CONTROL_CLEAR;
}

// A reverb control's parameter of the plug-in.
static int verbParameter(int8_t t)
{
    return t - VERB_FIRST + 1;
}

static bool valid(int8_t t)
{
    return t == CONTROL_BYPASS || t == CONTROL_ERASE || t == CONTROL_CLEAR || (t >= 0 && t < NUM_CONTROLS);
}

static float lowCutHz()
{
    return 40.0f * powf(50.0f, low_cut);
}

static void applyLowCut()
{
    // Two Butterworth stages: 24 dB an octave.
    const float hz = lowCutHz();
    lowcutL.setHighpass(0, hz, 0.7071f);
    lowcutL.setHighpass(1, hz, 0.7071f);
    lowcutR.setHighpass(0, hz, 0.7071f);
    lowcutR.setHighpass(1, hz, 0.7071f);
}

static void applyVerb()
{
    mixL.gain(1, verb_level);
    mixR.gain(1, verb_level);
}

static float volumeGain()
{
    const float v = volume * (127.0f / 101.0f);
    return v * v * v;
}

static void applyVolume()
{
    volumeL.gain(volumeGain());
    volumeR.gain(volumeGain());
}

static void printTarget(int8_t t)
{
    char text[VSTEffect::TEXT_LEN];
    if (t == CONTROL_BYPASS) {
        Serial.print("bypass");
    } else if (t == CONTROL_ERASE) {
        Serial.print("erase");
    } else if (t == CONTROL_CLEAR) {
        Serial.print("clear reverb");
    } else if (t == CONTROL_LOW_CUT) {
        Serial.print("Low cut");
    } else if (t == CONTROL_VERB) {
        Serial.print("Verb");
    } else if (t == CONTROL_VOLUME) {
        Serial.print("Volume");
    } else if (t >= VERB_FIRST) {
        reverb.parameterName(verbParameter(t), text);
        Serial.print("verb ");
        Serial.print(text);
    } else {
        tape.parameterName(t, text);
        Serial.print(text);
    }
}

static void printValue(int8_t t)
{
    char text[VSTEffect::TEXT_LEN];
    if (t == CONTROL_BYPASS) {
        Serial.print(tape.bypassed() ? "on" : "off");
    } else if (t == CONTROL_ERASE || t == CONTROL_CLEAR) {
        Serial.print("done");
    } else if (t == CONTROL_LOW_CUT) {
        Serial.print(static_cast<int>(lowCutHz() + 0.5f));
        Serial.print("Hz");
    } else if (t == CONTROL_VERB) {
        Serial.print(static_cast<int>(verb_level * 100.0f + 0.5f));
        Serial.print("%");
    } else if (t == CONTROL_VOLUME) {
        if (volume == 0.0f) {
            Serial.print("-inf dB");
        } else {
            Serial.print(20.0f * log10f(volumeGain()), 1);
            Serial.print(" dB");
        }
    } else {
        VSTEffect& effect = t >= VERB_FIRST ? static_cast<VSTEffect&>(reverb) : static_cast<VSTEffect&>(tape);
        const int index = t >= VERB_FIRST ? verbParameter(t) : t;
        effect.parameterDisplay(index, text);
        Serial.print(text);
        effect.parameterLabel(index, text);
        Serial.print(text);
    }
}

static void apply(int8_t t, uint8_t value)
{
    if (t == CONTROL_BYPASS) {
        tape.bypass(value >= 64);
    } else if (t == CONTROL_ERASE) {
        if (value >= 64) tape.plugin().clear();
    } else if (t == CONTROL_CLEAR) {
        if (value >= 64) reverb.plugin().clear();
    } else if (t == CONTROL_LOW_CUT) {
        low_cut = value / 127.0f;
        applyLowCut();
    } else if (t == CONTROL_VERB) {
        verb_level = value / 127.0f;
        applyVerb();
    } else if (t == CONTROL_VOLUME) {
        volume = value / 127.0f;
        applyVolume();
    } else if (t >= VERB_FIRST) {
        reverb.setParameter(verbParameter(t), value / 127.0f);
    } else {
        tape.setParameter(t, value / 127.0f);
    }
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

static void printControls()
{
    for (int8_t t = 0; t < NUM_CONTROLS; t++) {
        Serial.print("  ");
        Serial.print(t);
        Serial.print(" ");
        printTarget(t);
        Serial.print(" = ");
        printValue(t);
        Serial.println();
    }
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
    Serial.print("  tape: ");
    Serial.print(tape.processorUsageMax());
    Serial.println(tape.bypassed() ? "% cpu, bypassed" : "% cpu");
    Serial.print("  reverb: ");
    Serial.print(reverb.processorUsageMax());
    Serial.println("% cpu");
}

// Reads a control's name from p: its number, b, e or r. Says what is wrong
// and returns false if it names no control.
static bool parseTarget(char*& p, int8_t& t)
{
    while (*p == ' ') p++;
    if (*p == 'b') {
        t = CONTROL_BYPASS;
        p++;
    } else if (*p == 'e') {
        t = CONTROL_ERASE;
        p++;
    } else if (*p == 'r') {
        t = CONTROL_CLEAR;
        p++;
    } else if (*p >= '0' && *p <= '9') {
        const long n = strtol(p, &p, 10);
        t = static_cast<int8_t>(n < 127 ? n : 127);
    } else {
        Serial.println("name a control: its number, b, e or r");
        return false;
    }
    if (!valid(t)) {
        Serial.println("no such control; l lists them");
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
        printControls();
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

    // The send: the reverb all wet, the tape straight through beside it.
    reverb.setParameter(CumulusReverb::PARAM_AMOUNT, 1.0f);
    mixL.gain(0, 1.0f);
    mixR.gain(0, 1.0f);
    applyVerb();
    applyLowCut();
    applyVolume();

    defaultMap();
    midi1.setHandleControlChange(onControlChange);
    midi2.setHandleControlChange(onControlChange);

    usb.begin();
    in.begin();
    out.begin();
    Serial.println("Ferric tape into Cumulus, with MIDI: c lists the CC map, l the controls; a/x/L map, unmap and learn");
}

void loop()
{
    usb.Task();

    if (interface.state() != shown) {
        shown = interface.state();
        interface.printStatus(Serial);
        if (shown == USBAudioDevice::State::Streaming) {
            tape.sampleRate(interface.sampleRate());
            reverb.sampleRate(interface.sampleRate());
        }
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

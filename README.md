# Teensy USB Audio Host

A USB Audio Class host for the Teensy 4.1 and 3.6 host port: plug in a
class-compliant audio interface (the target is a Focusrite Scarlett 4i4 4th
Gen) and its inputs and outputs become Teensy Audio Library objects, so the
Teensy can record it to an SD card or run effects between its inputs and
outputs in real time.

It is a driver for `USBHost_t36`, the host stack in the Teensy core, and sits
next to that stack's other drivers (hubs, MIDI, HID) on the same port:

```cpp
#include <Audio.h>
#include <USBHost_t36.h>
#include <USBAudioHost.h>

USBHost usb;
USBHub hub(usb);
USBAudioDevice interface(usb);
AudioInputUSBHost  in(interface, 2);   // the interface's first two inputs
AudioOutputUSBHost out(interface, 2);  // its first two outputs

void setup() {
    AudioMemory(40);
    interface.setSampleRate(44100);
    usb.begin();
    in.begin();
    out.begin();
}
void loop() { usb.Task(); }
```

`examples/Passthrough` is that with a filter in the path; `examples/RecordToSD`
records the inputs to a WAV file while monitoring them. The other examples
put effects between the inputs and the outputs, with the serial monitor as
the front panel (see [Examples](#examples)), and `examples/VSTHost` runs
third-party VST-style plug-ins there (see [Hosting VST-style effect
plug-ins](#hosting-vst-style-effect-plug-ins)), and `examples/VSTHostMIDI` maps a MIDI
controller's knobs onto them.

**Status: compiles for both boards, untested on hardware.** Everything the
host can do without a device (descriptor parsing, rate pacing, feedback
decoding, the sample rings) is unit-tested natively. The isochronous engine and
the control sequence are written against the EHCI and USB Audio Class
specifications and USBHost_t36's source, and have not yet seen a Scarlett.
`USBAudioDevice::printStatus()` prints what was negotiated and the counters to
look at first.

## Using it from PlatformIO

```ini
[env:teensy41]
platform = teensy@6.0.0
board = teensy41
framework = arduino
lib_deps = https://github.com/mixedmode-fx/teensy-usb-audio-host.git
```

`USBHost_t36` and the Audio Library come with the Teensy platform; the
platform version pins them. The interface must be self-powered or on a powered
hub: the Teensy's host port cannot supply what a bus-powered interface draws.

## What the driver does

`USBAudioDevice` claims the AudioControl interface of the first UAC1 or UAC2
function offered to it and reads the function's descriptors
(`src/usb_audio_host/audio_function.h`). Per direction it picks one alternate
setting: PCM, at least 16 bits, packets that fit its buffers, the channel count
closest to what `setChannels()` asked for (more rather than fewer), then the
highest resolution. It then runs the control sequence from the USB interrupt:
UAC2 asks the clock source for its rate range, picks the wanted rate or the
nearest one offered, selects the clock and sets the rate, and sets both
alternate settings; UAC1 sets the alternate settings and the rate on each
endpoint. Then both directions stream.

Samples cross to the sketch through two rings (`sample_ring.h`) as `int32_t`,
left-justified, interleaved by channel, so a 24-bit interface loses nothing on
the way in. The Audio Library objects take the top 16 bits.

**Clocking.** The interface is the clock. `AudioInputUSBHost` claims the Audio
Library's update responsibility and runs the graph each time a block of frames
has arrived, so nothing is resampled and the output ring stays level. The
output packets are paced (`pacer.h`) at the nominal rate, corrected by the
interface's explicit feedback endpoint when it has one, or by mirroring the
input packets when the sink is asynchronous and fed back implicitly, which is
how UAC2 interfaces like the Scarlett do it. A sketch that also has an I2S or
other hardware output has two clocks; whichever claimed the update first
drives, and the other side drifts.

**Isochronous transfers.** USBHost_t36 has none, so `iso_engine.h` adds them
to the periodic frame list USBHost_t36 built: an iTD per frame for a high-speed
endpoint, an siTD per frame for a full-speed one, kept
`USB_AUDIO_HOST_QUEUE_FRAMES` frames ahead of the controller and re-armed from
an interrupt handler chained in front of USBHost_t36's own. Two things about
USBHost_t36 are relied on and checked rather than assumed: the frame list base
and size are read from the controller's registers, and its code that walks a
frame's list casts every node to its `Pipe_t` and reads one field to decide
where a new queue head goes, so each descriptor here carries that field at the
same offset (a `static_assert` holds the offsets together) with a value that
makes the walker step over it. Descriptors and packet buffers live inside the
`USBAudioDevice` object in ordinary RAM, which the Teensy 4 does not cache;
do not put the object in `DMAMEM` or `EXTMEM`.

## Examples

Every example takes the interface's first two inputs, does something to them
and sends the result to its first two outputs, and prints the stream status
when it changes. Keys on the serial monitor turn the knobs; each sketch
lists its keys at the top and `s` prints the status everywhere.

| Example       | What it does                                                                                            |
|---------------|---------------------------------------------------------------------------------------------------------|
| `Passthrough` | Straight through, with a state-variable low-pass to sweep.                                              |
| `RecordToSD`  | Records the inputs to a WAV file on the SD card while monitoring them.                                  |
| `Reverb`      | The Audio Library's stereo Freeverb, with room size, damping and wet mix.                               |
| `StereoDelay` | A ping-pong delay: each side's repeats feed the other side's line. Time, feedback and mix.              |
| `Overdrive`   | A pedal chain per channel: drive, a soft-clipping waveshaper, a tone low-pass, a bit crusher, level.    |
| `Modulation`  | The Audio Library's chorus and flanger side by side, with a mixer choosing which one reaches the output. |
| `VSTHost`     | Five Airwindows plug-ins in one rack object, in series and in parallel, from their unmodified sources.      |
| `VSTHostMIDI` | The `VSTHost` rack played from a USB MIDI controller on a hub beside the interface: CCs mapped to plug-in parameters, mix, gain and bypass, with MIDI learn. |

They all ask the interface for 44.1 kHz because the Audio Library's effects
assume it; the graph runs at whatever rate the interface ends up streaming,
and an effect's time constants scale with the difference. The delay example
sizes `AudioMemory()` for its longest time, which is where the delay lines
live; the rest need 40 blocks.

## Hosting VST-style effect plug-ins

`AudioEffectVST.h` runs effects written to the VST 2.x plug-in interface
as Audio Library objects. A chain of them is one object:

```cpp
#include <AudioEffectVST.h>
#include "plugins.h"                                 // the plug-ins the sketch carries

VSTPlugin<airwindows::capacitor::Capacitor> capacitor;
VSTPlugin<airwindows::tape::Tape> tape;
AudioEffectVSTChain chain;
AudioConnection c1(in, 0, chain, 0);
AudioConnection c2(in, 1, chain, 1);
AudioConnection c3(chain, 0, out, 0);
AudioConnection c4(chain, 1, out, 1);

void setup() {
    chain.add(capacitor);                            // stage 0
    chain.add(pressure)->gain(0.7f);                 // stage 1: the compressed signal...
    chain.addParallel()->gain(0.5f);                 //   ...summed with the dry one
    chain.add(tape)->mix(0.5f);                      // stage 2, half wet
    chain.slot(0)->setParameter(0, 0.5f);            // 0..1, as VST parameters are
    chain.slot(1)->bypass(true);
    chain.printParameters(Serial);                   // names, values, displays
    ...
}
```

The chain is a rack of stages. `add()` starts a stage after the last;
`addParallel()` joins the last stage, and every slot in a stage reads the
stage's input and is summed into its output at the slot's `gain()`. A slot
added with no plug-in is a wire, which is how the dry signal joins a sum.
Each slot's `mix()` blends the plug-in's output with its input, whether or
not the plug-in has a dry/wet of its own. The chain converts the Audio
Library's 16-bit blocks to float once, runs every stage on the float
buffers, and converts back once, so a rack of plug-ins costs one round
trip through 16 bits rather than one per plug-in, and one update rather
than one per object. It measures each slot's processor use with the cycle
counter (`processorUsageMax(n)`).

A single plug-in that sits between other Audio Library objects is
`AudioEffectVST<P>`, which owns its plug-in and has the same controls
(`filter.setParameter(0, 0.5f)`, `filter.mix(0.5f)`, `filter.bypass(true)`).
The controls of either are `VSTEffect` (`src/vst_host/effect.h`), which
the chain's slots are, and the routing is `VSTRack` (`src/vst_host/rack.h`);
both are framework-free and covered by the native tests.

A plug-in binary from a computer cannot be loaded on a microcontroller:
there is no operating system, no dynamic loader and the wrong processor.
What runs is a plug-in's *source*, compiled into the sketch, which is how
every embedded plug-in host works. `src/audioeffectx.h` stands in for the
SDK header the sources include: the `AudioEffect`/`AudioEffectX` classes
with the constants and string helpers, no host and no Steinberg code. It
was written against the open-source [Airwindows](https://github.com/airwindows/airwindows)
catalogue (hundreds of effects by Chris Johnson, MIT), whose plug-ins
compile against it unmodified; `examples/VSTHost/airwindows/` carries five
of them and `examples/VSTHost/plugins.h` shows how a sketch lists its
plug-ins, each in a namespace of its own, since every Airwindows header
names its parameters `kParamA..` at file scope. To add one, copy its three
files next to the others and add a namespace block.

The float buffers are what a plug-in's `processReplacing()` wants; the way
back clips at full scale. Stereo in and out is what the host carries, which
is what every Airwindows plug-in is. Parameters are set from `loop()` and
read by the plug-in at the start of each block, so no locking is involved.
Every plug-in is told the Audio Library's nominal rate; tell it the
interface's actual one with `sampleRate()` once streaming, as the example
does. A native test suite (`test/test_vst_host`) compiles the example's
plug-ins on the host machine and runs them, so the interface stand-in is
checked against the real code without a Teensy.

The sources need two accommodations, both in `audioeffectx.h` and explained
there: it includes the standard headers a plug-in includes after it, so that
the plug-in's sources can be wrapped in a namespace; and it defines the
`throw` keyword away when exceptions are off (every Teensy build), because
the Airwindows `setParameter()` ends in `default: throw;`, which GCC refuses
outright without `-fexceptions`.

Airwindows is double-precision arithmetic throughout. A Teensy 4.x has a
double-precision FPU and runs the example's five plug-ins with room to
spare; a Teensy 3.6 does not, and one plug-in exceeds its processor. The
reverbs in the catalogue hold their delay lines as double arrays of a
megabyte or so and need a Teensy 4.1 with PSRAM, with the host object placed
in `EXTMEM`; that is untested.

## Sizing

`src/usb_audio_host/config.h` holds the constants and each can be overridden
from the build: the channel width of the rings, their depth, the largest
packet, the queue depth. The defaults suit a 4i4 at any rate on a Teensy 4.1.
On a Teensy 3.6, which has a quarter of the RAM, halve
`USB_AUDIO_HOST_MAX_CHANNELS` and `USB_AUDIO_HOST_RING_FRAMES` for a stereo
application.

## Not done

- Volume, mute and the other feature-unit controls; the interface's own
  mixer. Set them from a computer first.
- An interface offering more channels than `USB_AUDIO_HOST_MAX_CHANNELS` in
  every alternate setting is not claimed: raise the constant.
- Both directions always stream when the interface has both.
- The full-speed (siTD) path is the least certain part: full-speed interfaces
  on the root port go through the controller's embedded transaction
  translator, and the split schedule follows what Linux does.
- The VST host carries stereo plug-ins only, has no presets (`getChunk`/
  `setChunk` are not called) and runs the float path, never
  `processDoubleReplacing`.

## Build and test

```
make setup     # install PlatformIO into .venv/ and pre-fetch the toolchains
make checks    # the commit gate: native tests and every firmware build. Silence is the pass
make test      # the native unit tests, no hardware
make build     # every example, for the Teensy 4.1 and 3.6
make size      # memory usage on a Teensy 4.1
```

`scripts/checks.sh` is the one list of checks; the Makefile, CI and the
Claude commit hook all run it. Sources build with `-Wall -Wextra -Weffc++
-Wshadow -Werror`; the core and its libraries are passed as `-isystem`
(`scripts/project_warnings.py`) so `-Werror` judges this code only. The
example sketches compile as project sources too (`scripts/example_ino.py`),
with the sketch's own directory on the include path as the Arduino IDE has
it. The third-party plug-in sources under `examples/VSTHost/airwindows/`
are the exception: `plugins.h` is a GCC system header, so they are judged by
their own standards and not by these flags.

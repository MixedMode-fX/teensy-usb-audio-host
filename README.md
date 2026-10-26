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
records the inputs to a WAV file while monitoring them.

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

## Build and test

```
make setup     # install PlatformIO into .venv/ and pre-fetch the toolchains
make checks    # the commit gate: native tests and every firmware build. Silence is the pass
make test      # the native unit tests, no hardware
make build     # the examples, for the Teensy 4.1 and 3.6
make size      # memory usage on a Teensy 4.1
```

`scripts/checks.sh` is the one list of checks; the Makefile, CI and the
Claude commit hook all run it. Sources build with `-Wall -Wextra -Weffc++
-Wshadow -Werror`; the core and its libraries are passed as `-isystem`
(`scripts/project_warnings.py`) so `-Werror` judges this code only. The
example sketches compile as project sources too (`scripts/example_ino.py`).

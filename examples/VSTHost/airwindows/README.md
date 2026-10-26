# Airwindows plug-ins

The files under this directory are the unmodified sources of five of the
Airwindows plug-ins by Chris Johnson, taken from
`plugins/LinuxVST/src/` of https://github.com/airwindows/airwindows at
commit `dfa54018e788fd67b8e57ae6c8012a05f1f086a6`, under the MIT license in
`LICENSE`. The whole catalogue (hundreds of effects, all written to the same
interface) is there; the host's `audioeffectx.h` was written against it.

| Plug-in   | What it is                                               |
|-----------|----------------------------------------------------------|
| Capacitor | Low-pass and high-pass filter with a dry/wet control     |
| Density   | Saturation, from a slight thickening to full overdrive   |
| Pressure4 | A compressor                                             |
| Tape      | Tape saturation and head bump                            |
| Tremolo   | Tremolo with a tube-like curve                           |

`plugins.h`, one directory up, is where they are compiled in.

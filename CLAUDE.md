# CLAUDE.md

A USB Audio Class host driver for `USBHost_t36` on the Teensy 4.1 and 3.6,
shipped as an Arduino/PlatformIO library with Audio Library objects on top.
`README.md` is the reference for what the driver does and how it is clocked.

**Nothing here has run on hardware yet.** No session has a Teensy or an
interface attached, so a change to the isochronous engine or the control
sequence can be compiled but not proven. Say so in the pull request rather
than implying it was tried.

## Where things live

- `src/usb_audio_host/` — the driver. Two halves, and the line between them is
  the one that matters:
  - framework-free: `audio_function`, `pacer`, `sample_ring`, `uac`. Only
    `<stdint.h>`-level includes, built and tested natively by `[env:native]`.
  - Teensy-bound: `iso_engine`, `usb_audio_device`, `audio_nodes`. These are
    what `Arduino.h`, `USBHost_t36.h` and `AudioStream.h` may appear in.
- `test/` — the native Unity suites, one directory per suite.
- `examples/` — the sketches, built as firmware for both boards by
  `scripts/example_ino.py`.
- `scripts/` — provisioning and the checks, reached by path from the hooks,
  from CI and from `make`.

## Workflow

**Before any check, wait for the environment**: `bash scripts/await_ready.sh`.
A container is cloned fresh for every web and mobile session with no `.venv/`
and no Teensy toolchain. It returns instantly when the container is warm. Do
not diagnose `pio: not found`, or a build stalled on a platform download,
before it has returned.

**The commit gate**: `bash scripts/checks.sh native` — the native unit tests.
Silence is the pass. The Claude `PreToolUse` hook runs it before every
`git commit` that touches code.

**The pre-PR gate**: `bash scripts/checks.sh` — the native tests and every
example built for the Teensy 4.1 and 3.6. The firmware builds are not in the
commit gate because they take minutes cold; they are the only thing that says
the Teensy-bound half still compiles under `-Werror`, and CI runs them on
every push.

**A pull request closes its issues by name, one keyword each.** `Closes #1,
#2` links the first and silently drops the rest; a bare `#1` links without
ever closing. An issue the branch only advances gets `Part of #1`, so it
survives the merge.

Most sessions here are fired from a phone and read back as a pull request. A
stated bug, a scoped issue, a change already described — build it and push it.
Ask first only where the shape of the library is a real choice: the public
API of `USBAudioDevice` and the Audio Library objects, the sample format in
the rings, or a constant in `config.h` that changes what a sketch must set.

## Code

- **A framework-free file never includes a Teensy header.** It compiles fine
  on the board and breaks only `[env:native]`, which is the half of the
  driver anyone can test. Move logic that needs testing into that half rather
  than mocking the core.
- **Warnings are errors, for this code only.** Builds are `-Wall -Wextra
  -Weffc++ -Wshadow -Werror`; `scripts/project_warnings.py` passes the core
  as `-isystem`. Fix the warning — do not add a pragma or relax the flags.
- **Sizing constants live in `config.h`** and are overridable from the build.
  Use the names, never the literals.
- **The descriptors' `Pipe_t` offset is load-bearing.** `USBHost_t36` walks
  the periodic list as `Pipe_t`s; the `static_assert` in `iso_engine` is what
  holds that together. Never weaken it to make a build pass.
- Every dependency in `platformio.ini` is pinned. The Teensy platform version
  is what pins `USBHost_t36` and the Audio Library; bump it deliberately.
- `.dev/` is the only runtime directory, and it is gitignored: logs, the
  prepared marker. Nothing at the repo root.

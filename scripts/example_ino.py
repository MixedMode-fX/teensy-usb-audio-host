# PlatformIO pre-script: compile an Arduino example sketch as project source.
#
# PlatformIO only converts .ino files that sit directly in src_dir, and this
# repository's src_dir is its root so the library's own sources compile under
# the strict flags. Each example environment names its sketch directory in
# `custom_example`; this script writes the sketch to .pio/examples/<name>/ as
# a .cpp with the Arduino header in front, which build_src_filter then picks
# up. The sketches define every function before its first use, so no
# prototypes need generating. The sketch's directory goes on the include
# path, as the Arduino IDE has it, so a sketch finds the headers next to it
# (the VSTHost example's plugins.h) from its copy.
import os

Import("env")  # noqa: F821  (provided by PlatformIO)

example = env.GetProjectOption("custom_example", "")  # noqa: F821
if example:
    project_dir = env["PROJECT_DIR"]  # noqa: F821
    name = os.path.basename(example.rstrip("/"))
    env.Append(CPPPATH=[os.path.join(project_dir, example)])  # noqa: F821
    sketch = os.path.join(project_dir, example, name + ".ino")
    out_dir = os.path.join(project_dir, ".pio", "examples", name)
    os.makedirs(out_dir, exist_ok=True)
    out = os.path.join(out_dir, name + ".cpp")
    with open(sketch) as f:
        content = '#include <Arduino.h>\n#line 1 "%s"\n%s' % (sketch, f.read())
    current = open(out).read() if os.path.exists(out) else None
    if current != content:
        with open(out, "w") as f:
            f.write(content)

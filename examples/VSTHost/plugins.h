// The Airwindows plug-ins this sketch carries, each in a namespace of its
// own. Every plug-in's header names its parameters kParamA.. and its factory
// createEffectInstance at file scope, so two of them can only share a sketch
// inside namespaces; audioeffectx.h includes the standard headers first to
// make that possible.
//
// The sources under airwindows/ are the upstream files, unmodified (see
// airwindows/README.md for where they come from and their license). They are
// not written to this project's warning flags, so they are compiled through
// this file and the pragma below has the compiler judge them by their own.
// The pragma has no effect in a primary source file, which is why
// plugins.cpp defines PLUGINS_SOURCES and includes this file to compile them.
//
// To carry another one: copy its three files into airwindows/<Name>/ and add
// a namespace block like the ones below.

#ifndef VST_HOST_EXAMPLE_PLUGINS_H
#define VST_HOST_EXAMPLE_PLUGINS_H
#pragma GCC system_header

#include <audioeffectx.h>

namespace airwindows {

namespace capacitor {
#include "airwindows/Capacitor/Capacitor.h"
#ifdef PLUGINS_SOURCES
#include "airwindows/Capacitor/Capacitor.cpp"
#include "airwindows/Capacitor/CapacitorProc.cpp"
#endif
}

namespace density {
#include "airwindows/Density/Density.h"
#ifdef PLUGINS_SOURCES
#include "airwindows/Density/Density.cpp"
#include "airwindows/Density/DensityProc.cpp"
#endif
}

namespace pressure4 {
#include "airwindows/Pressure4/Pressure4.h"
#ifdef PLUGINS_SOURCES
#include "airwindows/Pressure4/Pressure4.cpp"
#include "airwindows/Pressure4/Pressure4Proc.cpp"
#endif
}

namespace tape {
#include "airwindows/Tape/Tape.h"
#ifdef PLUGINS_SOURCES
#include "airwindows/Tape/Tape.cpp"
#include "airwindows/Tape/TapeProc.cpp"
#endif
}

namespace tremolo {
#include "airwindows/Tremolo/Tremolo.h"
#ifdef PLUGINS_SOURCES
#include "airwindows/Tremolo/Tremolo.cpp"
#include "airwindows/Tremolo/TremoloProc.cpp"
#endif
}

}  // namespace airwindows

#endif

#ifndef AUDIO_EFFECT_VST_H
#define AUDIO_EFFECT_VST_H

// VST 2.x-style effect plug-ins as Teensy Audio Library objects.
//
//   audioeffectx.h       the plug-in interface (AudioEffectX) they compile against
//   AudioEffectVST<P>    an Audio Library object that owns and runs plug-in P
//   AudioEffectVSTHost   the same around a plug-in instance of its own
//
// A plug-in binary from a computer cannot be loaded: there is no operating
// system, no dynamic loader and the wrong processor. What runs is a plug-in's
// source, compiled into the sketch. examples/VSTHost does that with a few of
// the open-source Airwindows plug-ins.

#include "vst_host/audio_effect_vst.h"

#endif

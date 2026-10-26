// The descriptor parser against two configuration images: a UAC2 interface
// shaped like a Focusrite Scarlett (clock selector, 6-in/6-out at 24 bits in
// 32-bit subslots, asynchronous sink fed back implicitly by the source), and
// a UAC1 interface with discrete rates, an adaptive sink and an explicit
// feedback endpoint.
#include <unity.h>
#include "usb_audio_host/audio_function.h"
#include "usb_audio_host/uac.h"

using namespace usb_audio_host;

// clang-format off
static const uint8_t UAC2_IMAGE[] = {
    // Interface 0, alt 0: AudioControl, UAC2
    9, 0x04, 0, 0, 0, 0x01, 0x01, 0x20, 0,
    // AC header: bcdADC 2.00, category, wTotalLength (unused), bmControls
    9, 0x24, 0x01, 0x00, 0x02, 0x08, 0x4A, 0x00, 0x00,
    // Clock source 0x28: internal, frequency programmable
    8, 0x24, 0x0A, 0x28, 0x01, 0x03, 0x00, 0,
    // Clock source 0x29: external, not programmable
    8, 0x24, 0x0A, 0x29, 0x00, 0x01, 0x00, 0,
    // Clock selector 0x2A: pins 0x28, 0x29
    9, 0x24, 0x0B, 0x2A, 2, 0x28, 0x29, 0x03, 0,
    // Input terminal 0x01: USB streaming, 6 channels, clock 0x2A (host -> device)
    17, 0x24, 0x02, 0x01, 0x01, 0x01, 0x00, 0x2A, 6, 0x3F, 0, 0, 0, 0, 0x00, 0x00, 0,
    // Output terminal 0x02: analog out, source feature unit 0x0A, clock 0x2A
    12, 0x24, 0x03, 0x02, 0x02, 0x06, 0x00, 0x0A, 0x2A, 0x00, 0x00, 0,
    // Input terminal 0x03: analog in, 6 channels, clock 0x2A
    17, 0x24, 0x02, 0x03, 0x01, 0x06, 0x00, 0x2A, 6, 0x3F, 0, 0, 0, 0, 0x00, 0x00, 0,
    // Output terminal 0x04: USB streaming (device -> host), source 0x03, clock 0x2A
    12, 0x24, 0x03, 0x04, 0x01, 0x01, 0x00, 0x03, 0x2A, 0x00, 0x00, 0,
    // Interface 1, alt 0: AudioStreaming, idle
    9, 0x04, 1, 0, 0, 0x01, 0x02, 0x20, 0,
    // Interface 1, alt 1: 6 channels out
    9, 0x04, 1, 1, 1, 0x01, 0x02, 0x20, 0,
    16, 0x24, 0x01, 0x01, 0x00, 0x01, 0x01, 0, 0, 0, 6, 0x3F, 0, 0, 0, 0,
    6, 0x24, 0x02, 0x01, 4, 24,
    // EP 0x01 OUT, iso async data, 1024 bytes, every microframe
    7, 0x05, 0x01, 0x05, 0x00, 0x04, 1,
    8, 0x25, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00,
    // Interface 2, alt 0: AudioStreaming, idle
    9, 0x04, 2, 0, 0, 0x01, 0x02, 0x20, 0,
    // Interface 2, alt 1: 6 channels in, implicit feedback data endpoint
    9, 0x04, 2, 1, 1, 0x01, 0x02, 0x20, 0,
    16, 0x24, 0x01, 0x04, 0x00, 0x01, 0x01, 0, 0, 0, 6, 0x3F, 0, 0, 0, 0,
    6, 0x24, 0x02, 0x01, 4, 24,
    // EP 0x81 IN, iso async implicit-feedback data, 1024 bytes, every microframe
    7, 0x05, 0x81, 0x25, 0x00, 0x04, 1,
    8, 0x25, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00,
    // Interface 3: MIDIStreaming, part of the function but not audio
    9, 0x04, 3, 0, 2, 0x01, 0x03, 0x00, 0,
    7, 0x24, 0x01, 0x00, 0x01, 0x07, 0x00,
    // Interface 4: a vendor class; the function ended before it
    9, 0x04, 4, 0, 1, 0xFF, 0x00, 0x00, 0,
    7, 0x05, 0x82, 0x01, 0x00, 0x04, 1,
};

static const uint8_t UAC1_IMAGE[] = {
    // Interface 0: AudioControl, UAC1
    9, 0x04, 0, 0, 0, 0x01, 0x01, 0x00, 0,
    10, 0x24, 0x01, 0x00, 0x01, 0x28, 0x00, 2, 1, 2,
    12, 0x24, 0x02, 0x01, 0x01, 0x01, 0x00, 2, 0x03, 0x00, 0, 0,
    9, 0x24, 0x03, 0x02, 0x01, 0x03, 0x00, 0x01, 0,
    12, 0x24, 0x02, 0x03, 0x01, 0x02, 0x00, 2, 0x03, 0x00, 0, 0,
    9, 0x24, 0x03, 0x04, 0x01, 0x01, 0x00, 0x03, 0,
    // Interface 1 alt 0 idle, alt 1 stereo 16-bit out at 44100/48000, adaptive
    9, 0x04, 1, 0, 0, 0x01, 0x02, 0x00, 0,
    9, 0x04, 1, 1, 1, 0x01, 0x02, 0x00, 0,
    7, 0x24, 0x01, 0x01, 1, 0x01, 0x00,
    14, 0x24, 0x02, 0x01, 2, 2, 16, 2, 0x44, 0xAC, 0x00, 0x80, 0xBB, 0x00,
    9, 0x05, 0x01, 0x09, 0xC8, 0x00, 1, 0, 0,
    7, 0x25, 0x01, 0x01, 0x00, 0x00, 0x00,
    // Interface 1 alt 2: stereo 24-bit in 3-byte subslots, continuous 8000..96000, async with feedback EP 0x82
    9, 0x04, 1, 2, 2, 0x01, 0x02, 0x00, 0,
    7, 0x24, 0x01, 0x01, 1, 0x01, 0x00,
    14, 0x24, 0x02, 0x01, 2, 3, 24, 0, 0x40, 0x1F, 0x00, 0x00, 0x77, 0x01,
    9, 0x05, 0x01, 0x05, 0x4C, 0x02, 1, 0, 0x82,
    7, 0x25, 0x01, 0x01, 0x00, 0x00, 0x00,
    9, 0x05, 0x82, 0x11, 0x03, 0x00, 1, 1, 0,
    // Interface 2 alt 1: stereo 16-bit in, async
    9, 0x04, 2, 0, 0, 0x01, 0x02, 0x00, 0,
    9, 0x04, 2, 1, 1, 0x01, 0x02, 0x00, 0,
    7, 0x24, 0x01, 0x04, 1, 0x01, 0x00,
    11, 0x24, 0x02, 0x01, 2, 2, 16, 1, 0x80, 0xBB, 0x00,
    9, 0x05, 0x81, 0x05, 0xC8, 0x00, 1, 0, 0,
    7, 0x25, 0x01, 0x00, 0x00, 0x00, 0x00,
};
// clang-format on

static AudioFunction fn;

void setUp() {}
void tearDown() {}

static void test_uac2_version_and_entities()
{
    TEST_ASSERT_TRUE(fn.parse(UAC2_IMAGE, sizeof UAC2_IMAGE));
    TEST_ASSERT_EQUAL(static_cast<int>(UacVersion::V2), static_cast<int>(fn.version()));
    TEST_ASSERT_EQUAL(0, fn.controlInterface());
    TEST_ASSERT_EQUAL(4, fn.terminalCount());
    TEST_ASSERT_EQUAL(3, fn.clockCount());
    const ClockEntity* internal = fn.findClock(0x28);
    TEST_ASSERT_NOT_NULL(internal);
    TEST_ASSERT_TRUE(internal->programmable());
    TEST_ASSERT_FALSE(fn.findClock(0x29)->programmable());
    const ClockEntity* selector = fn.findClock(0x2A);
    TEST_ASSERT_EQUAL(uac::AC_CLOCK_SELECTOR, selector->subtype);
    TEST_ASSERT_EQUAL(2, selector->n_pins);
    TEST_ASSERT_EQUAL(0x28, selector->source_id);
}

static void test_uac2_formats()
{
    TEST_ASSERT_TRUE(fn.parse(UAC2_IMAGE, sizeof UAC2_IMAGE));
    // Alt 0 settings and the MIDI interface produce no format; the vendor
    // interface after the function is never reached.
    TEST_ASSERT_EQUAL(2, fn.formatCount());
    const Format& out = fn.format(0);
    TEST_ASSERT_EQUAL(1, out.interface);
    TEST_ASSERT_EQUAL(1, out.alt);
    TEST_ASSERT_FALSE(out.input());
    TEST_ASSERT_EQUAL(6, out.channels);
    TEST_ASSERT_EQUAL(4, out.subslot);
    TEST_ASSERT_EQUAL(24, out.bits);
    TEST_ASSERT_TRUE(out.pcm);
    TEST_ASSERT_EQUAL(0x01, out.data.address);
    TEST_ASSERT_EQUAL(1024, out.data.max_packet);
    TEST_ASSERT_EQUAL(1, out.data.mult);
    TEST_ASSERT_EQUAL(static_cast<int>(SyncType::Async), static_cast<int>(out.data.sync()));
    TEST_ASSERT_FALSE(out.feedback.valid());
    TEST_ASSERT_TRUE(out.supportsRate(192000)); // the clock decides, not the format
    const Format& in = fn.format(1);
    TEST_ASSERT_TRUE(in.input());
    TEST_ASSERT_EQUAL(0x81, in.data.address);
    TEST_ASSERT_TRUE(in.data.isImplicitFeedback());
    TEST_ASSERT_EQUAL(0x04, in.terminal_link);
}

static void test_uac2_choice_and_clock_path()
{
    TEST_ASSERT_TRUE(fn.parse(UAC2_IMAGE, sizeof UAC2_IMAGE));
    const Format* out = fn.choose(false, 2, 48000, 1024);
    TEST_ASSERT_NOT_NULL(out);
    TEST_ASSERT_EQUAL(6, out->channels);
    TEST_ASSERT_NULL(fn.choose(false, 2, 48000, 512)); // packets too large for the engine
    const Format* in = fn.choose(true, 2, 48000, 1024);
    TEST_ASSERT_NOT_NULL(in);
    TEST_ASSERT_EQUAL(in, fn.implicitFeedbackSource(*out));
    TEST_ASSERT_NULL(fn.implicitFeedbackSource(*in));
    ClockPath path;
    TEST_ASSERT_TRUE(fn.clockFor(*out, path));
    TEST_ASSERT_EQUAL(0x28, path.source);
    TEST_ASSERT_EQUAL(0x2A, path.selector);
    TEST_ASSERT_EQUAL(1, path.selector_pin);
    TEST_ASSERT_TRUE(fn.clockFor(*in, path));
    TEST_ASSERT_EQUAL(0x28, path.source);
}

static void test_uac1_formats_and_rates()
{
    TEST_ASSERT_TRUE(fn.parse(UAC1_IMAGE, sizeof UAC1_IMAGE));
    TEST_ASSERT_EQUAL(static_cast<int>(UacVersion::V1), static_cast<int>(fn.version()));
    TEST_ASSERT_EQUAL(3, fn.formatCount());
    const Format& a = fn.format(0);
    TEST_ASSERT_EQUAL(2, a.channels);
    TEST_ASSERT_EQUAL(2, a.subslot);
    TEST_ASSERT_EQUAL(16, a.bits);
    TEST_ASSERT_EQUAL(2, a.n_rates);
    TEST_ASSERT_EQUAL(44100, a.rates[0]);
    TEST_ASSERT_EQUAL(48000, a.rates[1]);
    TEST_ASSERT_TRUE(a.supportsRate(44100));
    TEST_ASSERT_FALSE(a.supportsRate(96000));
    TEST_ASSERT_TRUE(a.rate_control);
    TEST_ASSERT_EQUAL(static_cast<int>(SyncType::Adaptive), static_cast<int>(a.data.sync()));
    TEST_ASSERT_EQUAL(200, a.data.max_packet);
    const Format& b = fn.format(1);
    TEST_ASSERT_EQUAL(3, b.subslot);
    TEST_ASSERT_EQUAL(0, b.n_rates);
    TEST_ASSERT_EQUAL(8000, b.rate_min);
    TEST_ASSERT_EQUAL(96000, b.rate_max);
    TEST_ASSERT_TRUE(b.supportsRate(88200));
    TEST_ASSERT_FALSE(b.supportsRate(176400));
    TEST_ASSERT_TRUE(b.feedback.valid());
    TEST_ASSERT_EQUAL(0x82, b.feedback.address);
    TEST_ASSERT_EQUAL(3, b.feedback.max_packet);
    TEST_ASSERT_EQUAL(588, b.data.max_packet);
    const Format& c = fn.format(2);
    TEST_ASSERT_TRUE(c.input());
    TEST_ASSERT_FALSE(c.rate_control);
}

static void test_uac1_choice_prefers_rate_then_resolution()
{
    TEST_ASSERT_TRUE(fn.parse(UAC1_IMAGE, sizeof UAC1_IMAGE));
    // Both OUT settings carry 44100; the 24-bit one wins.
    const Format* out = fn.choose(false, 2, 44100, 1024);
    TEST_ASSERT_NOT_NULL(out);
    TEST_ASSERT_EQUAL(2, out->alt);
    // Only alt 1 lists 48000 exactly... and so does the continuous range.
    TEST_ASSERT_EQUAL(2, fn.choose(false, 2, 48000, 1024)->alt);
    // Nothing offers 176400.
    TEST_ASSERT_NULL(fn.choose(false, 2, 176400, 1024));
    // The explicit feedback endpoint means no implicit source is wanted.
    TEST_ASSERT_NULL(fn.implicitFeedbackSource(*out));
    // The adaptive sink wants none either.
    TEST_ASSERT_NULL(fn.implicitFeedbackSource(fn.format(0)));
    ClockPath path;
    TEST_ASSERT_TRUE(fn.clockFor(*out, path));
    TEST_ASSERT_EQUAL(0, path.source);
}

static void test_rejects_non_control_interface()
{
    // Offered an AudioStreaming interface, the driver must decline.
    TEST_ASSERT_FALSE(fn.parse(UAC2_IMAGE + 9 + 9 + 8 + 8 + 9 + 17 + 12 + 17 + 12, 60));
    TEST_ASSERT_FALSE(fn.parse(UAC2_IMAGE, 4));
    static const uint8_t hid[] = {9, 0x04, 0, 0, 1, 0x03, 0x00, 0x00, 0};
    TEST_ASSERT_FALSE(fn.parse(hid, sizeof hid));
}

static void test_truncated_image_is_safe()
{
    // Every prefix parses without reading past its end (a crash here would
    // be a crash in the enumeration interrupt).
    for (uint32_t n = 0; n < sizeof UAC2_IMAGE; n++) fn.parse(UAC2_IMAGE, n);
    TEST_ASSERT_TRUE(fn.parse(UAC2_IMAGE, sizeof UAC2_IMAGE));
}

int main()
{
    UNITY_BEGIN();
    RUN_TEST(test_uac2_version_and_entities);
    RUN_TEST(test_uac2_formats);
    RUN_TEST(test_uac2_choice_and_clock_path);
    RUN_TEST(test_uac1_formats_and_rates);
    RUN_TEST(test_uac1_choice_prefers_rate_then_resolution);
    RUN_TEST(test_rejects_non_control_interface);
    RUN_TEST(test_truncated_image_is_safe);
    return UNITY_END();
}

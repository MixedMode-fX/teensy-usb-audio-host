// Records the first two inputs of a class-compliant USB audio interface on
// the Teensy's host port to a 16-bit stereo WAV file on the SD card, while
// passing them through to the interface's outputs for monitoring.
//
// Wiring: the interface into the host port (see the Passthrough example);
// a card in the Teensy 4.1 or 3.6 built-in SD socket.
//
// Serial monitor keys:   r  start recording   s  stop   i  status
//
// The two AudioRecordQueue objects hand the sketch 128-sample blocks; the
// sketch interleaves them into 512-byte card writes, which is the pattern
// of the Audio Library's own Recorder example. The interface keeps
// streaming while the card is busy: the USB engine runs in its interrupt
// and holds a few milliseconds of audio, and the queues hold more.

#include <Audio.h>
#include <SD.h>
#include <USBHost_t36.h>
#include <USBAudioHost.h>

USBHost usb;
USBHub hub(usb);
USBAudioDevice interface(usb);

AudioInputUSBHost in(interface, 2);
AudioRecordQueue queueL;
AudioRecordQueue queueR;
AudioOutputUSBHost out(interface, 2);

AudioConnection c1(in, 0, queueL, 0);
AudioConnection c2(in, 1, queueR, 0);
AudioConnection c3(in, 0, out, 0);
AudioConnection c4(in, 1, out, 1);

static File wav;
static bool recording = false;
static uint32_t data_bytes = 0;
static uint32_t rate = 44100;
static USBAudioDevice::State shown = USBAudioDevice::State::Idle;

static void writeHeader(uint32_t bytes)
{
    const uint16_t channels = 2;
    const uint16_t bits = 16;
    const uint32_t byte_rate = rate * channels * bits / 8;
    const uint16_t block_align = channels * bits / 8;
    uint8_t h[44];
    memcpy(h, "RIFF", 4);
    const uint32_t riff = 36 + bytes;
    memcpy(h + 4, &riff, 4);
    memcpy(h + 8, "WAVEfmt ", 8);
    const uint32_t fmt_len = 16;
    memcpy(h + 16, &fmt_len, 4);
    const uint16_t pcm = 1;
    memcpy(h + 20, &pcm, 2);
    memcpy(h + 22, &channels, 2);
    memcpy(h + 24, &rate, 4);
    memcpy(h + 28, &byte_rate, 4);
    memcpy(h + 32, &block_align, 2);
    memcpy(h + 34, &bits, 2);
    memcpy(h + 36, "data", 4);
    memcpy(h + 40, &bytes, 4);
    wav.seek(0);
    wav.write(h, sizeof h);
}

static void startRecording()
{
    if (recording) return;
    if (!interface.streaming()) {
        Serial.println("no interface streaming");
        return;
    }
    rate = interface.sampleRate();
    char name[16];
    for (int i = 0; i < 1000; i++) {
        snprintf(name, sizeof name, "REC%03d.WAV", i);
        if (!SD.exists(name)) break;
    }
    wav = SD.open(name, FILE_WRITE);
    if (!wav) {
        Serial.println("could not open a file on the card");
        return;
    }
    data_bytes = 0;
    writeHeader(0);
    queueL.begin();
    queueR.begin();
    recording = true;
    Serial.print("recording ");
    Serial.println(name);
}

static void stopRecording()
{
    if (!recording) return;
    queueL.end();
    queueR.end();
    while (queueL.available() > 0) queueL.readBuffer(), queueL.freeBuffer();
    while (queueR.available() > 0) queueR.readBuffer(), queueR.freeBuffer();
    recording = false;
    writeHeader(data_bytes);
    wav.close();
    Serial.print("stopped, ");
    Serial.print(data_bytes / (rate * 4));
    Serial.println(" seconds");
}

// Two blocks in, one 512-byte card write out.
static void drainQueues()
{
    if (!recording) return;
    while (queueL.available() >= 1 && queueR.available() >= 1) {
        const int16_t* l = queueL.readBuffer();
        const int16_t* r = queueR.readBuffer();
        int16_t frame[AUDIO_BLOCK_SAMPLES * 2];
        for (int i = 0; i < AUDIO_BLOCK_SAMPLES; i++) {
            frame[2 * i] = l[i];
            frame[2 * i + 1] = r[i];
        }
        queueL.freeBuffer();
        queueR.freeBuffer();
        wav.write(reinterpret_cast<const uint8_t*>(frame), sizeof frame);
        data_bytes += sizeof frame;
    }
}

void setup()
{
    Serial.begin(115200);
    AudioMemory(120);
    interface.setSampleRate(44100); // what the Audio Library assumes
    interface.setChannels(2, 2);
    usb.begin();
    in.begin();
    out.begin();
    if (!SD.begin(BUILTIN_SDCARD)) {
        Serial.println("no SD card: recording is off, monitoring still works");
    }
    Serial.println("USB audio recorder: r to record, s to stop, i for status");
}

void loop()
{
    usb.Task();
    drainQueues();

    if (interface.state() != shown) {
        shown = interface.state();
        interface.printStatus(Serial);
        if (shown != USBAudioDevice::State::Streaming) stopRecording();
    }

    while (Serial.available()) {
        const int key = Serial.read();
        if (key == 'r') startRecording();
        if (key == 's') stopRecording();
        if (key == 'i') {
            interface.printStatus(Serial);
            Serial.print("  recording: ");
            Serial.print(recording ? "yes, " : "no, ");
            Serial.print(data_bytes / (rate * 4));
            Serial.print(" s; audio library ");
            Serial.print(AudioProcessorUsageMax());
            Serial.print("% cpu, ");
            Serial.print(AudioMemoryUsageMax());
            Serial.println(" blocks");
        }
    }
}

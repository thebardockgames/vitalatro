#pragma once
// Software mixer standing in for LOVE's audio sources (OpenAL): voices play decoded PCM ("static"
// sources) or Ogg Vorbis streamed from memory ("stream" sources), with pitch (playback rate) and
// volume. The platform pulls mixed stereo frames from its audio thread.
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace vl::audio {

constexpr int kOutputRate = 48000;

// An .ogg file kept in memory, decoded to PCM up front when it is short.
struct Clip {
    std::string ogg;              // the file
    std::vector<int16_t> pcm;     // interleaved, empty for streamed clips
    int channels = 0, rate = 0;
    bool stream = false;
};
using ClipRef = std::shared_ptr<const Clip>;
// Files over kStreamBytes are always streamed: decoded they would take ~10x their size in RAM.
constexpr size_t kStreamBytes = 150000;
ClipRef load_clip(const std::string &path, bool stream);

using VoiceId = uint32_t;  // 0: none
VoiceId play(const ClipRef &clip, float pitch, float volume);
void set_volume(VoiceId v, float volume);
void set_pitch(VoiceId v, float pitch);
void stop(VoiceId v);
bool is_playing(VoiceId v);
void stop_all();

// Called by the platform audio thread: mixes n stereo frames (interleaved int16).
void mix(int16_t *out, int frames);

// Starts the platform output (Vita: an audio port fed by a thread on a free core).
void start_output();

}  // namespace vl::audio

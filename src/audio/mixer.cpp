#include "audio/mixer.h"

#include <algorithm>
#include <cmath>
#include <cstring>

#include "platform/platform.h"

#define STB_VORBIS_HEADER_ONLY
#include "stb/stb_vorbis.c"

namespace vl::audio {

namespace {
constexpr float kSilent = 2e-4f;  // below the 16-bit output's last bits

struct Voice {
    VoiceId id = 0;
    ClipRef clip;
    stb_vorbis *dec = nullptr;  // streamed clips
    std::vector<float> buf;     // decoded window of a stream (interleaved)
    int64_t buf_start = 0;      // sample frame index of buf[0]
    int buf_frames = 0;
    int64_t length = 0;         // streams: total sample frames
    bool skipped = false;       // streams: decoding stopped while silent, pos kept moving
    double pos = 0;             // playback position in source sample frames
    float pitch = 1, volume = 1;
    bool done = false;
};

platform::Mutex &mutex = *new platform::Mutex;
std::vector<std::unique_ptr<Voice>> voices;
VoiceId next_id = 1;
struct Lock {
    Lock() { mutex.lock(); }
    ~Lock() { mutex.unlock(); }
};

Voice *find(VoiceId v) {
    for (auto &p : voices)
        if (p->id == v) return p.get();
    return nullptr;
}

// One source frame (stereo, -1..1) of a voice, or false at the end of the clip.
bool frame_at(Voice &v, int64_t f, float &l, float &r) {
    const Clip &c = *v.clip;
    if (!c.stream) {
        int64_t n = (int64_t)c.pcm.size() / c.channels;
        if (f >= n) return false;
        const int16_t *s = &c.pcm[(size_t)f * c.channels];
        l = s[0] / 32768.f;
        r = c.channels > 1 ? s[1] / 32768.f : l;
        return true;
    }
    while (f >= v.buf_start + v.buf_frames) {
        // decode the next chunk (streams only move forward); the previous chunk's last frame is
        // kept in front so interpolation across the boundary still has both neighbours
        float **out = nullptr;
        int n = stb_vorbis_get_frame_float(v.dec, nullptr, &out);
        if (n <= 0) return false;
        float last_l = v.buf_frames ? v.buf[(size_t)(v.buf_frames - 1) * 2] : 0.f;
        float last_r = v.buf_frames ? v.buf[(size_t)(v.buf_frames - 1) * 2 + 1] : 0.f;
        bool keep = v.buf_frames > 0;
        v.buf_start += v.buf_frames - (keep ? 1 : 0);
        v.buf.resize((size_t)(n + (keep ? 1 : 0)) * 2);
        size_t o = 0;
        if (keep) {
            v.buf[0] = last_l;
            v.buf[1] = last_r;
            o = 2;
        }
        for (int i = 0; i < n; i++) {
            v.buf[o + i * 2] = out[0][i];
            v.buf[o + i * 2 + 1] = c.channels > 1 ? out[1][i] : out[0][i];
        }
        v.buf_frames = n + (keep ? 1 : 0);
    }
    if (f < v.buf_start) f = v.buf_start;
    const float *s = &v.buf[(size_t)(f - v.buf_start) * 2];
    l = s[0];
    r = s[1];
    return true;
}
}  // namespace

ClipRef load_clip(const std::string &path, bool stream) {
    auto c = std::make_shared<Clip>();
    if (!platform::read_file(path.c_str(), c->ogg)) return nullptr;
    int err = 0;
    stb_vorbis *v = stb_vorbis_open_memory((const unsigned char *)c->ogg.data(), (int)c->ogg.size(), &err, nullptr);
    if (!v) return nullptr;
    stb_vorbis_info info = stb_vorbis_get_info(v);
    c->channels = info.channels;
    c->rate = (int)info.sample_rate;
    c->stream = stream || c->ogg.size() > kStreamBytes;
    if (!stream) {
        int total = (int)stb_vorbis_stream_length_in_samples(v);
        c->pcm.resize((size_t)total * c->channels);
        int got = stb_vorbis_get_samples_short_interleaved(v, c->channels, c->pcm.data(), (int)c->pcm.size());
        c->pcm.resize((size_t)got * c->channels);
        c->ogg.clear();
        c->ogg.shrink_to_fit();
    }
    stb_vorbis_close(v);
    return c;
}

VoiceId play(const ClipRef &clip, float pitch, float volume) {
    if (!clip) return 0;
    auto v = std::make_unique<Voice>();
    v->clip = clip;
    if (clip->stream) {
        int err = 0;
        v->dec = stb_vorbis_open_memory((const unsigned char *)clip->ogg.data(), (int)clip->ogg.size(), &err, nullptr);
        if (!v->dec) return 0;
        v->length = stb_vorbis_stream_length_in_samples(v->dec);
    }
    v->pitch = pitch;
    v->volume = std::clamp(volume, 0.f, 1.f);
    Lock lock;
    v->id = next_id++;
    VoiceId id = v->id;
    voices.push_back(std::move(v));
    return id;
}

void set_volume(VoiceId id, float volume) {
    Lock lock;
    if (Voice *v = find(id)) v->volume = std::clamp(volume, 0.f, 1.f);
}

void set_pitch(VoiceId id, float pitch) {
    Lock lock;
    if (Voice *v = find(id)) v->pitch = pitch;
}

void stop(VoiceId id) {
    Lock lock;
    if (Voice *v = find(id)) v->done = true;
}

bool is_playing(VoiceId id) {
    Lock lock;
    Voice *v = find(id);
    return v && !v->done;
}

void stop_all() {
    Lock lock;
    for (auto &v : voices) v->done = true;
}

void mix(int16_t *out, int frames) {
    static std::vector<float> acc;
    static std::vector<Voice *> active;
    acc.assign((size_t)frames * 2, 0.f);
    {
        // only the list is locked: decoding and mixing run without blocking the game thread
        // (play() appends, and voices are only deleted here)
        Lock lock;
        active.clear();
        for (auto &vp : voices) active.push_back(vp.get());
    }
    {
        for (Voice *vp : active) {
            Voice &v = *vp;
            if (v.done) continue;
            const double step = v.pitch * v.clip->rate / (double)kOutputRate;
            const float vol = v.volume;
            if (v.dec) {
                // The music stems all play in sync and the silent ones fade towards 0 forever:
                // an inaudible stream only advances its position, and seeks back into the file
                // when it becomes audible again (most of the decode cost of the 5 stems).
                if (vol < kSilent) {
                    v.pos += step * frames;
                    v.skipped = true;
                    if ((int64_t)v.pos >= v.length) v.done = true;
                    continue;
                }
                if (v.skipped) {
                    v.skipped = false;
                    int64_t f = (int64_t)v.pos;
                    if (f >= v.length || !stb_vorbis_seek(v.dec, (unsigned)f)) {
                        v.done = true;
                        continue;
                    }
                    v.buf_start = f;
                    v.buf_frames = 0;
                }
            }
            if (!v.dec) {
                // decoded clip: straight over the PCM (the splash fires ~30 whooshes at once)
                const Clip &c = *v.clip;
                const int64_t n = (int64_t)c.pcm.size() / c.channels;
                const int16_t *pcm = c.pcm.data();
                const int ch = c.channels, r_off = c.channels > 1 ? 1 : 0;
                const float g = vol / 32768.f;
                double pos = v.pos;
                float *o = acc.data();
                for (int i = 0; i < frames; i++, o += 2) {
                    int64_t f = (int64_t)pos;
                    if (f >= n) {
                        v.done = true;
                        break;
                    }
                    float t = (float)(pos - (double)f);
                    const int16_t *a = pcm + f * ch;
                    const int16_t *b = f + 1 < n ? a + ch : a;
                    o[0] += (a[0] + (b[0] - a[0]) * t) * g;
                    o[1] += (a[r_off] + (b[r_off] - a[r_off]) * t) * g;
                    pos += step;
                }
                v.pos = pos;
                continue;
            }
            for (int i = 0; i < frames; i++) {
                // linear interpolation between the two source frames around pos
                int64_t f = (int64_t)v.pos;
                float t = (float)(v.pos - (double)f);
                float l0, r0, l1, r1;
                if (!frame_at(v, f, l0, r0)) {
                    v.done = true;
                    break;
                }
                if (!frame_at(v, f + 1, l1, r1)) {
                    l1 = l0;
                    r1 = r0;
                }
                acc[i * 2] += (l0 + (l1 - l0) * t) * vol;
                acc[i * 2 + 1] += (r0 + (r1 - r0) * t) * vol;
                v.pos += step;
            }
        }
    }
    {
        Lock lock;
        voices.erase(std::remove_if(voices.begin(), voices.end(),
                                    [](const std::unique_ptr<Voice> &v) {
                                        if (v->done && v->dec) stb_vorbis_close(v->dec);
                                        return v->done;
                                    }),
                     voices.end());
    }
    for (int i = 0; i < frames * 2; i++) {
        float s = std::clamp(acc[i], -1.f, 1.f);
        out[i] = (int16_t)std::lrint(s * 32767.f);
    }
}

}  // namespace vl::audio

#include "game/sound.h"

#include <algorithm>
#include <cmath>
#include <deque>
#include <map>
#include <string>
#include <vector>

#include "audio/mixer.h"
#include "game/blind.h"
#include "game/g.h"
#include "platform/platform.h"

namespace vl {

namespace {
inline Value fld(const Value &t, const char *k) { return t.get(Value(intern(k))); }

// A request as pushed on G.SOUND_MANAGER.channel (the channel copies the table, so does this)
struct Request {
    enum Type { Sound, Stop, Modulate, ResetStates } type = Sound;
    std::string sound_code, desired_track;
    float per = 1, vol = 1, pitch_mod = 1, splash_vol = 1;
    double dt = 0;
    int state = 0;
    bool overlay_menu = false;
    // sound_settings
    float volume = 50, music_volume = 100, game_sounds_volume = 100;
    // ambient_control (modulate only)
    struct Ambient {
        std::string code;
        float vol, per;
    };
    std::vector<Ambient> ambient_control;
    bool has_ambient = false;
};

// ------------------------------------------------------------------ engine/sound_manager.lua
// Everything below runs on the sound thread.
struct Source {
    audio::VoiceId voice = 0;
    std::string sound_code;
    float original_pitch = 1, original_volume = 1;
    float current_volume = 0;
    bool has_current_volume = false;
    float temp_pitch = -1;
    int created_on_state = 0;
    bool playing() const { return audio::is_playing(voice); }
};
std::map<std::string, std::vector<Source>> SOURCES;
std::map<std::string, audio::ClipRef> clips;
std::vector<std::string> preload;  // sounds not loaded yet, loaded while the thread is idle
const char *const MUSIC[] = {"music1", "music2", "music3", "music4", "music5"};

bool contains(const std::string &s, const char *sub) { return s.find(sub) != std::string::npos; }

audio::ClipRef clip(const std::string &code) {
    auto it = clips.find(code);
    if (it != clips.end()) return it->second;
    bool stream = contains(code, "music") || contains(code, "ambient");
    audio::ClipRef c = audio::load_clip("sounds/" + code + ".ogg", stream);
    if (!c) platform::log("sound: cannot load %s", code.c_str());
    clips[code] = c;
    return c;
}

void SET_SFX(Source &s, const Request &args, bool apply) {
    float vol, pitch;
    if (contains(s.sound_code, "music")) {
        float target = s.sound_code == args.desired_track ? 1.f : 0.f;
        if (!s.has_current_volume) {
            s.current_volume = target;
            s.has_current_volume = true;
        }
        s.current_volume = target * (float)(args.dt * 3) + (1 - (float)(args.dt * 3)) * s.current_volume;
        vol = s.current_volume * s.original_volume * (args.volume / 100.f) * (args.music_volume / 100.f);
        pitch = s.original_pitch * args.pitch_mod;
    } else {
        pitch = s.original_pitch;
        s.temp_pitch = s.original_pitch;
        vol = s.original_volume * (args.volume / 100.f) * (args.game_sounds_volume / 100.f);
        if (s.created_on_state == 13) vol *= args.splash_vol;
        if (vol <= 0) {
            audio::stop(s.voice);
            s.voice = 0;
            return;
        }
    }
    if (apply) {
        audio::set_volume(s.voice, vol);
        audio::set_pitch(s.voice, pitch);
    } else {
        s.voice = audio::play(clip(s.sound_code), pitch, vol);
    }
}

// Stopped sources are dropped by MODULATE (as the original releases them), so a new one is
// always made here: a voice is cheap, the clip is shared.
void PLAY_SOUND(const Request &args) {
    if (!clip(args.sound_code)) return;
    Source s;
    s.sound_code = args.sound_code;
    s.original_pitch = args.per;
    s.original_volume = args.vol;
    s.created_on_state = args.state;
    SET_SFX(s, args, false);
    if (s.voice) SOURCES[args.sound_code].push_back(s);
}

void STOP_AUDIO() {
    for (auto &kv : SOURCES)
        for (Source &s : kv.second) audio::stop(s.voice);
}

void RESTART_MUSIC(Request &args) {
    for (const char *k : MUSIC) {
        for (Source &s : SOURCES[k]) audio::stop(s.voice);
        SOURCES[k].clear();
        args.per = 0.7f;
        args.vol = 0.6f;
        args.sound_code = k;
        PLAY_SOUND(args);
    }
}

void MODULATE(Request &args) {
    if (!args.desired_track.empty()) {
        for (const char *k : MUSIC) {
            auto &v = SOURCES[k];
            if (v.empty() || !v[0].playing()) {
                RESTART_MUSIC(args);
                break;
            }
        }
    }
    for (auto &kv : SOURCES) {
        auto &v = kv.second;
        v.erase(std::remove_if(v.begin(), v.end(), [](const Source &s) { return !s.playing(); }), v.end());
        for (Source &s : v) SET_SFX(s, args, true);
    }
}

void AMBIENT(Request &args) {
    for (const Request::Ambient &a : args.ambient_control) {
        bool start_ambient = a.vol * (args.volume / 100.f) * (args.game_sounds_volume / 100.f) > 0;
        for (Source &s : SOURCES[a.code]) {
            if (s.playing()) {
                s.original_volume = a.vol;
                SET_SFX(s, args, true);
                start_ambient = false;
            }
        }
        if (start_ambient) {
            args.sound_code = a.code;
            args.vol = a.vol;
            args.per = a.per;
            PLAY_SOUND(args);
        }
    }
}

void handle(Request &r) {
    switch (r.type) {
    case Request::Sound: PLAY_SOUND(r); break;
    case Request::Stop: STOP_AUDIO(); break;
    case Request::Modulate:
        MODULATE(r);
        if (r.has_ambient) AMBIENT(r);
        break;
    case Request::ResetStates:
        for (auto &kv : SOURCES)
            for (Source &s : kv.second) s.created_on_state = r.state;
        break;
    }
}

// ------------------------------------------------------------------ the channel
// never destroyed: the thread still waits on them while the program exits
platform::Mutex &mutex = *new platform::Mutex;
platform::Semaphore &pending = *new platform::Semaphore;
std::deque<Request> channel;
bool started = false;

void push(Request r) {
    if (!started) return;
    mutex.lock();
    channel.push_back(std::move(r));
    mutex.unlock();
    pending.post();
}

void thread_main() {
    for (;;) {
        if (!pending.try_wait()) {
            if (!preload.empty()) {
                // nothing to play: decode the next sound ahead of its first use
                std::string code = preload.back();
                preload.pop_back();
                clip(code);
                continue;
            }
            pending.wait();
        }
        mutex.lock();
        Request r = std::move(channel.front());
        channel.pop_front();
        mutex.unlock();
        handle(r);
    }
}

void fill_settings(Request &r) {
    Value snd = G.SETTINGS->get(K("SOUND"));
    r.volume = (float)fld(snd, "volume").num();
    r.music_volume = (float)fld(snd, "music_volume").num();
    r.game_sounds_volume = (float)fld(snd, "game_sounds_volume").num();
    r.pitch_mod = (float)G.PITCH_MOD;
    r.state = G.STATE;
    r.splash_vol = (float)G.SPLASH_VOL;
    r.overlay_menu = G.OVERLAY_MENU.get() != nullptr;
}

bool live(const NodeRef &n) { return n && !n->REMOVED; }
}  // namespace

void sound_start() {
    // love.filesystem.getDirectoryItems("resources/sounds"), shipped as a list
    std::string index;
    if (platform::read_file("sounds/index.txt", index)) {
        size_t a = 0;
        while (a < index.size()) {
            size_t b = index.find('\n', a);
            if (b == std::string::npos) b = index.size();
            size_t e = b;
            while (e > a && (index[e - 1] == '\r' || index[e - 1] == ' ')) e--;
            if (e > a) preload.push_back(index.substr(a, e - a));
            a = b + 1;
        }
        std::reverse(preload.begin(), preload.end());  // popped from the back: file order
    }
    audio::start_output();
    started = true;
    platform::run_thread(thread_main);
}

void play_sound(const char *sound_code, float per, float vol) {
    if (G.F_MUTE) return;
    if (!sound_code || !(fld(G.SETTINGS->get(K("SOUND")), "volume").num() > 0.001)) return;
    Request r;
    r.type = Request::Sound;
    r.sound_code = sound_code;
    r.per = per;
    r.vol = vol;
    fill_settings(r);
    push(std::move(r));
}

void modulate_sound(double dt) {
    // volume of the splash screen is set here
    G.SPLASH_VOL = 2 * dt * (G.STATE == SPLASH ? 1 : 0) + G.SPLASH_VOL * (1 - 2 * dt);

    // Control the music here
    Value blind = G.GAME->get(K("blind"));
    const char *desired_track = G.STATE == SPLASH ? ""
                                // TODO(run): booster_pack_sparkles -> music2, booster_pack_meteors -> music3
                                : live(G.booster_pack)                         ? "music2"
                                : live(G.shop)                                 ? "music4"
                                : blind.truthy() && fld(blind, "boss").truthy() ? "music5"
                                                                               : "music1";

    G.PITCH_MOD = G.PITCH_MOD * (1 - dt) + dt * ((!G.normal_music_speed && G.STATE == GAME_OVER) ? 0.5 : 1);

    // For ambient sound control
    Value AC = G.SETTINGS->get(K("ambient_control"));
    if (!AC.is_table()) {
        AC = Value(T({}));
        G.SETTINGS->set(K("ambient_control"), AC);
    }
    Value hand = fld(fld(G.GAME->get(K("current_round")), "current_hand"), "chips");
    Value mult = fld(fld(G.GAME->get(K("current_round")), "current_hand"), "mult");
    double earned_score = hand.is_num() && mult.is_num() ? hand.num() * mult.num() : 0;
    const Blind *bl = blind.as<Blind>();  // a Blind object: chips is a C++ field
    double required_score = bl ? bl->chips : 0;
    // G.ARGS.score_intensity: flame_handler reads these, so its flames lag the score by one
    // frame like the original's (this runs before the frame's events)
    Value si = G.ARGS->get(K("score_intensity"));
    if (!si.is_table()) {
        si = Value(T({}));
        G.ARGS->set(K("score_intensity"), si);
    }
    si.table()->set("earned_score", earned_score);
    si.table()->set("required_score", required_score);
    Value chip_flames = G.ARGS->get(K("chip_flames")), mult_flames = G.ARGS->get(K("mult_flames"));
    double flames = std::min(1.0, (G.STAGE == RUN ? 1 : 0) *
                                      (chip_flames.truthy() ? fld(chip_flames, "real_intensity").num() + fld(chip_flames, "change").num()
                                                            : 0)) /
                    10;
    double organ = required_score > 0
                       ? std::max(std::min(0.4, 0.1 * std::log(earned_score / (required_score + 1)) / std::log(5.0)), 0.)
                       : 0;
    double sound_music = fld(G.SETTINGS->get(K("SOUND")), "music_volume").num();

    // G.ARGS.ambient_sounds (pairs order does not matter: each sound is independent)
    static const char *const ambient_names[] = {"ambientFire2", "ambientFire1", "ambientFire3", "ambientOrgan1"};
    Request r;
    r.type = Request::Modulate;
    r.dt = dt;
    r.desired_track = desired_track;
    for (int i = 0; i < 4; i++) {
        const char *k = ambient_names[i];
        Value ac = AC[k];
        if (!ac.is_table()) {
            ac = Value(T({}));
            AC.table()->set(Value(intern(k)), ac);
        }
        double per = i == 3 ? 0.7 : i == 1 ? 1.1 : i == 0 ? 1.05 : 1;
        Value prev_v = fld(ac, "vol");
        double vol = 0;
        if (!(G.STATE == SPLASH) && prev_v.truthy()) {
            double prev = prev_v.num();
            switch (i) {
            case 0: vol = prev * (1 - dt) + dt * 0.9 * (flames > 0.3 ? 1 : flames / 0.3); break;
            case 1: vol = prev * (1 - dt) + dt * 0.8 * (flames > 0.3 ? (flames - 0.3) / 0.7 : 0); break;
            case 2:
                vol = prev * (1 - dt) + dt * 0.4 *
                                            ((chip_flames.truthy() ? fld(chip_flames, "change").num() : 0) +
                                             (mult_flames.truthy() ? fld(mult_flames, "change").num() : 0));
                break;
            case 3: vol = prev * (1 - dt) + dt * 0.6 * (sound_music + 100) / 200 * organ; break;
            }
        }
        ac.table()->set(K("per"), Value(per));
        ac.table()->set(K("vol"), Value(vol));
        r.ambient_control.push_back({k, (float)vol, (float)per});
    }
    r.has_ambient = true;
    fill_settings(r);
    push(std::move(r));
}

void sound_reset_states(int state) {
    Request r;
    r.type = Request::ResetStates;
    r.state = state;
    push(std::move(r));
}

void sound_stop() {
    Request r;
    r.type = Request::Stop;
    push(std::move(r));
}

}  // namespace vl

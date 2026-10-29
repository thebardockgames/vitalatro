// Vita audio output: a BGM port at 48 kHz fed by a thread on the cores the game thread leaves free.
#include <psp2/audioout.h>
#include <psp2/kernel/processmgr.h>
#include <psp2/kernel/threadmgr.h>

#include "audio/mixer.h"
#include "platform/platform.h"

namespace vl::audio {

namespace {
constexpr int kGrain = 1024;  // frames per output call (~21 ms)

int audio_thread(SceSize, void *) {
    int port = sceAudioOutOpenPort(SCE_AUDIO_OUT_PORT_TYPE_BGM, kGrain, kOutputRate, SCE_AUDIO_OUT_MODE_STEREO);
    if (port < 0) {
        platform::log("audio: no output port (0x%08x)", (unsigned)port);
        return sceKernelExitDeleteThread(0);
    }
    int vol[2] = {SCE_AUDIO_VOLUME_0DB, SCE_AUDIO_VOLUME_0DB};
    sceAudioOutSetVolume(port, (SceAudioOutChannelFlag)(SCE_AUDIO_VOLUME_FLAG_L_CH | SCE_AUDIO_VOLUME_FLAG_R_CH), vol);
    static int16_t buf[2][kGrain * 2];
    // mix cost, logged every ~5 s: a grain lasts 21.3 ms, the mix must stay well under it
    SceUInt64 total = 0, worst = 0;
    int grains = 0;
    for (int i = 0;; i ^= 1) {
        SceUInt64 t0 = sceKernelGetProcessTimeWide();
        mix(buf[i], kGrain);
        SceUInt64 dt = sceKernelGetProcessTimeWide() - t0;
        total += dt;
        if (dt > worst) worst = dt;
        if (++grains == 235) {
#ifdef VL_DEV
            platform::log("audio: mix avg %.2f ms, max %.2f ms per 21.3 ms grain", total / 235 / 1000.0, worst / 1000.0);
#endif
            total = worst = 0;
            grains = 0;
        }
        sceAudioOutOutput(port, buf[i]);  // blocks until the previous buffer has been consumed
    }
    return 0;
}
}  // namespace

void start_output() {
    // above the game thread's priority (it only wakes every grain), on cores 1 and 2
    SceUID t = sceKernelCreateThread("vl_audio", audio_thread, 0x10000100 - 10, 64 * 1024, 0, 0x20000 | 0x40000, nullptr);
    if (t < 0 || sceKernelStartThread(t, 0, nullptr) < 0) platform::log("audio: could not start the output thread");
}

}  // namespace vl::audio

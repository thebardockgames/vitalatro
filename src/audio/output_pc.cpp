// PC (headless) audio output: the mix is consumed at the real-time rate and discarded, so voices
// finish when they would have finished playing. VL_PCM=<file> also writes it out (s16le stereo 48 kHz).
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <thread>

#include "audio/mixer.h"

namespace vl::audio {

void start_output() {
    std::thread([] {
        static int16_t buf[1024 * 2];
        FILE *pcm = std::getenv("VL_PCM") ? std::fopen(std::getenv("VL_PCM"), "wb") : nullptr;
        for (;;) {
            mix(buf, 1024);
            if (pcm) {
                std::fwrite(buf, sizeof buf, 1, pcm);
                std::fflush(pcm);
            }
            std::this_thread::sleep_for(std::chrono::microseconds(1024 * 1000000 / kOutputRate));
        }
    }).detach();
}

}  // namespace vl::audio

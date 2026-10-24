#pragma once
// The dev server (VL_DEV builds): a TCP command channel to the running game, used by
// tools/vitactl.py on the PC to read performance, take screenshots, inject input, hot-reload
// shaders, change settings and move files without reinstalling or going through FTP.
//
// Wire format, both directions: a header line, then `len` raw bytes.
//   request:  "<len> <command> [args...]\n" <payload>
//   response: "OK <len>\n" <payload>   or   "ERR <len>\n" <message>
// Commands run on the game thread between frames; some answer frames later (perf, wait, shot).
#include "platform/platform.h"

namespace vl::dev {

constexpr int kPort = 7777;

void start();
bool running();

struct FrameTimes {
    double dt, input, update, draw, submit;  // dt in seconds, the rest in ms
};
// game_frame: before the update (runs queued commands, injects scripted input into `in`)...
void frame_begin(platform::Input &in);
// ...and after the frame was presented (screenshots, perf sampling)
void frame_end(const FrameTimes &t);

}  // namespace vl::dev

#include <psp2/ctrl.h>
#include <psp2/io/fcntl.h>
#include <psp2/io/stat.h>
#include <psp2/kernel/processmgr.h>
#include <psp2/power.h>
#include <psp2/touch.h>

#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <string>

#include "core/time.h"
#include "platform/platform.h"

// Newlib heap: let the game use most of the user memory (textures live in CDRAM).
extern "C" {
int _newlib_heap_size_user = 192 * 1024 * 1024;
}

namespace vl {

double time_now() { return sceKernelGetProcessTimeWide() / 1000000.0; }
void time_sleep(double s) {
    if (s > 0) sceKernelDelayThread((SceUInt)(s * 1000000.0));
}

namespace platform {
namespace {
const char *kDataRoot = "app0:";
const char *kSaveRoot = "ux0:data/Vitalatro/";

bool read_whole(const std::string &path, std::string &out) {
    SceUID fd = sceIoOpen(path.c_str(), SCE_O_RDONLY, 0);
    if (fd < 0) return false;
    SceOff size = sceIoLseek(fd, 0, SCE_SEEK_END);
    sceIoLseek(fd, 0, SCE_SEEK_SET);
    out.resize((size_t)size);
    size_t done = 0;
    while (done < out.size()) {
        int r = sceIoRead(fd, &out[done], (SceSize)(out.size() - done));
        if (r <= 0) break;
        done += (size_t)r;
    }
    sceIoClose(fd);
    return done == out.size();
}
}  // namespace

void init() {
    // Max clocks: the A9 cores at 444 MHz, GPU at 222 MHz.
    scePowerSetArmClockFrequency(444);
    scePowerSetBusClockFrequency(222);
    scePowerSetGpuClockFrequency(222);
    scePowerSetGpuXbarClockFrequency(166);
    log("clocks: arm %d bus %d gpu %d xbar %d", scePowerGetArmClockFrequency(), scePowerGetBusClockFrequency(),
        scePowerGetGpuClockFrequency(), scePowerGetGpuXbarClockFrequency());
    sceCtrlSetSamplingMode(SCE_CTRL_MODE_ANALOG_WIDE);
    sceTouchSetSamplingState(SCE_TOUCH_PORT_FRONT, SCE_TOUCH_SAMPLING_STATE_START);
    sceIoMkdir("ux0:data", 0777);
    sceIoMkdir("ux0:data/Vitalatro", 0777);
}

bool read_file(const char *path, std::string &out) { return read_whole(std::string(kDataRoot) + path, out); }
bool read_save(const char *path, std::string &out) { return read_whole(std::string(kSaveRoot) + path, out); }

bool write_save(const char *path, const std::string &data) {
    std::string full = std::string(kSaveRoot) + path;
    std::string tmp = full + ".tmp";
    SceUID fd = sceIoOpen(tmp.c_str(), SCE_O_WRONLY | SCE_O_CREAT | SCE_O_TRUNC, 0777);
    if (fd < 0) return false;
    int w = sceIoWrite(fd, data.data(), (SceSize)data.size());
    sceIoClose(fd);
    if (w != (int)data.size()) return false;
    sceIoRemove(full.c_str());
    return sceIoRename(tmp.c_str(), full.c_str()) >= 0;
}

void log(const char *fmt, ...) {
    char buf[1024];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);
    SceUID fd = sceIoOpen("ux0:data/Vitalatro/log.txt", SCE_O_WRONLY | SCE_O_CREAT | SCE_O_APPEND, 0777);
    if (fd >= 0) {
        sceIoWrite(fd, buf, (SceSize)strlen(buf));
        sceIoWrite(fd, "\n", 1);
        sceIoClose(fd);
    }
}

void fatal(const char *fmt, ...) {
    char buf[1024];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);
    log("FATAL: %s", buf);
    sceKernelExitProcess(1);
    for (;;) {}
}

void poll_input(Input &in) {
    SceCtrlData pad{};
    sceCtrlPeekBufferPositive(0, &pad, 1);
    in.buttons = 0;
    auto map = [&](unsigned sce, uint32_t b) { if (pad.buttons & sce) in.buttons |= b; };
    map(SCE_CTRL_UP, BTN_UP); map(SCE_CTRL_DOWN, BTN_DOWN);
    map(SCE_CTRL_LEFT, BTN_LEFT); map(SCE_CTRL_RIGHT, BTN_RIGHT);
    map(SCE_CTRL_CROSS, BTN_CROSS); map(SCE_CTRL_CIRCLE, BTN_CIRCLE);
    map(SCE_CTRL_SQUARE, BTN_SQUARE); map(SCE_CTRL_TRIANGLE, BTN_TRIANGLE);
    map(SCE_CTRL_LTRIGGER, BTN_L); map(SCE_CTRL_RTRIGGER, BTN_R);
    map(SCE_CTRL_START, BTN_START); map(SCE_CTRL_SELECT, BTN_SELECT);
    in.lx = (pad.lx - 128) / 128.0f;
    in.ly = (pad.ly - 128) / 128.0f;
    in.rx = (pad.rx - 128) / 128.0f;
    in.ry = (pad.ry - 128) / 128.0f;
    SceTouchData touch{};
    sceTouchPeek(SCE_TOUCH_PORT_FRONT, &touch, 1);
    in.touching = touch.reportNum > 0;
    if (in.touching) {
        // front panel reports 1920x1088
        in.touch_x = touch.report[0].x * 0.5f;
        in.touch_y = touch.report[0].y * 0.5f;
    }
}

}  // namespace platform
}  // namespace vl

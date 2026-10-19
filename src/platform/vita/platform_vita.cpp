#include <psp2/ctrl.h>
#include <psp2/io/fcntl.h>
#include <psp2/io/stat.h>
#include <psp2/kernel/processmgr.h>
#include <psp2/kernel/threadmgr.h>
#include <psp2/kernel/sysmem.h>
#include <psp2/power.h>
#include <psp2/touch.h>

#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <string>

#include "core/time.h"
#include "platform/platform.h"

// Newlib heap size.
extern "C" {
// 96 MB: the game data needs far less, and the rest of the user memory backs GPU allocations
// (textures and fonts fall back to it when CDRAM is full)
int _newlib_heap_size_user = 96 * 1024 * 1024;
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

namespace {
void log_start();
}

void init() {
    sceIoMkdir("ux0:data", 0777);
    sceIoMkdir("ux0:data/Vitalatro", 0777);
    log_start();
    // Max clocks: the A9 cores at 444 MHz, GPU at 222 MHz.
    scePowerSetArmClockFrequency(444);
    scePowerSetBusClockFrequency(222);
    scePowerSetGpuClockFrequency(222);
    scePowerSetGpuXbarClockFrequency(166);
    log("clocks: arm %d bus %d gpu %d xbar %d", scePowerGetArmClockFrequency(), scePowerGetBusClockFrequency(),
        scePowerGetGpuClockFrequency(), scePowerGetGpuXbarClockFrequency());
    sceCtrlSetSamplingMode(SCE_CTRL_MODE_ANALOG_WIDE);
    sceTouchSetSamplingState(SCE_TOUCH_PORT_FRONT, SCE_TOUCH_SAMPLING_STATE_START);
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

bool remove_save(const char *path) { return sceIoRemove((std::string(kSaveRoot) + path).c_str()) >= 0; }

void make_save_dir(const char *name) { sceIoMkdir((std::string(kSaveRoot) + name).c_str(), 0777); }

namespace {
// Log lines are appended to the memory card by a worker thread: opening and writing the file costs
// tens of milliseconds, which showed up as frame spikes.
SceKernelLwMutexWork log_mutex;
SceUID log_sema = -1;
std::string log_pending;

void log_write(const std::string &text) {
    SceUID fd = sceIoOpen("ux0:data/Vitalatro/log.txt", SCE_O_WRONLY | SCE_O_CREAT | SCE_O_APPEND, 0777);
    if (fd >= 0) {
        sceIoWrite(fd, text.data(), (SceSize)text.size());
        sceIoClose(fd);
    }
}

std::string log_take() {
    sceKernelLockLwMutex(&log_mutex, 1, nullptr);
    std::string text;
    text.swap(log_pending);
    sceKernelUnlockLwMutex(&log_mutex, 1);
    return text;
}

int log_thread(SceSize, void *) {
    for (;;) {
        sceKernelWaitSema(log_sema, 1, nullptr);
        std::string text = log_take();
        if (!text.empty()) log_write(text);
    }
    return 0;
}

void log_start() {
    sceKernelCreateLwMutex(&log_mutex, "vl_log", 0, 0, nullptr);
    log_sema = sceKernelCreateSema("vl_log", 0, 0, 1 << 20, nullptr);
    SceUID t = sceKernelCreateThread("vl_log", log_thread, 0x10000100 + 30, 16 * 1024, 0, 0x20000 | 0x40000, nullptr);
    if (log_sema < 0 || t < 0 || sceKernelStartThread(t, 0, nullptr) < 0) log_sema = -1;  // write inline
}
}  // namespace

void log(const char *fmt, ...) {
    char buf[1024];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);
    if (log_sema < 0) {
        log_write(std::string(buf) + "\n");
        return;
    }
    sceKernelLockLwMutex(&log_mutex, 1, nullptr);
    log_pending += buf;
    log_pending += '\n';
    sceKernelUnlockLwMutex(&log_mutex, 1);
    sceKernelSignalSema(log_sema, 1);
}

void fatal(const char *fmt, ...) {
    char buf[1024];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);
    std::string text = log_sema >= 0 ? log_take() : std::string();
    log_write(text + "FATAL: " + buf + "\n");
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

namespace {
int async_entry(SceSize, void *argp) {
    auto *job = *(std::function<void()> **)argp;
    (*job)();
    delete job;
    return sceKernelExitDeleteThread(0);
}
}  // namespace

void run_async(std::function<void()> job) {
    auto *heap_job = new std::function<void()>(std::move(job));
    // lower priority than the game thread, 256 KB stack, any of the user cores
    // on the two cores the game thread does not use (it runs on core 0)
    SceUID t = sceKernelCreateThread("vl_worker", async_entry, 0x10000100 + 20, 256 * 1024, 0, 0x20000 | 0x40000, nullptr);
    if (t < 0 || sceKernelStartThread(t, sizeof heap_job, &heap_job) < 0) {
        log("run_async: no worker thread (0x%08x), running inline", (unsigned)t);
        if (t >= 0) sceKernelDeleteThread(t);
        (*heap_job)();
        delete heap_job;
    }
}

void run_thread(std::function<void()> loop) {
    auto *heap_job = new std::function<void()>(std::move(loop));
    SceUID t = sceKernelCreateThread("vl_loop", async_entry, 0x10000100 + 20, 256 * 1024, 0, 0x20000 | 0x40000, nullptr);
    if (t < 0 || sceKernelStartThread(t, sizeof heap_job, &heap_job) < 0) fatal("run_thread: no thread (0x%08x)", (unsigned)t);
}

Mutex::Mutex() {
    auto *w = new SceKernelLwMutexWork;
    sceKernelCreateLwMutex(w, "vl_mutex", 0, 0, nullptr);
    h_ = w;
}
Mutex::~Mutex() {
    sceKernelDeleteLwMutex((SceKernelLwMutexWork *)h_);
    delete (SceKernelLwMutexWork *)h_;
}
void Mutex::lock() { sceKernelLockLwMutex((SceKernelLwMutexWork *)h_, 1, nullptr); }
void Mutex::unlock() { sceKernelUnlockLwMutex((SceKernelLwMutexWork *)h_, 1); }

Semaphore::Semaphore() { h_ = (void *)(intptr_t)sceKernelCreateSema("vl_sema", 0, 0, 1 << 20, nullptr); }
Semaphore::~Semaphore() { sceKernelDeleteSema((SceUID)(intptr_t)h_); }
void Semaphore::post() { sceKernelSignalSema((SceUID)(intptr_t)h_, 1); }
void Semaphore::wait() { sceKernelWaitSema((SceUID)(intptr_t)h_, 1, nullptr); }
bool Semaphore::try_wait() { return sceKernelPollSema((SceUID)(intptr_t)h_, 1) == 0; }

void free_memory_mb(int &user, int &cdram) {
    SceKernelFreeMemorySizeInfo info{};
    info.size = sizeof info;
    if (sceKernelGetFreeMemorySize(&info) < 0) {
        user = cdram = -1;
        return;
    }
    user = info.size_user >> 20;
    cdram = info.size_cdram >> 20;
}

}  // namespace platform
}  // namespace vl

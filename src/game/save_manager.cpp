// engine/string_packer.lua + engine/save_manager.lua
#include "game/save_manager.h"

#include <cstdlib>
#include <deque>

#include "platform/platform.h"
#include "stb/stb_image.h"
#include "core/stb_extra.h"
#include "stb/stb_image_write.h"

namespace vl {

namespace {
// love.data.compress('string', 'deflate', s, 1): a raw DEFLATE stream. stb writes a zlib stream:
// strip its 2-byte header and 4-byte Adler-32 trailer.
std::string deflate(const std::string &s) {
    int len = 0;
    unsigned char *z = stbi_zlib_compress((unsigned char *)s.data(), (int)s.size(), &len, 1);
    if (!z || len < 6) return std::string();
    std::string out((const char *)z + 2, (size_t)len - 6);
    std::free(z);
    return out;
}

bool inflate(const std::string &s, std::string &out) {
    int len = 0;
    char *d = stbi_zlib_decode_noheader_malloc(s.data(), (int)s.size(), &len);
    if (!d) return false;
    out.assign(d, (size_t)len);
    std::free(d);
    return true;
}

// ------------------------------------------------------------------ the save thread
struct Job {
    enum Kind { Write, Notify } kind;
    std::string path;  // relative to the save root
    std::string text;  // packed table (Write) or the unlock key (Notify)
    std::string dir;   // profile folder to create first, if any
};
// never destroyed: the thread still waits on them while the program exits
platform::Mutex &mutex = *new platform::Mutex;
platform::Semaphore &pending = *new platform::Semaphore;
std::deque<Job> jobs;
bool started = false;

void run(const Job &j) {
    if (!j.dir.empty()) platform::make_save_dir(j.dir.c_str());
    if (j.kind == Job::Write) {
        if (!platform::write_save(j.path.c_str(), deflate(j.text))) platform::log("save: could not write %s", j.path.c_str());
        return;
    }
    // save_notify: append the key to the profile's unlock_notify.jkr unless it is already there
    std::string notify;
    get_compressed(j.path, notify);
    if (notify.find(j.text) == std::string::npos) platform::write_save(j.path.c_str(), deflate(notify + j.text + "\n"));
}

void push(Job j) {
    mutex.lock();
    jobs.push_back(std::move(j));
    bool start = !started;
    started = true;
    mutex.unlock();
    pending.post();
    if (start)
        platform::run_thread([] {
            for (;;) {
                pending.wait();
                mutex.lock();
                Job job = std::move(jobs.front());
                jobs.pop_front();
                mutex.unlock();
                run(job);
            }
        });
}

std::string packed(const Value &v) { return v.is_table() ? str_pack(*v.table()) : v.to_string(); }
}  // namespace

bool get_compressed(const std::string &file, std::string &out) {
    std::string raw;
    if (!platform::read_save(file.c_str(), raw) || raw.empty()) return false;
    if (raw.compare(0, 6, "return") == 0) {
        out = std::move(raw);
        return true;
    }
    return inflate(raw, out);
}

Value load_local_file(const std::string &file) {
    if (file.find("unlock_notify") != std::string::npos) return Value();
    std::string text;
    if (!get_compressed(file, text)) return Value();
    std::string err;
    Value v = parse_lua_literal(text, &err);
    if (!v.is_table()) platform::log("save: could not read %s: %s", file.c_str(), err.c_str());
    return v;
}

void save_manager_request(const TableRef &r) {
    Value type = r->get(K("type"));
    auto profile_dir = [&](const Value &n) { return (n.truthy() ? n.to_string() : std::string("1")); };
    if (type.equals("save_progress")) {
        Value sp = r->get(K("save_progress"));
        std::string prof = profile_dir(sp["SETTINGS"]["profile"]);
        push({Job::Write, prof + "/meta.jkr", packed(sp["META"]), prof});
        push({Job::Write, "settings.jkr", packed(sp["SETTINGS"]), ""});
        push({Job::Write, prof + "/profile.jkr", packed(sp["PROFILE"]), prof});
    } else if (type.equals("empty_profile")) {
        std::string prof = profile_dir(r->get(K("profile_num")));
        push({Job::Write, "settings.jkr", packed(r->get(K("save_settings"))), ""});
        push({Job::Write, prof + "/profile.jkr", packed(r->get(K("save_profile"))), prof});
    } else if (type.equals("save_settings")) {
        std::string prof = profile_dir(r->get(K("profile_num")));
        push({Job::Write, "settings.jkr", packed(r->get(K("save_settings"))), ""});
        push({Job::Write, prof + "/profile.jkr", packed(r->get(K("save_profile"))), prof});
    } else if (type.equals("save_notify")) {
        std::string prof = profile_dir(r->get(K("profile_num")));
        Value key = r->get(K("save_notify"));
        if (key.truthy()) push({Job::Notify, prof + "/unlock_notify.jkr", key.to_string(), prof});
    } else if (type.equals("save_run")) {
        std::string prof = profile_dir(r->get(K("profile_num")));
        push({Job::Write, prof + "/save.jkr", packed(r->get(K("save_table"))), prof});
    }
}

}  // namespace vl

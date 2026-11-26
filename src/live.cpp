// Live mode: minimal HTTP/1.1 server on 127.0.0.1 (Winsock, one thread per
// connection) + latest-wins state published by the simulation thread.
// Endpoints: / (page) /stats (JSON) /frame.bin /eye.bin /layout.bin
// /pipeline (JSON) /video (Range) /ctl (POST).
#include "live.h"
#include "csv.h"
#include "video.h"
#include "live_page.h"

#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

struct LiveServer::Imp {
    std::mutex mtx;                      // guards everything published
    std::string stats;                   // ready JSON for /stats
    std::vector<uint8_t> activity;       // per-neuron rate*255
    std::vector<uint8_t> eye_photo, eye_lam, eye_feat;  // 768 each
    double sim_t = 0;
    int64_t sim_frame = 0;
    int64_t top_tick = -1;               // top20 refreshed every 5th publish

    size_t N = 0;
    std::string video_path;
    bool replay = false;
    std::vector<uint16_t> px_, py_;
    std::vector<uint8_t> grp_;
    std::vector<uint32_t> root_ids_, type_idx_, class_idx_;
    std::vector<uint8_t> nt_;
    std::vector<std::string> strings_;
    std::string groups_json, pipeline_json;
    std::vector<uint32_t> top_idx_;
    std::string top20_json = "[]";

    std::atomic<bool> pause_{false};
    std::atomic<double> speed_{1.0};     // live default 1.0x; 0 = max
    std::atomic<bool> restart_{false};
    std::mutex ctl_mtx;
    std::condition_variable url_cv_;
    std::string lesion, new_url;

    std::atomic<bool> running_{false};
    SOCKET listen_sock = INVALID_SOCKET;
    int port = 0;

    // ---- replay mode: metrics CSV rows served by /stats?i=N ----
    std::vector<std::vector<double>> rc;      // columns by name in ridx
    std::vector<std::string> rlesion;
    std::vector<Metrics::Event> revents;      // re-detected at 1.5 s refractory
    int64_t rrows = 0;
    int ridx[24];                             // csv column per metric name
};

LiveServer::~LiveServer() { stop(); }

// ---------------- layout ----------------
static const struct { SUP sup; const char* name; } GROUPS[] = {
    { SUP_OL_SENSORY, "фоторецепторы" },        { SUP_OL_INTRINSIC, "зрительная система (OL)" },
    { SUP_VISUAL_PROJECTION, "визуальные проекц." }, { SUP_VISUAL_CENTRIFUGAL, "визуальные центробеж." },
    { SUP_CB_SENSORY, "сенсорные (антенны)" },  { SUP_CB_INTRINSIC, "центральный мозг" },
    { SUP_CB_ENDOCRINE, "эндокринные" },        { SUP_DESCENDING, "нисходящие (DN)" },
    { SUP_ASCENDING, "восходящие" },            { SUP_SENSORY_ASCENDING, "восходящие сенсор." },
    { SUP_VNC_SENSORY, "VNC сенсорные" },       { SUP_VNC_INTRINSIC, "VNC интернейроны" },
    { SUP_VNC_MOTOR, "моторные (VNC)" },        { SUP_VNC_EFFERENT, "VNC эфферентные" },
    { SUP_CB_MOTOR, "прочие (CB motor и др.)" },
};
static const int NGROUPS = (int)(sizeof(GROUPS) / sizeof(GROUPS[0]));

void LiveServer::init(const Connectome& C, bool replay_mode) {
    if (!imp_) imp_ = new Imp();
    Imp& im = *imp_;
    im.replay = replay_mode;
    im.N = C.n();
    im.activity.assign(im.N, 0);
    im.eye_photo.assign(768, 0);
    im.eye_lam.assign(768, 0);
    im.eye_feat.assign(768, 0);
    im.px_.resize(im.N);
    im.py_.resize(im.N);
    im.grp_.assign(im.N, 0);
    im.root_ids_.resize(im.N);
    im.type_idx_.resize(im.N);
    im.class_idx_.resize(im.N);
    im.nt_.resize(im.N);
    im.top_idx_.resize(im.N);
    for (size_t i = 0; i < im.N; ++i) im.top_idx_[i] = (uint32_t)i;

    int cnt[NGROUPS] = {0};
    for (size_t i = 0; i < im.N; ++i) {
        int g = NGROUPS - 1;
        for (int k = 0; k < NGROUPS - 1; ++k)
            if (C.neurons[i].sup == GROUPS[k].sup) { g = k; break; }
        im.grp_[i] = (uint8_t)g;
        ++cnt[g];
    }
    // stack blocks in a 408-wide field; identical geometry computed twice is
    // avoided by storing rects from the first pass
    const int W = 408;
    // dim base color per group (map is visible even at rest)
    static const int GC[NGROUPS][3] = {
        {120, 40, 40}, {40, 100, 60}, {40, 90, 110}, {60, 70, 110}, {110, 70, 40},
        {70, 70, 90}, {90, 50, 90}, {130, 45, 45}, {45, 95, 45}, {95, 75, 35},
        {45, 85, 85}, {60, 60, 85}, {85, 90, 40}, {70, 45, 75}, {75, 75, 75},
    };
    int x = 2, y = 2;
    int bx[NGROUPS], by[NGROUPS], bw[NGROUPS], bh[NGROUPS];
    std::string gj = "[";
    for (int g = 0; g < NGROUPS; ++g) {
        int n = cnt[g];
        if (n > 0) {
            int cols = (int)std::max(1.0, std::sqrt((double)n * W / 200.0));
            bw[g] = cols;
            bh[g] = (n + cols - 1) / cols;
            if (x + bw[g] > W - 2) { x = 2; y += bh[g] + 12; }
        } else { bw[g] = 0; bh[g] = 0; }
        bx[g] = x; by[g] = y;
        char b[224];
        snprintf(b, sizeof b,
                 "%s{\"name\":\"%s\",\"x\":%d,\"y\":%d,\"w\":%d,\"h\":%d,\"n\":%d,"
                 "\"c\":\"rgb(%d,%d,%d)\"}",
                 g ? "," : "", GROUPS[g].name, x, y, bw[g], bh[g], n,
                 GC[g][0], GC[g][1], GC[g][2]);
        gj += b;
        x += bw[g] + 8;
        if (x > W - 40) { x = 2; y += bh[g] + 12; }
    }
    gj += "]";
    im.groups_json = gj;
    int head[NGROUPS] = {0};
    for (size_t i = 0; i < im.N; ++i) {
        int g = im.grp_[i];
        if (bw[g] == 0) continue;
        int k = head[g]++;
        im.px_[i] = (uint16_t)(bx[g] + (k % bw[g]));
        im.py_[i] = (uint16_t)(by[g] + (k / bw[g]));
    }

    im.strings_.clear();
    auto intern = [&](const std::string& s) {
        for (size_t k = 0; k < im.strings_.size(); ++k)
            if (im.strings_[k] == s) return (uint32_t)k;
        im.strings_.push_back(s);
        return (uint32_t)(im.strings_.size() - 1);
    };
    for (size_t i = 0; i < im.N; ++i) {
        const Neuron& nn = C.neurons[i];
        im.root_ids_[i] = nn.id;
        im.type_idx_[i] = intern(C.s(nn.cell_type));
        // top-20 class fallback: Class, else Super Class, else Sub Class
        im.class_idx_[i] = !C.s(nn.cls).empty() ? intern(C.s(nn.cls))
                         : !C.s(nn.sub_class).empty() ? intern(C.s(nn.sub_class))
                         : intern(GROUPS[im.grp_[i]].name);
        im.nt_[i] = nn.nt < 9 ? nn.nt : 0;
    }

    // pipeline edge weights: total synapses between key populations (static)
    auto gsum = [&](const std::vector<uint32_t>& a, const std::vector<uint32_t>& b) {
        std::vector<uint8_t> ina(C.n(), 0);
        for (uint32_t i : a) ina[i] = 1;
        uint64_t s = 0;
        for (uint32_t pb : b)
            for (uint32_t k = C.in_off[pb]; k < C.in_off[pb + 1]; ++k)
                if (ina[C.edges[k].pre]) s += (uint64_t)C.edges[k].w;
        return s;
    };
    char b[512];
    snprintf(b, sizeof b,
             "{\"edges\":[{\"a\":0,\"b\":1,\"w\":%llu},{\"a\":1,\"b\":2,\"w\":%llu},"
             "{\"a\":2,\"b\":3,\"w\":%llu},{\"a\":3,\"b\":4,\"w\":%llu},{\"a\":4,\"b\":5,\"w\":%llu}]}",
             (unsigned long long)gsum(C.photos, C.lamina),
             (unsigned long long)gsum(C.lamina, C.t4t5),
             (unsigned long long)gsum(C.t4t5, C.lc_loom),
             (unsigned long long)gsum(C.lc_loom, C.gf),
             (unsigned long long)gsum(C.gf, C.motor));
    im.pipeline_json = b;
}

void LiveServer::set_video_path(const std::string& path) {
    Imp& im = *imp_;
    std::lock_guard<std::mutex> lk(im.mtx);
    im.video_path = path;
}

// ---------------- replay CSV ----------------
// metric-key -> csv column name (index 3 is an unused slot)
static const char* RK_COL[24] = {
    "t", "gf", "lc4", nullptr, "escape_drive", "escapes_cum", "satisfaction", "behavior",
    "motion", "transient", "contrast", "photo", "lamina", "t4t5", "hsvs", "mbon",
    "da", "oct", "ser", "dnleg", "dntct", "dnall", "motor", "all"
};

void LiveServer::init_replay_csv(const char* csv_path, std::string* err) {
    *err = "";
    Imp& im = *imp_;
    std::vector<char> buf;
    if (!csv::read_file(csv_path, buf)) { *err = "cannot open csv"; return; }
    char* p = buf.data();
    csv::Field f[32];
    csv::split_line(&p, f, 32);
    for (int k = 0; k < 24; ++k) im.ridx[k] = -1;
    for (size_t i = 0; i < 26 && f[i].p; ++i)
        for (int k = 0; k < 24; ++k)
            if (RK_COL[k] && f[i].is(RK_COL[k])) { im.ridx[k] = (int)i; break; }
    if (im.ridx[0] < 0) { *err = "csv lacks t column"; return; }
    im.rc.assign(24, {});
    std::vector<double> ts, gfs, lcs;
    while (*p) {
        size_t nf = csv::split_line(&p, f, 32);
        if (nf < 7) continue;
        for (int k = 0; k < 24; ++k)
            if (im.ridx[k] >= 0 && (size_t)im.ridx[k] < nf)
                im.rc[k].push_back(f[im.ridx[k]].to_double());
        ts.push_back(f[im.ridx[0]].to_double());
        gfs.push_back(im.ridx[1] >= 0 ? f[im.ridx[1]].to_double() : 0.0);
        lcs.push_back(im.ridx[2] >= 0 ? f[im.ridx[2]].to_double() : 0.0);
        ++im.rrows;
    }
    im.revents = detect_escapes(ts, gfs, lcs, 1.5);
    printf("[replay] %lld rows, %zu events (refractory 1.5 s)\n",
           (long long)im.rrows, im.revents.size());
}

// One CSV row rendered in the same JSON format as live /stats.
std::string LiveServer::replay_stats(int64_t i) {
    Imp& im = *imp_;
    if (im.rrows == 0) return "{\"t\":0,\"replay\":true}";
    if (i < 0) i = 0;
    if (i >= im.rrows) i = im.rrows - 1;
    auto col = [&](int k) {
        return im.ridx[k] >= 0 && (size_t)i < im.rc[k].size() ? im.rc[k][(size_t)i] : 0.0;
    };
    double t = col(0);
    // events up to this row (same detector as the log => regression check holds)
    std::string ev;
    int64_t esc = 0;
    for (const Metrics::Event& e : im.revents)
        if (e.t <= t + 1e-9) {
            char b[96];
            snprintf(b, sizeof b, "%s[%.2f,%.4f,%.4f]", esc ? "," : "", e.t, e.gf, e.lc4);
            ev += b;
            ++esc;
        }
    char sj[1024];
    snprintf(sj, sizeof sj,
        "{\"t\":%.2f,\"frame\":%lld,\"speed\":0,\"escapes\":%lld,\"sat\":%.3f,\"beh\":%d,"
        "\"gf\":%.4f,\"lc4\":%.4f,\"photo\":%.4f,\"lam\":%.4f,\"t4t5\":%.4f,\"mbon\":%.4f,"
        "\"da\":%.4f,\"oct\":%.4f,\"ser\":%.4f,\"dnleg\":%.4f,\"dntct\":%.4f,\"motor\":%.4f,"
        "\"all\":%.4f,\"motion\":%.4f,\"transient\":%.4f,\"contrast\":%.4f,"
        "\"active\":%lld,\"threads\":0,\"lesion\":\"\",\"paused\":false,\"replay\":true,"
        "\"events\":[%s],\"top20\":[]}",
        t, (long long)i, (long long)esc, col(6), (int)(col(7) + 0.5),
        col(1), col(2), col(11), col(12), col(13), col(15),
        col(16), col(17), col(18), col(19), col(20), col(22), col(23),
        col(8), col(9), col(10),
        (long long)(col(21) * 1000), ev.c_str());
    return sj;
}

// Minimal JSON string escape: quotes, backslashes and control characters.
static std::string json_escape(const std::string& s) {
    std::string o;
    o.reserve(s.size() + 8);
    for (unsigned char c : s) {
        switch (c) {
            case '"':  o += "\\\""; break;
            case '\\': o += "\\\\"; break;
            case '\n': o += "\\n"; break;
            case '\r': o += "\\r"; break;
            case '\t': o += "\\t"; break;
            default:
                if (c < 0x20) {
                    char b[7];
                    snprintf(b, sizeof b, "\\u%04x", c);
                    o += b;
                } else o += (char)c;
        }
    }
    return o;
}

// ---------------- publish (sim thread) ----------------
void LiveServer::publish(const Brain& B, const Metrics& M, const FlyEye& eye,
                         double sim_t, int64_t frame, double speed_x) {
    Imp& im = *imp_;
    // try_lock: if a connection thread happens to hold the state mutex, this
    // frame is simply dropped — the simulation NEVER waits on the UI server.
    std::unique_lock<std::mutex> lk(im.mtx, std::try_to_lock);
    if (!lk.owns_lock()) return;
    im.sim_t = sim_t;
    im.sim_frame = frame;
    int64_t active = 0;
    for (size_t i = 0; i < im.N; ++i) {
        float r = B.rate[i];
        im.activity[i] = (uint8_t)(r * 255.0f + 0.5f);
        if (r > 0.5f) ++active;
    }
    const float* dp = eye.drv_photo();
    const float* dl = eye.drv_lam();
    const float* dm = eye.feat_motion();
    for (int p = 0; p < eye.positions() && p < 768; ++p) {
        im.eye_photo[p] = (uint8_t)(dp[p] * 255.0f + 0.5f);
        im.eye_lam[p] = (uint8_t)(dl[p] * 255.0f + 0.5f);
        im.eye_feat[p] = (uint8_t)(dm[p] * 127.0f + 0.5f);
    }
    // top-20 every 5th publish (~4 Hz)
    if (++im.top_tick % 5 == 0) {
        std::partial_sort(im.top_idx_.begin(), im.top_idx_.begin() + 20, im.top_idx_.end(),
                          [&](uint32_t a, uint32_t b) { return B.rate[a] > B.rate[b]; });
        std::string tj = "[";
        char e[512];
        for (int k = 0; k < 20; ++k) {
            uint32_t i = im.top_idx_[k];
            std::string typ = json_escape(im.strings_[im.type_idx_[i]]);
            std::string cls = json_escape(im.strings_[im.class_idx_[i]]);
            snprintf(e, sizeof e, "%s[%u,\"%s\",\"%s\",%.3f]", k ? "," : "",
                     im.root_ids_[i], typ.c_str(), cls.c_str(), B.rate[i]);
            tj += e;
        }
        tj += "]";
        im.top20_json = tj;
    }
    char sj[4096];
    DlStatus dls = dl_status();
    std::string lesion;  // snapshot under ctl_mtx: set_lesion writes it from /ctl
    { std::lock_guard<std::mutex> lk2(im.ctl_mtx); lesion = im.lesion; }
    lesion = json_escape(lesion);
    std::string dl_stage = json_escape(dls.stage), dl_err = json_escape(dls.error);
    snprintf(sj, sizeof sj,
        "{\"t\":%.2f,\"frame\":%lld,\"speed\":%.2f,\"escapes\":%lld,\"sat\":%.3f,\"beh\":%d,"
        "\"gf\":%.4f,\"lc4\":%.4f,\"photo\":%.4f,\"lam\":%.4f,\"t4t5\":%.4f,\"mbon\":%.4f,"
        "\"da\":%.4f,\"oct\":%.4f,\"ser\":%.4f,\"dnleg\":%.4f,\"dntct\":%.4f,\"motor\":%.4f,"
        "\"all\":%.4f,\"motion\":%.4f,\"transient\":%.4f,\"contrast\":%.4f,"
        "\"active\":%lld,\"threads\":%d,\"lesion\":\"%s\",\"paused\":%s,\"replay\":%s,"
        "\"dl\":{\"stage\":\"%s\",\"pct\":%.1f,\"mbs\":%.2f,\"eta\":%.0f,\"err\":\"%s\"},"
        "\"events\":[",
        sim_t, (long long)frame, speed_x, (long long)M.escapes_total, M.satisfaction, M.behavior,
        M.gf, M.lc4, M.m_photo, M.m_lam, M.m_t4t5, M.m_mbon,
        M.m_da, M.m_oct, M.m_ser, M.m_dnleg, M.m_dntct, M.m_motor, M.m_all,
        eye.motion_mean(), eye.transient_mean(), eye.contrast_mean(),
        (long long)active, B.threads, lesion.c_str(),
        im.pause_.load() ? "true" : "false", im.replay ? "true" : "false",
        dl_stage.c_str(), dls.percent, dls.speed_mbs, dls.eta_s, dl_err.c_str());
    std::string sj2 = sj;
    size_t n0 = M.events.size();
    for (size_t k = n0 > 10 ? n0 - 10 : 0; k < n0; ++k) {
        char e[96];
        snprintf(e, sizeof e, "%s[%.2f,%.4f,%.4f]", k == (n0 > 10 ? n0 - 10 : 0) ? "" : ",",
                 M.events[k].t, M.events[k].gf, M.events[k].lc4);
        sj2 += e;
    }
    sj2 += "],\"top20\":" + im.top20_json + "}";
    im.stats = sj2;
}

// ---------------- control ----------------
bool LiveServer::is_paused() const { return imp_->pause_.load(); }
void LiveServer::set_paused(bool p) { imp_->pause_.store(p); }
double LiveServer::speed_target() const { return imp_->speed_.load(); }
void LiveServer::set_speed(double x) { imp_->speed_.store(x < 0 ? 0.0 : x); }
bool LiveServer::restart_requested() { return imp_->restart_.exchange(false); }
void LiveServer::request_restart() { imp_->restart_.store(true); }
std::string LiveServer::lesion_group() const {
    std::lock_guard<std::mutex> lk(imp_->ctl_mtx);
    return imp_->lesion;
}
void LiveServer::set_lesion(const std::string& g) {
    std::lock_guard<std::mutex> lk(imp_->ctl_mtx);
    imp_->lesion = g;
}
std::string LiveServer::take_new_url() {
    std::lock_guard<std::mutex> lk(imp_->ctl_mtx);
    std::string u = imp_->new_url;
    imp_->new_url.clear();
    return u;
}
void LiveServer::take_new_url_wait(std::string& out, double timeout_s) {
    // wake early when a URL arrives (cv) or on timeout
    std::unique_lock<std::mutex> lk(imp_->ctl_mtx);
    imp_->url_cv_.wait_for(lk, std::chrono::duration<double>(timeout_s),
                           [&] { return !imp_->new_url.empty() || imp_->restart_.load(); });
    out = imp_->new_url;
    imp_->new_url.clear();
}
int LiveServer::port() const { return imp_->port; }

// ---------------- HTTP core ----------------
static void send_all(SOCKET s, const char* p, size_t n) {
    while (n > 0) {
        int k = send(s, p, (int)n, 0);
        if (k <= 0) return;
        p += k;
        n -= (size_t)k;
    }
}
static void resp_raw(SOCKET s, const char* code, const char* ctype,
                     const char* body, size_t blen, const char* extra = "") {
    char h[512];
    int hl = snprintf(h, sizeof h,
                      "HTTP/1.1 %s\r\nContent-Type: %s\r\nContent-Length: %llu\r\n"
                      "Cache-Control: no-store\r\nConnection: close\r\n%s\r\n",
                      code, ctype, (unsigned long long)blen, extra);
    send_all(s, h, (size_t)hl);
    if (body) send_all(s, body, blen);
}

static void serve_video(SOCKET s, const std::string& path, const std::string& rng, bool head_only) {
    FILE* f = fopen(path.c_str(), "rb");
    if (!f) { resp_raw(s, "404 Not Found", "text/plain", "video not found", 15); return; }
    fseek(f, 0, SEEK_END);
    long long size = _ftelli64(f);
    long long s0 = 0, s1 = size - 1;
    bool range = rng.rfind("bytes=", 0) == 0;
    if (range) {
        long long a = -1, b = -1;
        if (rng.size() > 7 && rng[6] == '-') {          // "bytes=-S": last S bytes
            long long sfx = atoll(rng.c_str() + 7);
            s0 = (sfx <= 0 || sfx >= size) ? 0 : size - sfx;
            s1 = size - 1;
        } else if (sscanf(rng.c_str(), "bytes=%lld-%lld", &a, &b) >= 1 && a >= 0) {
            s0 = a;                                      // "bytes=N-" or "bytes=N-M"
            if (b >= 0 && b < size) s1 = b;
        }
        if (s0 > s1 || s0 >= size) {         // unsatisfiable
            fclose(f);
            char h[256];
            int hl = snprintf(h, sizeof h,
                "HTTP/1.1 416 Range Not Satisfiable\r\nContent-Range: bytes */%lld\r\n"
                "Content-Length: 0\r\nConnection: close\r\n\r\n", size);
            send_all(s, h, (size_t)hl);
            return;
        }
    }
    long long clen = range ? (s1 - s0 + 1) : size;
    char h[640];
    int hl;
    if (range)
        hl = snprintf(h, sizeof h,
            "HTTP/1.1 206 Partial Content\r\nContent-Type: video/mp4\r\n"
            "Content-Length: %lld\r\nContent-Range: bytes %lld-%lld/%lld\r\n"
            "Accept-Ranges: bytes\r\nConnection: close\r\n\r\n",
            clen, s0, s1, size);
    else
        hl = snprintf(h, sizeof h,
            "HTTP/1.1 200 OK\r\nContent-Type: video/mp4\r\nContent-Length: %lld\r\n"
            "Accept-Ranges: bytes\r\nConnection: close\r\n\r\n", size);
    send_all(s, h, (size_t)hl);
    if (head_only) { fclose(f); return; }
    _fseeki64(f, s0, SEEK_SET);
    std::vector<char> buf(1 << 18);
    long long left = clen;
    while (left > 0) {
        size_t want = left > (long long)buf.size() ? buf.size() : (size_t)left;
        size_t got = fread(buf.data(), 1, want, f);
        if (got == 0) break;
        size_t off = 0;
        while (off < got) {
            int k = send(s, buf.data() + off, (int)(got - off), 0);
            if (k <= 0) { fclose(f); return; }  // client went away
            off += (size_t)k;
        }
        left -= (long long)got;
    }
    fclose(f);
}

static std::string url_decode(const std::string& s) {
    std::string o;
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '%' && i + 2 < s.size()) {
            char h[3] = { s[i + 1], s[i + 2], 0 };
            o += (char)strtol(h, nullptr, 16);
            i += 2;
        } else if (s[i] == '+') o += ' ';
        else o += s[i];
    }
    return o;
}

void LiveServer::conn_loop(void* sock) {
    SOCKET cs = (SOCKET)sock;
    DWORD tmo = 5000, smo = 30000;
    setsockopt(cs, SOL_SOCKET, SO_RCVTIMEO, (const char*)&tmo, sizeof tmo);
    setsockopt(cs, SOL_SOCKET, SO_SNDTIMEO, (const char*)&smo, sizeof smo);
    std::string req;
    char buf[8192];
    while (req.find("\r\n\r\n") == std::string::npos && req.size() < 16384) {
        int k = recv(cs, buf, sizeof buf, 0);
        if (k <= 0) break;
        req.append(buf, (size_t)k);
    }
    if (req.size() < 16) { closesocket(cs); return; }
    char method[16] = {0}, path[512] = {0};
    sscanf(req.c_str(), "%15s %511s", method, path);
    std::string p = path, query;
    size_t q = p.find('?');
    if (q != std::string::npos) { query = p.substr(q + 1); p = p.substr(0, q); }
    std::string range;
    size_t rp = req.find("Range: ");
    if (rp != std::string::npos) range = req.substr(rp + 7, req.find("\r\n", rp) - rp - 7);

    Imp& im = *imp_;
    if (p == "/" || p == "/index.html") {
        resp_raw(cs, "200 OK", "text/html; charset=utf-8", LIVE_PAGE, strlen(LIVE_PAGE));
    } else if (p == "/stats") {
        std::string s;
        if (im.replay) {
            long i = 0;
            if (!query.empty()) sscanf(query.c_str(), "i=%ld", &i);
            s = replay_stats(i);
        } else {
            std::lock_guard<std::mutex> lk(im.mtx);
            s = im.stats;
        }
        if (s.empty()) {   // simulation not started yet (download/transcode phase):
            char djb[2048]; // still report the download progress for the page
            DlStatus dls = dl_status();
            std::string dl_stage = json_escape(dls.stage), dl_err = json_escape(dls.error);
            snprintf(djb, sizeof djb,
                "{\"t\":0,\"frame\":0,\"speed\":0,\"escapes\":0,\"sat\":0.5,\"beh\":0,"
                "\"gf\":0,\"lc4\":0,\"photo\":0,\"lam\":0,\"t4t5\":0,\"mbon\":0,"
                "\"da\":0,\"oct\":0,\"ser\":0,\"dnleg\":0,\"dntct\":0,\"motor\":0,"
                "\"all\":0,\"motion\":0,\"transient\":0,\"contrast\":0,"
                "\"active\":0,\"threads\":0,\"lesion\":\"\",\"paused\":false,\"replay\":false,"
                "\"dl\":{\"stage\":\"%s\",\"pct\":%.1f,\"mbs\":%.2f,\"eta\":%.0f,\"err\":\"%s\"},"
                "\"events\":[],\"top20\":[]}",
                dl_stage.c_str(), dls.percent, dls.speed_mbs, dls.eta_s, dl_err.c_str());
            s = djb;
        }
        resp_raw(cs, "200 OK", "application/json", s.data(), s.size());
    } else if (p == "/frame.bin") {
        std::vector<uint8_t> a;
        { std::lock_guard<std::mutex> lk(im.mtx); a = im.activity; }
        if (a.empty()) a.resize(4, 0);
        char h[5] = { 'F', 'L', 'Y', 'F', '1' };
        std::string out(h, 5);
        uint32_t n = (uint32_t)a.size();
        out.append((const char*)&n, 4);
        out.append((const char*)a.data(), a.size());
        resp_raw(cs, "200 OK", "application/octet-stream", out.data(), out.size());
    } else if (p == "/eye.bin") {
        std::string out = "FLYE1";
        uint32_t n = 768;
        out.append((const char*)&n, 4);
        { std::lock_guard<std::mutex> lk(im.mtx);
          out.append((const char*)im.eye_photo.data(), im.eye_photo.size());
          out.append((const char*)im.eye_lam.data(), im.eye_lam.size());
          out.append((const char*)im.eye_feat.data(), im.eye_feat.size()); }
        resp_raw(cs, "200 OK", "application/octet-stream", out.data(), out.size());
    } else if (p == "/groups") {
        std::string s;
        { std::lock_guard<std::mutex> lk(im.mtx); s = im.groups_json; }
        resp_raw(cs, "200 OK", "application/json", s.data(), s.size());
    } else if (p == "/pipeline") {
        std::string s;
        { std::lock_guard<std::mutex> lk(im.mtx); s = im.pipeline_json; }
        resp_raw(cs, "200 OK", "application/json", s.data(), s.size());
    } else if (p == "/layout.bin") {
        std::string out;
        {
            std::lock_guard<std::mutex> lk(im.mtx);
            out.reserve(64 + im.N * 20 + 4096);
            out += "FLYL1";
            uint32_t n = (uint32_t)im.N, ns = (uint32_t)im.strings_.size();
            out.append((const char*)&n, 4);
            out.append((const char*)&ns, 4);
            for (size_t i = 0; i < im.N; ++i) {
                out.append((const char*)&im.px_[i], 2);
                out.append((const char*)&im.py_[i], 2);
                out += (char)im.grp_[i];
                out.append((const char*)&im.root_ids_[i], 4);
                out.append((const char*)&im.type_idx_[i], 4);
                out.append((const char*)&im.class_idx_[i], 4);
                out += (char)im.nt_[i];
            }
            for (const std::string& s2 : im.strings_) {
                uint32_t L = (uint32_t)s2.size();
                out.append((const char*)&L, 4);
                out.append(s2);
            }
        }
        resp_raw(cs, "200 OK", "application/octet-stream", out.data(), out.size());
    } else if (p == "/video") {
        std::string vp;
        { std::lock_guard<std::mutex> lk(im.mtx); vp = im.video_path; }
        if (vp.empty()) {   // browser copy still being prepared
            char h[256];
            int hl = snprintf(h, sizeof h,
                "HTTP/1.1 503 Service Unavailable\r\nContent-Type: text/plain\r\n"
                "Content-Length: 9\r\nRetry-After: 3\r\nConnection: close\r\n\r\n");
            send_all(cs, h, (size_t)hl);
            send_all(cs, "not ready", 9);
        } else serve_video(cs, vp, range, !strcmp(method, "HEAD"));
    } else if (p == "/ctl" && !strcmp(method, "POST")) {
        size_t cl = 0, bp = req.find("\r\n\r\n");
        size_t clp = req.find("Content-Length:");
        if (clp != std::string::npos) cl = (size_t)atoi(req.c_str() + clp + 15);
        if (bp == std::string::npos) {
            resp_raw(cs, "400 Bad Request", "text/plain", "bad request", 11);
            closesocket(cs);
            return;
        }
        if (cl > (1u << 20)) {   // control bodies are tiny; cap at 1 MiB
            resp_raw(cs, "413 Payload Too Large", "text/plain", "too large", 9);
            closesocket(cs);
            return;
        }
        std::string body = req.substr(bp + 4);
        while (body.size() < cl) {
            int k = recv(cs, buf, sizeof buf, 0);
            if (k <= 0) break;
            body.append(buf, (size_t)k);
        }
        auto val = [&](const char* key) -> std::string {
            std::string pk = std::string(key) + "=";
            size_t v = body.find(pk);
            if (v == std::string::npos) return "";
            size_t e = body.find('&', v);
            return url_decode(body.substr(v + pk.size(), e == std::string::npos ? e : e - v - pk.size()));
        };
        std::string v;
        if ((v = val("pause")) == "1") set_paused(true);
        if ((v = val("pause")) == "0") set_paused(false);
        if (!(v = val("speed")).empty())
            set_speed(v == "max" ? 0.0 : atof(v.c_str()));
        if ((v = val("restart")) == "1") request_restart();
        if (body.find("lesion=") != std::string::npos) set_lesion(val("lesion"));
        if ((v = val("cancel")) == "1") dl_cancel();
        if (!(v = val("url")).empty()) {
            { std::lock_guard<std::mutex> lk(im.ctl_mtx);
              im.new_url = v; }
            im.url_cv_.notify_all();
        }
        resp_raw(cs, "200 OK", "text/plain", "ok", 2);
    } else {
        resp_raw(cs, "404 Not Found", "text/plain", "not found", 9);
    }
    closesocket(cs);
}

void LiveServer::accept_loop() {
    Imp& im = *imp_;
    while (im.running_.load()) {
        sockaddr_in ca;
        int cl = sizeof ca;
        SOCKET cs = accept(im.listen_sock, (sockaddr*)&ca, &cl);
        if (cs == INVALID_SOCKET) break;
        std::thread([this, cs]() { conn_loop((void*)cs); }).detach();
    }
}

bool LiveServer::start(int port) {
    Imp& im = *imp_;
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) return false;
    int chosen = 0;
    for (int tp = port; tp < port + 10; ++tp) {
        SOCKET ls = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (ls == INVALID_SOCKET) return false;
        // NB: no SO_REUSEADDR — on Windows it allows binding a port already
        // served by another process, silently splitting connections between them.
        sockaddr_in a{};
        a.sin_family = AF_INET;
        a.sin_port = htons((u_short)tp);
        InetPtonA(AF_INET, "127.0.0.1", &a.sin_addr);
        if (bind(ls, (sockaddr*)&a, sizeof a) == 0 && listen(ls, 16) == 0) {
            im.listen_sock = ls;
            im.port = tp;
            chosen = tp;
            break;
        }
        closesocket(ls);
    }
    if (!chosen) {
        printf("[live] ERROR: ports %d-%d are all busy; pass another --port\n", port, port + 9);
        return false;
    }
    if (chosen != port) printf("[live] port %d busy, using %d\n", port, chosen);
    im.running_ = true;
    printf("[live] http://127.0.0.1:%d/\n", chosen);
    std::thread([this]() { accept_loop(); }).detach();
    char cmd[64];
    snprintf(cmd, sizeof cmd, "start \"\" http://127.0.0.1:%d/", chosen);
    system(cmd);
    return true;
}

void LiveServer::stop() {
    if (!imp_) return;
    Imp& im = *imp_;
    im.running_ = false;
    if (im.listen_sock != INVALID_SOCKET) {
        closesocket(im.listen_sock);
        im.listen_sock = INVALID_SOCKET;
    }
}

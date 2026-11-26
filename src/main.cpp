// flysim — simulation of the digitized fly brain (FlyWire/Princeton connectome).
// Stage 2: connectome loading, aggregation and validation.
#include "connectome.h"
#include "live.h"
#include "metrics.h"
#include "report.h"
#include "sim.h"
#include <chrono>
#include <thread>
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <mmsystem.h>
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <string>
#include <unordered_map>
#include <vector>

void run_selftest(const Connectome& C);
int run_eyetest(const Connectome& C, const char* mode, const char* out_suffix);
int run_video(const Connectome& C, const std::string& source, bool is_url,
              double max_sec, const char* suffix, LiveServer* live);
int run_replay(const Connectome& C, const std::string& csv_path,
               const std::string& video_path, int port);
extern std::string g_new_url_taken;

static const char* NT_NAME[NT_COUNT] = { "?", "ACH", "GABA", "GLUT", "OCT", "DA", "SER", "HIST" };
static const char* SIDE_NAME[4] = { "?", "left", "right", "center" };
static const char* DOM_NAME[4] = { "none", "leg(walk/groom)", "tectulum(flight/jump)", "other" };

static const char* sup_name(uint8_t sup) {
    switch (sup) {
        case SUP_OL_INTRINSIC: return "ol_intrinsic";
        case SUP_CB_INTRINSIC: return "cb_intrinsic";
        case SUP_VNC_INTRINSIC: return "vnc_intrinsic";
        case SUP_VISUAL_PROJECTION: return "visual_projection";
        case SUP_VNC_SENSORY: return "vnc_sensory";
        case SUP_OL_SENSORY: return "ol_sensory";
        case SUP_CB_SENSORY: return "cb_sensory";
        case SUP_ASCENDING: return "ascending_neuron";
        case SUP_DESCENDING: return "descending_neuron";
        case SUP_VNC_MOTOR: return "vnc_motor";
        case SUP_VISUAL_CENTRIFUGAL: return "visual_centrifugal";
        case SUP_SENSORY_ASCENDING: return "sensory_ascending";
        case SUP_CB_MOTOR: return "cb_motor";
        case SUP_VNC_EFFERENT: return "vnc_efferent";
        case SUP_CB_ENDOCRINE: return "cb_endocrine";
        default: return "other";
    }
}

static void print_report(const Connectome& C, bool from_cache) {
    printf("=== flysim connectome validation ===\n");
    printf("source: %s\n", from_cache ? "cache (connectome.bin)" : "CSV files");
    printf("neurons           : %zu\n", C.n());
    printf("raw connection rows: %llu\n", (unsigned long long)C.raw_rows);
    printf("unique (pre,post)  : %llu  (aggregated across neuropils)\n",
           (unsigned long long)C.unique_pairs);
    printf("total synapses     : %llu\n", (unsigned long long)C.total_syn);

    // NT distribution
    uint64_t nt_cnt[NT_COUNT] = { 0 };
    for (const Neuron& nn : C.neurons) ++nt_cnt[nn.nt];
    printf("neurotransmitters  : ");
    for (int i = 1; i < NT_COUNT; ++i) printf("%s=%llu ", NT_NAME[i], (unsigned long long)nt_cnt[i]);
    printf("unknown=%llu\n", (unsigned long long)nt_cnt[0]);

    // Degree stats
    uint64_t out_sum = 0;
    uint32_t max_out = 0, max_in = 0;
    {
        std::vector<uint32_t> outd(C.n(), 0);
        for (const Edge& e : C.edges) ++outd[e.pre];
        for (uint32_t d : outd) { out_sum += d; if (d > max_out) max_out = d; }
        for (size_t i = 0; i < C.n(); ++i) {
            uint32_t d = C.in_off[i + 1] - C.in_off[i];
            if (d > max_in) max_in = d;
        }
    }
    printf("in-degree  mean=%.1f max=%u | out-degree mean=%.1f max=%u\n",
           (double)C.m() / (double)C.n(), max_in,
           (double)out_sum / (double)C.n(), max_out);

    // Readout groups
    printf("\n-- readout groups --\n");
    printf("photoreceptors (ol_sensory): %zu\n", C.photos.size());
    printf("lamina L1-L5               : %zu\n", C.lamina.size());
    printf("T4/T5 motion detectors     : %zu\n", C.t4t5.size());
    printf("LC4+LPLC (looming)         : %zu\n", C.lc_loom.size());
    printf("HS/VS wide-field motion    : %zu\n", C.hs_vs.size());
    printf("MBON (mushroom body out)   : %zu\n", C.mbons.size());
    printf("dopaminergic (DA)          : %zu\n", C.dan.size());
    printf("octopaminergic (OCT)       : %zu\n", C.oan.size());
    printf("serotonergic (SER)         : %zu\n", C.ser.size());
    printf("descending neurons (DN)    : %zu\n", C.dnv.size());
    printf("motor neurons              : %zu\n", C.motor.size());
    printf("ascending neurons          : %zu\n", C.asc.size());

    // Amendment 6: verify DNp01 = giant fiber before building the escape counter.
    printf("\n-- DNp01 (Giant Fiber) verification --\n");
    for (uint32_t gi : C.gf) {
        const Neuron& nn = C.neurons[gi];
        printf("root_id=%u  cell_type=%s  class=%s  sub_class=%s  super=%s  nerve=%s  nt=%s  side=%s  in_edges=%u\n",
               nn.id, C.s(nn.cell_type).c_str(), C.s(nn.cls).c_str(),
               C.s(nn.sub_class).c_str(), sup_name(nn.sup), C.s(nn.nerve).c_str(),
               NT_NAME[nn.nt], SIDE_NAME[nn.side], C.in_off[gi + 1] - C.in_off[gi]);
    }
    if (C.gf.size() != 2)
        printf("WARNING: expected exactly 2 DNp01 neurons (left/right), found %zu\n", C.gf.size());

    // Top presynaptic partners of the giant fiber, aggregated by cell type
    // (biology cross-check: LC4/LPLC2 populations must dominate).
    printf("top presynaptic partners of GF (aggregated by cell type):\n");
    {
        struct PT { uint32_t cnt; double w; };
        std::unordered_map<uint32_t, PT> agg;
        for (uint32_t gi : C.gf)
            for (uint32_t k = C.in_off[gi]; k < C.in_off[gi + 1]; ++k) {
                PT& pt = agg[C.neurons[C.edges[k].pre].cell_type];
                pt.cnt += 1;
                pt.w += C.edges[k].w;
            }
        std::vector<std::pair<double, std::pair<uint32_t, PT>>> v;
        for (auto& kv : agg) v.push_back({kv.second.w, {kv.first, kv.second}});
        std::sort(v.begin(), v.end(),
                  [](auto& a, auto& b) { return a.first > b.first; });
        size_t lim = std::min<size_t>(v.size(), 8);
        for (size_t i = 0; i < lim; ++i)
            printf("  %-10s (%u neurons) %7.0f synapses\n",
                   C.s(v[i].second.first).c_str(), v[i].second.second.cnt, v[i].first);
    }

    // DN motor domains
    printf("\n-- descending neuron motor domains --\n");
    {
        uint64_t dom[4] = { 0, 0, 0, 0 };
        for (uint32_t di : C.dnv) ++dom[C.dn_dom[di]];
        for (int d = 1; d < 4; ++d)
            printf("  %-24s %llu\n", DOM_NAME[d], (unsigned long long)dom[d]);
    }
    printf("\nvalidation done.\n");
}

int main(int argc, char** argv) {
    setvbuf(stdout, nullptr, _IONBF, 0);  // live progress must not sit in buffers
    timeBeginPeriod(1);                   // Sleep(1) granularity for live pacing
    std::string dir = "..\\neyron";       // CSVs live outside the project dir
    std::string cache = "connectome.bin"; // cache stays in the working dir
    bool dir_given = false;
    std::string cmd = "validate", url, video;
    std::vector<std::string> pos;
    bool rebuild = false, shuffle = false, live_mode = false, realtime = false;
    uint64_t seed = 20261024;
    double max_sec = 0, speed0 = -1;
    int live_port = 8765;
    for (int i = 1; i < argc; ++i) {
        if (!strcmp(argv[i], "--dir") && i + 1 < argc) { dir = argv[++i]; dir_given = true; }
        else if (!strcmp(argv[i], "--cache") && i + 1 < argc) cache = argv[++i];
        else if (!strcmp(argv[i], "--rebuild")) rebuild = true;
        else if (!strcmp(argv[i], "--shuffle")) shuffle = true;
        else if (!strcmp(argv[i], "--live")) live_mode = true;
        else if (!strcmp(argv[i], "--realtime")) realtime = true;
        else if (!strcmp(argv[i], "--port") && i + 1 < argc) live_port = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--speed") && i + 1 < argc)
            speed0 = !strcmp(argv[++i], "max") ? 0.0 : atof(argv[i]);
        else if (!strcmp(argv[i], "--seed") && i + 1 < argc) { seed = strtoull(argv[++i], nullptr, 10); g_seed = seed; }
        else if (!strcmp(argv[i], "--url") && i + 1 < argc) url = argv[++i];
        else if (!strcmp(argv[i], "--video") && i + 1 < argc) video = argv[++i];
        else if (!strcmp(argv[i], "--max-sec") && i + 1 < argc) max_sec = atof(argv[++i]);
        else if (!strcmp(argv[i], "--threads") && i + 1 < argc) g_threads = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--escape-refractory") && i + 1 < argc) g_escape_refractory = atof(argv[++i]);
        else if (!strcmp(argv[i], "lc")) { /* eyetest mode flag */ }
        else pos.push_back(argv[i]);
    }
    if (!pos.empty()) cmd = pos[0];
    std::string replay_csv = pos.size() > 1 ? pos[1] : "";

    Connectome C;
    std::error_code dir_ec;
    if (!dir_given && !std::filesystem::exists(dir + "/neurons.csv", dir_ec))
        dir = ".";  // compatibility: data next to the project
    bool from_cache = false;
    const char* eyetest_mode = "pure";
    for (int i = 1; i < argc; ++i)
        if (!strcmp(argv[i], "lc")) eyetest_mode = "lc";
    if (!rebuild && C.load(cache.c_str())) {
        from_cache = true;
    } else {
        C.build((dir + "/neurons.csv").c_str(),
                (dir + "/connections_princeton.csv").c_str());
        C.save(cache.c_str());
    }
    if (shuffle) {
        C.shuffle_wiring(seed);
        printf("[control mode] wiring shuffled, seed=%llu (degree sequence preserved)\n",
               (unsigned long long)seed);
    }
    if (cmd == "validate") print_report(C, from_cache);
    else if (cmd == "selftest") run_selftest(C);
    else if (cmd == "eyetest") run_eyetest(C, eyetest_mode, shuffle ? "_shuf" : "");
    else if (cmd == "run") {
        if (url.empty() && video.empty()) {
            fprintf(stderr, "usage: flysim run --url <youtube-link> | --video <file> [--max-sec N] "
                            "[--threads N] [--seed N] [--dir <data-dir>] [--cache <file>] "
                            "[--live] [--port N] [--realtime|--speed X] [--shuffle]\n");
            return 1;
        }
        LiveServer live;
        if (live_mode) {
            live.init(C, false);
            // live defaults to realtime pacing; --speed/--realtime override
            live.set_speed(realtime ? 1.0 : (speed0 >= 0 ? speed0 : 1.0));
            if (!live.start(live_port)) live_mode = false;
        }
        std::string cur = !url.empty() ? url : video;
        bool cur_is_url = !url.empty();
        for (;;) {
            int rc = run_video(C, cur, cur_is_url, max_sec, shuffle ? "_shuf" : "",
                               live_mode ? &live : nullptr);
            if (rc == 3) {  // new URL submitted from the live page
                cur = g_new_url_taken;
                cur_is_url = true;
                printf("[run] switching to new URL: %s\n", cur.c_str());
                continue;
            }
            if (rc == 2 && live_mode) {  // restart requested from the page
                printf("[run] restarting...\n");
                continue;
            }
            if (rc == 1 && live_mode && cur_is_url) {
                // download/sim error: stay alive so the user can submit a new
                // link on the page; dl error is visible in /stats
                printf("[run] error; live page stays up — submit a new link\n");
                for (;;) {
                    std::this_thread::sleep_for(std::chrono::seconds(1));
                    std::string nu;
                    live.take_new_url_wait(nu, 1.0);
                    if (!nu.empty()) { cur = nu; cur_is_url = true; break; }
                    if (live.restart_requested()) { cur_is_url = false; break; }
                }
                continue;
            }
            return rc == 2 ? 0 : rc;
        }
    }
    else if (cmd == "replay") {
        if (replay_csv.empty()) {
            fprintf(stderr, "usage: flysim replay metrics.csv --video <file> [--port N]\n");
            return 1;
        }
        return run_replay(C, replay_csv, video, live_port);
    }
    else if (cmd == "report") {
        // Rebuild out/report.html from a metrics CSV (--video <csv>, default out/metrics_run.csv)
        std::string csvp = video.empty() ? "out/metrics_run.csv" : video;
        regenerate_report(csvp.c_str(), shuffle ? "shuffled" : "real wiring");
    }
    else fprintf(stderr, "unknown command: %s (available: validate, selftest, eyetest [lc], run, replay, report)\n", cmd.c_str());
    return 0;
}

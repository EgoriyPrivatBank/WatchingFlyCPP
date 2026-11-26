// Stage 4: fly-eye test on synthetic video. Pure mode (amendment 3): only
// photoreceptors (clamped) and lamina (external drive) receive video-derived
// signals; looming must reach DNp01 through the real wiring. Debug mode
// "lc" drives LC4/LPLC2 directly for readout calibration.
#include "flyeye.h"
#include "metrics.h"
#include <chrono>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

static const int FW = 320, FH = 180, FPS = 30;
static const int STEPS_PER_FRAME = 6;   // 6 x 5 ms = 30 ms per video frame

static float gmean(const Brain& B, const std::vector<uint32_t>& g) {
    if (g.empty()) return 0.0f;
    double s = 0.0;
    for (uint32_t i : g) s += B.rate[i];
    return (float)(s / g.size());
}

struct EyPhase {
    const char* name;
    double t0, t1;
    int mode;  // 0 rest, 1 looming, 2 receding, 3 flicker, 4 grating
};
static const EyPhase EYPH[] = {
    {"rest",     0.0, 3.0, 0},
    {"looming",  3.0, 6.0, 1},
    {"receding", 6.0, 8.0, 2},
    {"flicker",  8.0, 11.0, 3},
    {"grating",  11.0, 14.0, 4},
    {"rest2",    14.0, 16.0, 0},
};
static const int NEYPH = 6;

static void make_frame(const EyPhase& ph, double tt, uint8_t* buf) {
    switch (ph.mode) {
        case 0:  // uniform gray
            memset(buf, 140, FW * FH);
            break;
        case 1: {  // looming: black disk accelerates to a full-screen collision
            double u = (tt - ph.t0) / (ph.t1 - ph.t0);
            float r = (float)(4.0 + u * u * u * 179.0);  // half-diagonal at u=1
            for (int y = 0; y < FH; ++y)
                for (int x = 0; x < FW; ++x) {
                    float dx = x - FW * 0.5f, dy = y - FH * 0.5f;
                    buf[y * FW + x] = (dx * dx + dy * dy < r * r) ? 0 : 140;
                }
            break;
        }
        case 2: {  // receding: disk shrinks over 2 s
            double u = (tt - ph.t0) / (ph.t1 - ph.t0);
            float r = (float)(120.0 - u * 116.0);
            for (int y = 0; y < FH; ++y)
                for (int x = 0; x < FW; ++x) {
                    float dx = x - FW * 0.5f, dy = y - FH * 0.5f;
                    buf[y * FW + x] = (dx * dx + dy * dy < r * r) ? 40 : 140;
                }
            break;
        }
        case 3: {  // full-field 5 Hz luminance flicker (no spatial structure)
            bool on = fmod(tt - ph.t0, 0.2) < 0.1;
            memset(buf, on ? 40 : 240, FW * FH);
            break;
        }
        case 4: {  // translating vertical grating: motion without looming
            for (int y = 0; y < FH; ++y)
                for (int x = 0; x < FW; ++x) {
                    int xx = (int)((x - 120.0 * (tt - ph.t0)) / 40.0);
                    buf[y * FW + x] = (xx & 1) ? 200 : 80;
                }
            break;
        }
    }
}

int run_eyetest(const Connectome& C, const char* mode, const char* out_suffix) {
    const bool pure = strcmp(mode, "lc") != 0;
    printf("[eyetest] mode=%s (%s)%s\n", pure ? "pure" : "debug-lc",
           pure ? "drive photos+lamina only" : "direct LC4/LPLC2 injection",
           out_suffix[0] ? "  *** SHUFFLED-WIRING CONTROL ***" : "");

    Brain B(C);
    B.P = SimParams();
    B.init(MODEL_RATE);
    B.threads = g_threads;

    FlyEye eye;
    EyeParams ep;
    eye.init(C, ep);
    printf("eye: %d positions, %zu photos, %zu lamina, %zu LC4/LPLC cells\n",
           eye.positions(), eye.photos().size(), eye.lamina().size(), C.lc_loom.size());

    std::vector<uint32_t> dn_leg, dn_tct;
    for (uint32_t d : C.dnv) {
        if (C.dn_dom[d] == DN_DOM_LEG) dn_leg.push_back(d);
        else if (C.dn_dom[d] == DN_DOM_FLIGHT) dn_tct.push_back(d);
    }
    struct G { const char* name; std::vector<uint32_t> idx; };
    G groups[] = {
        {"photo", C.photos}, {"lamina", C.lamina}, {"t4t5", C.t4t5},
        {"lc4", C.lc_loom}, {"hsvs", C.hs_vs}, {"mbon", C.mbons},
        {"dan", C.dan}, {"oan", C.oan}, {"ser", C.ser}, {"gf", C.gf},
        {"dnleg", dn_leg}, {"dntct", dn_tct}, {"motor", C.motor},
    };
    const int NG = (int)(sizeof(groups) / sizeof(groups[0]));
    std::vector<std::vector<double>> sum(NEYPH, std::vector<double>(NG + 3, 0.0));
    std::vector<std::vector<double>> peak(NEYPH, std::vector<double>(NG + 3, 0.0));
    std::vector<std::vector<int64_t>> cnt(NEYPH, std::vector<int64_t>(NG + 3, 0));

    std::filesystem::create_directories("out");
    std::string et_csv_name = std::string("out/eyetest_") + (pure ? "pure" : "lc") + out_suffix + ".csv";
    std::string mt_csv_name = std::string("out/metrics_") + (pure ? "pure" : "lc") + out_suffix + ".csv";
    FILE* csv = fopen(et_csv_name.c_str(), "w");
    if (csv) {
        fprintf(csv, "t,phase,motion,transient,contrast");
        for (int i = 0; i < NG; ++i) fprintf(csv, ",%s", groups[i].name);
        fprintf(csv, ",all\n");
    }

    Metrics M;
    M.init(C);
    FILE* mcsv = fopen(mt_csv_name.c_str(), "w");
    if (mcsv) M.write_csv_header(mcsv);
    double sat_sum[NEYPH] = {0};
    int64_t sat_cnt[NEYPH] = {0};
    int64_t beh_cnt[NEYPH][BEH_COUNT] = {{0}};

    std::vector<uint8_t> frame(FW * FH);
    auto t0wall = std::chrono::steady_clock::now();
    const int nframes = (int)(16.0 * FPS);
    for (int f = 0; f < nframes; ++f) {
        double tt = (double)f / FPS;
        int ph = -1;
        for (int i = 0; i < NEYPH; ++i)
            if (tt >= EYPH[i].t0 && tt < EYPH[i].t1) { ph = i; break; }
        if (ph < 0) ph = NEYPH - 1;
        make_frame(EYPH[ph], tt, frame.data());
        eye.process_frame(frame.data(), FW, FH);
        if (pure) eye.drive_pure(B);
        else eye.drive_lc(B, 2.0);
        for (int s = 0; s < STEPS_PER_FRAME; ++s) B.step();

        if (csv) {
            fprintf(csv, "%.3f,%s,%.4f,%.4f,%.4f", tt, EYPH[ph].name,
                    eye.motion_mean(), eye.transient_mean(), eye.contrast_mean());
            for (int i = 0; i < NG; ++i) fprintf(csv, ",%.4f", gmean(B, groups[i].idx));
            fprintf(csv, ",%.4f\n", B.mean_rate());
        }
        double vals[16];
        for (int i = 0; i < NG; ++i) vals[i] = gmean(B, groups[i].idx);
        vals[NG] = B.mean_rate();
        vals[NG + 1] = eye.motion_mean();
        vals[NG + 2] = eye.transient_mean();
        for (int i = 0; i < NG + 3; ++i) {
            sum[ph][i] += vals[i];
            ++cnt[ph][i];
            if (vals[i] > peak[ph][i]) peak[ph][i] = vals[i];
        }
        M.update(B, tt, 1.0 / FPS, eye.motion_mean(), eye.transient_mean(), eye.contrast_mean());
        if (mcsv) M.write_csv_row(mcsv);
        sat_sum[ph] += M.satisfaction;
        ++sat_cnt[ph];
        ++beh_cnt[ph][M.behavior];
    }
    eye.release(B);
    double wall = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0wall).count();
    if (csv) fclose(csv);

    printf("%.1f s video processed in %.1f wall-s (%.1fx realtime)\n",
           16.0, wall, 16.0 / wall);
    printf("  %-9s %8s %8s %8s %8s %8s %8s %8s\n",
           "phase", "motion", "lamina", "t4t5", "lc4", "gf", "dntct", "all");
    for (int p = 0; p < NEYPH; ++p) {
        printf("  %-9s %8.4f %8.4f %8.4f %8.4f %8.4f %8.4f %8.4f\n", EYPH[p].name,
               cnt[p][NG + 1] ? sum[p][NG + 1] / cnt[p][NG + 1] : 0.0,
               cnt[p][1] ? sum[p][1] / cnt[p][1] : 0.0,
               cnt[p][2] ? sum[p][2] / cnt[p][2] : 0.0,
               cnt[p][3] ? sum[p][3] / cnt[p][3] : 0.0,
               cnt[p][9] ? sum[p][9] / cnt[p][9] : 0.0,
               cnt[p][11] ? sum[p][11] / cnt[p][11] : 0.0,
               cnt[p][NG] ? sum[p][NG] / cnt[p][NG] : 0.0);
    }
    printf("  GF peaks per phase: ");
    for (int p = 0; p < NEYPH; ++p) printf("%s=%.3f ", EYPH[p].name, peak[p][9]);
    printf("\n  LC4 peaks per phase: ");
    for (int p = 0; p < NEYPH; ++p) printf("%s=%.3f ", EYPH[p].name, peak[p][3]);
    if (mcsv) fclose(mcsv);
    printf("  ESCAPE events (DNp01 bursts): total=%lld\n", (long long)M.escapes_total);
    for (const Metrics::Event& e : M.events)
        printf("    t=%.2f s  gf=%.3f  lc4=%.3f\n", e.t, e.gf, e.lc4);
    printf("  %-9s %8s %7s  behavior mix (rest/active/walk/flight/jump, %%)\n",
           "phase", "escapes", "satis");
    for (int p = 0; p < NEYPH; ++p) {
        int64_t esc = 0;
        for (const Metrics::Event& e : M.events)
            if (e.t >= EYPH[p].t0 && e.t < EYPH[p].t1) ++esc;
        int64_t tot = 0;
        for (int b = 0; b < BEH_COUNT; ++b) tot += beh_cnt[p][b];
        printf("  %-9s %8lld %7.3f", EYPH[p].name, (long long)esc,
               sat_cnt[p] ? sat_sum[p] / sat_cnt[p] : 0.0);
        for (int b = 0; b < BEH_COUNT; ++b)
            printf("%s%.0f", b ? "/" : "  ", tot ? 100.0 * beh_cnt[p][b] / tot : 0.0);
        printf("\n");
    }
    printf("CSV: %s, %s\n", et_csv_name.c_str(), mt_csv_name.c_str());
    return 0;
}

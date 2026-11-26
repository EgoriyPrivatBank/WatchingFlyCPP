// Stage 3 self-test: stability, stimulus propagation and speed of the two
// dynamics models (rate vs LIF after Shiu et al. 2024) on the real connectome.
#include "sim.h"
#include <chrono>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <string>
#include <vector>

static float gmean(const Brain& B, const std::vector<uint32_t>& g) {
    if (g.empty()) return 0.0f;
    double s = 0.0;
    for (uint32_t i : g) s += B.rate[i];
    return (float)(s / g.size());
}

struct GroupDef {
    const char* name;
    const std::vector<uint32_t>* idx;  // nullptr -> all neurons
};

// Backward BFS from the giant fiber over in-edges: how many synapses upstream
// does the visual periphery sit? Proves the pure-input path exists.
static void path_check(const Connectome& C) {
    const size_t N = C.n();
    std::vector<int32_t> dist(N, -1);
    std::vector<uint32_t> q;
    q.reserve(1 << 16);
    for (uint32_t gi : C.gf) { dist[gi] = 0; q.push_back(gi); }
    for (size_t head = 0; head < q.size(); ++head) {
        uint32_t p = q[head];
        for (uint32_t k = C.in_off[p]; k < C.in_off[p + 1]; ++k) {
            uint32_t pre = C.edges[k].pre;
            if (dist[pre] < 0) { dist[pre] = dist[p] + 1; q.push_back(pre); }
        }
    }
    printf("-- upstream path check from Giant Fiber (backward BFS over real wiring) --\n");
    auto rep = [&](const char* name, const std::vector<uint32_t>& g) {
        int32_t mn = 1 << 30;
        double tot = 0;
        int64_t cnt = 0;
        for (uint32_t i : g)
            if (dist[i] >= 0) { if (dist[i] < mn) mn = dist[i]; tot += dist[i]; ++cnt; }
        printf("  %-10s: min_hops=%d mean_hops=%.1f reachable=%lld/%zu\n",
               name, mn == (1 << 30) ? -1 : mn, cnt ? tot / (double)cnt : 0.0,
               (long long)cnt, g.size());
    };
    rep("photos", C.photos);
    rep("lamina", C.lamina);
    rep("T4/T5", C.t4t5);
    rep("LC4/LPLC", C.lc_loom);
    rep("MBON", C.mbons);
    rep("DN(all)", C.dnv);
}

struct Phase {
    const char* name;
    double t0, t1;
    int mode;  // 0 none, 1 brightness step, 2 5 Hz flicker, 3 random per 100 ms
};
static const Phase PHASES[] = {
    {"spont",   0.0, 3.0, 0},
    {"step",    3.0, 5.0, 1},
    {"flicker", 5.0, 8.0, 2},
    {"random",  8.0, 11.0, 3},
    {"surge",  11.0, 13.0, 4},   // global brightness ramp, looming proxy (no spatial eye yet)
    {"recover", 13.0, 15.0, 0},
};
static const int NPHASE = 6;

static void run_model(const Connectome& C, int model, const GroupDef* groups, int ngroups) {
    Brain B(C);
    SimParams P;          // defaults; tau=50 ms, gain=1.2, dt=5 ms; LIF per Shiu et al.
    B.P = P;
    B.init(model);
    B.threads = g_threads;

    std::filesystem::create_directories("out");
    const char* mname = model == MODEL_RATE ? "rate" : "lif";
    FILE* csv = fopen((std::string("out/selftest_") + mname + ".csv").c_str(), "w");
    if (csv) {
        fprintf(csv, "t,phase");
        for (int gi = 0; gi < ngroups; ++gi) fprintf(csv, ",%s", groups[gi].name);
        fprintf(csv, "\n");
    }

    // per-phase accumulation: [phase][group]
    std::vector<std::vector<double>> sum(NPHASE, std::vector<double>(ngroups, 0.0));
    std::vector<std::vector<double>> peak(NPHASE, std::vector<double>(ngroups, 0.0));
    std::vector<std::vector<int64_t>> cnt(NPHASE, std::vector<int64_t>(ngroups, 0));
    int64_t pinned_spont = 0;

    int cur_phase = -1;
    int64_t last_boundary = -1;

    auto apply_phase = [&](int ph) {
        if (ph == cur_phase) return;
        cur_phase = ph;
        last_boundary = -1;
        if (ph < 0) return;
        switch (PHASES[ph].mode) {
            case 0: B.clear_clamps(); break;
            case 1: B.set_clamp_all(C.photos, 0.7f); break;
            default: break;  // flicker/random update per boundary below
        }
    };

    auto t0wall = std::chrono::steady_clock::now();
    const int total_steps = (int)(15.0 / B.P.dt);
    for (int s = 0; s < total_steps; ++s) {
        double tt = B.t;
        int ph = -1;
        for (int i = 0; i < NPHASE; ++i)
            if (tt >= PHASES[i].t0 && tt < PHASES[i].t1) { ph = i; break; }
        if (ph >= 0 && PHASES[ph].mode == 2) {  // 5 Hz flicker, 200 ms halves
            int64_t b = (int64_t)std::floor((tt - PHASES[2].t0) / 0.2);
            if (b != last_boundary) {
                last_boundary = b;
                B.set_clamp_all(C.photos, (b % 2 == 0) ? 0.8f : 0.2f);
            }
        } else if (ph >= 0 && PHASES[ph].mode == 3) {  // random pattern per 100 ms
            int64_t b = (int64_t)std::floor((tt - PHASES[3].t0) / 0.1);
            if (b != last_boundary) {
                last_boundary = b;
                B.set_clamp_hash(C.photos, 0.2, 0.8, (uint64_t)b * 7919 + 13);
            }
        } else if (ph >= 0 && PHASES[ph].mode == 4) {  // brightness surge: 0.6 s ramp 0.2 -> 1.0
            double u = (tt - PHASES[4].t0) / 0.6;
            B.set_clamp_all(C.photos, u < 1.0 ? (float)(0.2 + 0.8 * u) : 1.0f);
        }
        apply_phase(ph);
        B.step();

        // stats every 5 steps (25 ms)
        if (s % 5 == 0 && ph >= 0) {
            for (int gi = 0; gi < ngroups; ++gi) {
                float r = groups[gi].idx ? gmean(B, *groups[gi].idx) : (float)B.mean_rate();
                sum[ph][gi] += r;
                cnt[ph][gi]++;
                if (r > peak[ph][gi]) peak[ph][gi] = r;
            }
            if (ph == 0) {
                for (size_t i = 0; i < B.rate.size(); ++i)
                    if (B.rate[i] > 0.95f) ++pinned_spont;
            }
        }
        if (csv && s % 10 == 0) {
            fprintf(csv, "%.3f,%s", tt, ph >= 0 ? PHASES[ph].name : "-");
            for (int gi = 0; gi < ngroups; ++gi) {
                float r = groups[gi].idx ? gmean(B, *groups[gi].idx) : (float)B.mean_rate();
                fprintf(csv, ",%.4f", r);
            }
            fprintf(csv, "\n");
        }
    }
    double wall = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0wall).count();
    if (csv) fclose(csv);

    printf("\n[%s] %.1f sim-s in %.1f wall-s (%.1fx realtime), %zu neurons / %llu edges\n",
           mname, 15.0, wall, 15.0 / wall, C.n(), (unsigned long long)C.m());
    printf("  spontaneous (0-3 s): mean_all=%.4f  pinned(>0.95)=%.2f%%\n",
           cnt[0][ngroups - 1] ? sum[0][ngroups - 1] / cnt[0][ngroups - 1] : 0.0,
           100.0 * (double)pinned_spont / ((double)cnt[0][ngroups - 1] * (double)C.n()));
    printf("  %-8s", "phase");
    for (int gi = 0; gi < ngroups; ++gi) printf(" %8s", groups[gi].name);
    printf("\n");
    for (int ph = 0; ph < NPHASE; ++ph) {
        printf("  %-8s", PHASES[ph].name);
        for (int gi = 0; gi < ngroups; ++gi)
            printf(" %8.4f", cnt[ph][gi] ? sum[ph][gi] / cnt[ph][gi] : 0.0);
        printf("\n");
    }
    printf("  peaks  ");
    for (int gi = 0; gi < ngroups; ++gi) {
        double pk = 0;
        for (int ph = 1; ph < 5; ++ph)
            if (peak[ph][gi] > pk) pk = peak[ph][gi];
        printf(" %8.4f", pk);
    }
    printf("   (max over step/flicker/random/surge)\n");
}

void run_selftest(const Connectome& C) {
    path_check(C);

    std::vector<uint32_t> dn_leg, dn_tct;
    for (uint32_t d : C.dnv) {
        if (C.dn_dom[d] == DN_DOM_LEG) dn_leg.push_back(d);
        else if (C.dn_dom[d] == DN_DOM_FLIGHT) dn_tct.push_back(d);
    }
    GroupDef groups[] = {
        {"photo",   &C.photos},
        {"lamina",  &C.lamina},
        {"t4t5",    &C.t4t5},
        {"lc4",     &C.lc_loom},
        {"hsvs",    &C.hs_vs},
        {"mbon",    &C.mbons},
        {"dan",     &C.dan},
        {"oan",     &C.oan},
        {"ser",     &C.ser},
        {"gf",      &C.gf},
        {"dnleg",   &dn_leg},
        {"dntct",   &dn_tct},
        {"motor",   &C.motor},
        {"all",     nullptr},
    };
    int ngroups = (int)(sizeof(groups) / sizeof(groups[0]));

    for (int model : {MODEL_RATE, MODEL_LIF})
        run_model(C, model, groups, ngroups);
}

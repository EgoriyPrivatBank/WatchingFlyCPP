// Stage 7: the full pipeline — video file or YouTube URL -> fly eye ->
// whole-connectome simulation -> behavioral metrics -> time-series CSV.
#include "flyeye.h"
#include "live.h"
#include "metrics.h"
#include "report.h"
#include "video.h"
#include <chrono>
#include <thread>
#include <cstdio>
#include <filesystem>
#include <string>
#include <vector>

static const int STEPS_PER_FRAME = 6;  // 6 x 5 ms per 30 fps frame

// Set when the live page submits a new YouTube URL mid-run (run_video returns 3).
std::string g_new_url_taken;

int run_video(const Connectome& C, const std::string& source, bool is_url,
              double max_sec, const char* suffix, LiveServer* live) {
    std::string err, path = source;
    if (is_url) {
        printf("[run] downloading YouTube video (yt-dlp)...\n");
        path = youtube_download(source, "out", max_sec > 0 ? max_sec : 0, &err);
        if (path.empty()) {
            printf("[run] download failed: %s\n", err.c_str());
            return 1;
        }
        printf("[run] downloaded: %s\n", path.c_str());
    }
    VideoSource vs;
    if (!vs.open(path, is_url ? 0 : max_sec, &err)) {
        printf("[run] cannot open video: %s\n", err.c_str());
        return 1;
    }
    if (live) live->set_video_path(prepare_browser_copy(path));  // display copy for <video>

    Brain B(C);
    B.P = SimParams();
    B.init(MODEL_RATE);
    B.threads = g_threads;
    FlyEye eye;
    eye.init(C, EyeParams());
    Metrics M;
    M.init(C);

    std::filesystem::create_directories("out");
    std::string csv_name = std::string("out/metrics_run") + suffix + ".csv";
    FILE* csv = fopen(csv_name.c_str(), "w");
    if (csv) M.write_csv_header(csv);
    else printf("[run] cannot write %s (continuing without CSV)\n", csv_name.c_str());

    std::vector<uint8_t> frame(VideoSource::FW * VideoSource::FH);
    const double dt = 1.0 / VideoSource::FPS;
    int64_t beh_cnt[BEH_COUNT] = {0};
    double sat_sum = 0, sat_min = 1, sat_max = 0;
    int64_t nframes = 0;
    double t = 0.0;
    std::string cur_lesion;
    auto lesion_group_of = [&](const std::string& key) -> const std::vector<uint32_t>* {
        if (key == "lc") return &C.lc_loom;
        if (key == "t4t5") return &C.t4t5;
        if (key == "da") return &C.dan;
        if (key == "photos") return &C.photos;
        if (key == "mbon") return &C.mbons;
        if (key == "oct") return &C.oan;
        return nullptr;
    };
    auto next_pub = std::chrono::steady_clock::now();
    double speed_ema = 1.0;
    std::chrono::steady_clock::time_point last_pub_wall{};
    double last_pub_sim = -1;
    bool last_pub_valid = false;
    std::chrono::steady_clock::time_point pacing_base{};
    int64_t pacing_n = 0;
    bool pacing_base_valid = false;

    auto t0wall = std::chrono::steady_clock::now();
    while (vs.read_frame(frame.data()) == 1) {
        if (max_sec > 0 && t >= max_sec) break;
        auto f0 = std::chrono::steady_clock::now();
        if (live) {
            // ---- live control (never touches computation when idle) ----
            if (live->restart_requested()) return 2;
            {   std::string nu = live->take_new_url();
                if (!nu.empty()) { g_new_url_taken = nu; return 3; } }
            std::string lg = live->lesion_group();
            if (lg != cur_lesion) {
                const std::vector<uint32_t>* old_g = lesion_group_of(cur_lesion);
                const std::vector<uint32_t>* new_g = lesion_group_of(lg);
                if (old_g) B.set_lesion(*old_g, false);
                if (new_g) B.set_lesion(*new_g, true);
                cur_lesion = lg;
                M.lesion_name = lg.empty() ? "none" : lg;
            }
            auto pause_pub = std::chrono::steady_clock::now();
            while (live->is_paused()) {
                // keep serving the page: publish paused state ~5 Hz so the
                // resume button actually flips; sim state is frozen
                auto nowp = std::chrono::steady_clock::now();
                if (nowp >= pause_pub) {
                    live->publish(B, M, eye, t, nframes, 0.0);
                    pause_pub = nowp + std::chrono::milliseconds(200);
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(50));
                if (live->restart_requested()) return 2;
            }
        }
        eye.process_frame(frame.data(), VideoSource::FW, VideoSource::FH);
        eye.drive_pure(B);
        for (int s = 0; s < STEPS_PER_FRAME; ++s) B.step();
        M.update(B, t, dt, eye.motion_mean(), eye.transient_mean(), eye.contrast_mean());
        if (csv) M.write_csv_row(csv);
        ++beh_cnt[M.behavior];
        sat_sum += M.satisfaction;
        if (M.satisfaction < sat_min) sat_min = M.satisfaction;
        if (M.satisfaction > sat_max) sat_max = M.satisfaction;
        ++nframes;
        t += dt;
        if (live) {  // ---- pacing first, then publish (real achieved speed) ----
            double target = live->speed_target();
            if (target > 0) {
                // frame deadline: f0 + dt/target; sleep in small chunks, then
                // spin the last ~2 ms (Sleep granularity ~15.6 ms on Windows)
                // cumulative schedule: deadline = base + n*dt (drift-free);
                // if a frame is late, the next deadline is NOT pushed forward
                if (!pacing_base_valid) { pacing_base = f0; pacing_n = 0; pacing_base_valid = true; }
                ++pacing_n;
                auto deadline = pacing_base + std::chrono::duration<double>(pacing_n * dt / target);
                for (;;) {
                    double left = std::chrono::duration<double>(deadline -
                        std::chrono::steady_clock::now()).count();
                    if (left <= 0.002) break;
                    std::this_thread::sleep_for(std::chrono::milliseconds(
                        left > 0.020 ? 15 : 1));
                }
                while (std::chrono::steady_clock::now() < deadline) {
                    std::this_thread::yield();
                }
            }
            auto now = std::chrono::steady_clock::now();
            // achieved speed: sim seconds per wall second between publishes
            if (last_pub_valid) {
                double wall = std::chrono::duration<double>(now - last_pub_wall).count();
                double sim = t - last_pub_sim;
                if (wall > 1e-3 && sim >= 0)
                    speed_ema = 0.85 * speed_ema + 0.15 * (sim / wall);
            }
            last_pub_wall = now;
            last_pub_sim = t;
            last_pub_valid = true;
            if (now >= next_pub) {
                live->publish(B, M, eye, t, nframes, speed_ema);
                next_pub = now + std::chrono::milliseconds(50);
            }
        }
        if ((nframes % 300) == 0)
            printf("  ... %.0f s processed, %lld escape attempts\n",
                   t, (long long)M.escapes_total);
    }
    if (csv) fclose(csv);
    int64_t nan_cnt = 0;
    for (float r : B.rate) if (r != r) ++nan_cnt;
    if (nan_cnt) printf("WARNING: %lld NaN rates in final state!\n", (long long)nan_cnt);
    double wall = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0wall).count();

    printf("\n=== RUN SUMMARY (%s) ===\n", suffix[0] ? "SHUFFLED CONTROL" : "real wiring");
    printf("video: %.1f s (%lld frames), processed in %.1f wall-s (%.1fx realtime)\n",
           t, (long long)nframes, wall, t / wall);
    printf("ESCAPE ATTEMPTS (DNp01 bursts): %lld\n", (long long)M.escapes_total);
    for (const Metrics::Event& e : M.events)
        printf("  t=%7.2f s  gf=%.3f  lc4=%.3f\n", e.t, e.gf, e.lc4);
    printf("satisfaction: mean=%.3f  min=%.3f  max=%.3f\n",
           nframes ? sat_sum / nframes : 0.0, sat_min, sat_max);
    printf("behavior distribution: ");
    for (int b = 0; b < BEH_COUNT; ++b)
        printf("%s%s=%.1f%%", b ? " " : "", beh_name(b),
               nframes ? 100.0 * beh_cnt[b] / nframes : 0.0);
    printf("\n");
    printf("time series: %s\n", csv_name.c_str());
    write_html_report(csv_name, "out/report.html", M, t, nframes,
                      suffix[0] ? "shuffled" : "real wiring");
    return 0;
}

// Stage 6: replay a recorded metrics CSV in the live UI (no simulation).
int run_replay(const Connectome& C, const std::string& csv_path,
               const std::string& video_path, int port) {
    LiveServer live;
    live.init(C, true);
    std::string err;
    live.init_replay_csv(csv_path.c_str(), &err);
    if (!err.empty()) {
        printf("[replay] %s: %s\n", err.c_str(), csv_path.c_str());
        return 1;
    }
    if (!video_path.empty()) live.set_video_path(video_path);
    if (!live.start(port)) return 1;
    printf("[replay] page is open in the browser: play/seek the video, data follows it\n");
    printf("[replay] Ctrl+C to exit\n");
    for (;;) std::this_thread::sleep_for(std::chrono::seconds(1));
    return 0;
}

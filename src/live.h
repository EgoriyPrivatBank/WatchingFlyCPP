#pragma once
// Live mode: embedded 127.0.0.1-only HTTP server (Winsock) streaming the
// running simulation to a browser page. Server runs in its own threads; the
// simulation never blocks on it: publish() copies into latest-wins buffers
// under a short lock, slow clients simply miss frames.
#include "connectome.h"
#include "flyeye.h"
#include "metrics.h"
#include "sim.h"
#include <cstdint>
#include <string>

class LiveServer {
public:
    LiveServer() : imp_(nullptr) {}
    ~LiveServer();

    // Computes the neuron-map layout and pipeline edge weights from the
    // connectome. set_video_path() points /video at the media file.
    void init(const Connectome& C, bool replay_mode);
    void set_video_path(const std::string& path);
    // Replay mode: load a metrics CSV; /stats?i=N then serves row N.
    void init_replay_csv(const char* csv_path, std::string* err);
    bool start(int port);   // tries [port .. port+9], prints the final URL
    void stop();
    int port() const;

    // Simulation thread, up to ~20 Hz. Never blocks: copies under a short lock.
    void publish(const Brain& B, const Metrics& M, const FlyEye& eye,
                 double sim_t, int64_t frame, double speed_x);

    // ---- control state (set from the page via POST /ctl) ----
    bool is_paused() const;
    void set_paused(bool p);
    double speed_target() const;          // 0 = unlimited
    void set_speed(double x);
    bool restart_requested();             // one-shot
    void request_restart();
    std::string lesion_group() const;     // "" = none
    void set_lesion(const std::string& g);
    std::string take_new_url();           // one-shot, "" if none
    // like take_new_url but also wakes early on restart; waits up to timeout_s
    void take_new_url_wait(std::string& out, double timeout_s);

private:
    struct Imp;
    Imp* imp_;
    void accept_loop();
    void conn_loop(void* sock);
    std::string replay_stats(int64_t i);  // one CSV row as live-format JSON
    LiveServer(const LiveServer&);
    LiveServer& operator=(const LiveServer&);
};

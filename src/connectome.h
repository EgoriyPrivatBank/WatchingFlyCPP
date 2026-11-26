#pragma once
// Connectome of the adult fly CNS (FlyWire/Princeton export).
// neurons.csv provides per-neuron metadata; connections_princeton.csv provides
// (pre, post, neuropil, syn_count) rows which are aggregated by (pre, post)
// pair, summing syn_count across neuropils.
#include <cstdint>
#include <cstring>
#include <random>
#include <string>
#include <unordered_map>
#include <vector>

enum NT : uint8_t {
    NT_UNKNOWN = 0, NT_ACH, NT_GABA, NT_GLUT, NT_OCT, NT_DA, NT_SER, NT_HIST, NT_COUNT
};
enum SUP : uint8_t {
    SUP_OTHER = 0, SUP_OL_INTRINSIC, SUP_CB_INTRINSIC, SUP_VNC_INTRINSIC,
    SUP_VISUAL_PROJECTION, SUP_VNC_SENSORY, SUP_OL_SENSORY, SUP_CB_SENSORY,
    SUP_ASCENDING, SUP_DESCENDING, SUP_VNC_MOTOR, SUP_VISUAL_CENTRIFUGAL,
    SUP_SENSORY_ASCENDING, SUP_CB_MOTOR, SUP_VNC_EFFERENT, SUP_CB_ENDOCRINE
};
enum SIDE : uint8_t { SIDE_UNKNOWN = 0, SIDE_LEFT, SIDE_RIGHT, SIDE_CENTER };
// Motor domain of a descending neuron, from its synaptic targets in the VNC:
// LegNp* -> leg control (walking/grooming), tectulum (IntTct/LTct) -> flight/jump.
enum DN_DOM : uint8_t { DN_DOM_NONE = 0, DN_DOM_LEG, DN_DOM_FLIGHT, DN_DOM_OTHER };

struct Neuron {
    uint32_t id;                                   // root id
    uint32_t cell_type, cls, sub_class, nerve;     // string table indices
    uint8_t  nt, sup, side, _pad;
};
static_assert(sizeof(Neuron) == 24, "unexpected Neuron layout");

struct Edge { uint32_t pre, post; float w; };      // aggregated syn count, w > 0
static_assert(sizeof(Edge) == 12, "unexpected Edge layout");

struct Connectome {
    std::vector<Neuron>      neurons;              // sorted by id
    std::vector<std::string> strings;              // interned labels
    std::vector<Edge>        edges;                // unique (pre,post), sorted by (post,pre)
    std::vector<uint32_t>    in_off;               // CSR offsets over posts, size N+1
    std::vector<uint8_t>     dn_dom;               // per-neuron DN motor domain

    // Readout groups (neuron indices, ascending).
    std::vector<uint32_t> photos, lamina, t4t5, lc_loom, hs_vs, mbons;
    std::vector<uint32_t> dan, oan, ser;           // dopaminergic / octopaminergic / serotonergic
    std::vector<uint32_t> dnv, gf, motor, asc;     // descending, giant fiber, motor, ascending

    uint64_t raw_rows = 0, unique_pairs = 0, total_syn = 0;

    size_t n() const { return neurons.size(); }
    size_t m() const { return edges.size(); }
    const std::string& s(uint32_t i) const { return strings[i]; }
    bool streq(uint32_t sid, const char* t) const { return strings[sid] == t; }
    bool strstarts(uint32_t sid, const char* t) const {
        const std::string& v = strings[sid];
        size_t L = strlen(t);
        return v.size() >= L && v.compare(0, L, t) == 0;
    }

    void build(const char* neurons_csv, const char* connections_csv);
    void build_groups();
    void build_csr();
    void save(const char* path) const;
    bool load(const char* path);                    // false if absent/corrupt
    void shuffle_wiring(uint64_t seed);             // permute posts, keep degree sequence

    uint32_t intern(const char* p, uint32_t n);

private:
    bool load_impl(const char* path);
    std::unordered_map<std::string, uint32_t> intern_map_;
};

// Open-addressing id -> index map used while streaming connections.
struct IdMap {
    std::vector<uint64_t> keys;
    std::vector<int32_t>  vals;
    uint64_t mask = 0;
    void init(const std::vector<Neuron>& ns);
    int32_t find(uint64_t id) const;
};

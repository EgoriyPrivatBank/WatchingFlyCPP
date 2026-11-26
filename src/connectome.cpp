#include "connectome.h"
#include "csv.h"
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>

using csv::Field;

static NT parse_nt(const Field& f) {
    if (f.is("ACH"))   return NT_ACH;
    if (f.is("GABA"))  return NT_GABA;
    if (f.is("GLUT"))  return NT_GLUT;
    if (f.is("OCT"))   return NT_OCT;
    if (f.is("DA"))    return NT_DA;
    if (f.is("SER"))   return NT_SER;
    if (f.is("HIST"))  return NT_HIST;
    return NT_UNKNOWN;
}
static SUP parse_sup(const Field& f) {
    if (f.is("ol_intrinsic"))       return SUP_OL_INTRINSIC;
    if (f.is("cb_intrinsic"))       return SUP_CB_INTRINSIC;
    if (f.is("vnc_intrinsic"))      return SUP_VNC_INTRINSIC;
    if (f.is("visual_projection"))  return SUP_VISUAL_PROJECTION;
    if (f.is("vnc_sensory"))        return SUP_VNC_SENSORY;
    if (f.is("ol_sensory"))         return SUP_OL_SENSORY;
    if (f.is("cb_sensory"))         return SUP_CB_SENSORY;
    if (f.is("ascending_neuron"))   return SUP_ASCENDING;
    if (f.is("descending_neuron"))  return SUP_DESCENDING;
    if (f.is("vnc_motor"))          return SUP_VNC_MOTOR;
    if (f.is("visual_centrifugal")) return SUP_VISUAL_CENTRIFUGAL;
    if (f.is("sensory_ascending"))  return SUP_SENSORY_ASCENDING;
    if (f.is("cb_motor"))           return SUP_CB_MOTOR;
    if (f.is("vnc_efferent"))       return SUP_VNC_EFFERENT;
    if (f.is("cb_endocrine"))       return SUP_CB_ENDOCRINE;
    return SUP_OTHER;
}
static SIDE parse_side(const Field& f) {
    if (f.is("left"))   return SIDE_LEFT;
    if (f.is("right"))  return SIDE_RIGHT;
    if (f.is("center")) return SIDE_CENTER;
    return SIDE_UNKNOWN;
}

uint32_t Connectome::intern(const char* p, uint32_t n) {
    std::string key(p, n);
    auto it = intern_map_.find(key);
    if (it != intern_map_.end()) return it->second;
    uint32_t id = (uint32_t)strings.size();
    strings.push_back(key);
    intern_map_.emplace(key, id);
    return id;
}

void IdMap::init(const std::vector<Neuron>& ns) {
    size_t cap = 16;
    while (cap < ns.size() * 4) cap <<= 1;
    keys.assign(cap, ~0ull);
    vals.assign(cap, -1);
    mask = cap - 1;
    for (size_t i = 0; i < ns.size(); ++i) {
        uint64_t h = ns[i].id * 0x9E3779B97F4A7C15ull;
        for (;;) {
            uint64_t k = h & mask;
            if (keys[k] == ~0ull) { keys[k] = ns[i].id; vals[k] = (int32_t)i; break; }
            ++h;
        }
    }
}
int32_t IdMap::find(uint64_t id) const {
    uint64_t h = id * 0x9E3779B97F4A7C15ull;
    for (;;) {
        uint64_t k = h & mask;
        if (keys[k] == id) return vals[k];
        if (keys[k] == ~0ull) return -1;
        ++h;
    }
}

void Connectome::build(const char* neurons_csv, const char* connections_csv) {
    // ---------- neurons.csv ----------
    std::vector<char> buf;
    if (!csv::read_file(neurons_csv, buf)) {
        fprintf(stderr, "ERROR: cannot open %s\n", neurons_csv);
        exit(1);
    }
    char* p = buf.data();
    Field f[24];
    split_line(&p, f, 24);  // header
    neurons.reserve(170000);
    while (*p) {
        size_t nf = csv::split_line(&p, f, 24);
        if (nf < 17) continue;
        Neuron nn{};
        nn.id        = (uint32_t)f[0].to_u64();
        nn.nt        = parse_nt(f[3]);
        if (nn.nt == NT_UNKNOWN && nf > 5) nn.nt = parse_nt(f[5]);  // verified NT fallback
        nn.sup       = parse_sup(f[10]);
        nn.cls       = intern(f[11].p, f[11].n);
        nn.sub_class = intern(f[12].p, f[12].n);
        nn.nerve     = intern(f[14].p, f[14].n);
        nn.side      = parse_side(f[15]);
        nn.cell_type = intern(f[16].p, f[16].n);
        neurons.push_back(nn);
    }
    std::sort(neurons.begin(), neurons.end(),
              [](const Neuron& a, const Neuron& b) { return a.id < b.id; });
    auto dup = std::unique(neurons.begin(), neurons.end(),
                           [](const Neuron& a, const Neuron& b) { return a.id == b.id; });
    if (dup != neurons.end()) {
        fprintf(stderr, "WARNING: %zu duplicate neuron ids dropped\n",
                (size_t)(neurons.end() - dup));
        neurons.erase(dup, neurons.end());
    }
    fprintf(stderr, "neurons loaded: %zu\n", neurons.size());

    IdMap idmap;
    idmap.init(neurons);

    // ---------- connections_princeton.csv ----------
    if (!csv::read_file(connections_csv, buf)) {
        fprintf(stderr, "ERROR: cannot open %s\n", connections_csv);
        exit(1);
    }
    p = buf.data();
    csv::split_line(&p, f, 24);  // header
    struct RawEdge { uint32_t pre, post, syn; };
    std::vector<RawEdge> raw;
    raw.reserve(9500000);
    std::vector<uint32_t> leg_syn(n(), 0), tct_syn(n(), 0);
    uint64_t skipped_unknown = 0, skipped_zero = 0;
    while (*p) {
        size_t nf = csv::split_line(&p, f, 24);
        ++raw_rows;
        if (nf < 4) continue;
        int32_t pre  = idmap.find(f[0].to_u64());
        int32_t post = idmap.find(f[1].to_u64());
        uint32_t syn = (uint32_t)f[3].to_u64();
        if (pre < 0 || post < 0) { ++skipped_unknown; continue; }
        if (syn == 0)            { ++skipped_zero;    continue; }
        // Motor domain of descending neurons from their VNC targets.
        if (neurons[pre].sup == SUP_DESCENDING) {
            if (f[2].starts_with("LegNp"))                          leg_syn[pre] += syn;
            else if (f[2].starts_with("IntTct") || f[2].starts_with("LTct"))
                                                                    tct_syn[pre] += syn;
        }
        raw.push_back({(uint32_t)pre, (uint32_t)post, syn});
        if ((raw_rows & 0x1FFFFFF) == 0)
            fprintf(stderr, "  ... %llu rows scanned\n", (unsigned long long)raw_rows);
    }
    fprintf(stderr,
            "connection rows scanned: %llu (unknown ids: %llu, zero-syn rows: %llu)\n",
            (unsigned long long)raw_rows, (unsigned long long)skipped_unknown,
            (unsigned long long)skipped_zero);

    // Aggregate by (pre, post), summing syn_count across neuropils.
    std::sort(raw.begin(), raw.end(), [](const RawEdge& a, const RawEdge& b) {
        return a.pre != b.pre ? a.pre < b.pre : a.post < b.post;
    });
    edges.clear();
    edges.reserve(raw.size());
    total_syn = 0;
    for (size_t i = 0; i < raw.size();) {
        uint64_t w = 0;
        size_t j = i;
        while (j < raw.size() && raw[j].pre == raw[i].pre && raw[j].post == raw[i].post) {
            w += raw[j].syn;
            ++j;
        }
        edges.push_back({raw[i].pre, raw[i].post, (float)w});
        total_syn += w;
        i = j;
    }
    unique_pairs = edges.size();
    raw.clear();
    raw.shrink_to_fit();
    fprintf(stderr, "unique (pre,post) pairs: %llu, total synapses: %llu\n",
            (unsigned long long)unique_pairs, (unsigned long long)total_syn);

    // Sort by (post, pre) for the in-edge CSR, then build offsets.
    std::sort(edges.begin(), edges.end(), [](const Edge& a, const Edge& b) {
        return a.post != b.post ? a.post < b.post : a.pre < b.pre;
    });
    build_csr();

    // DN motor domains.
    dn_dom.assign(n(), DN_DOM_NONE);
    for (size_t i = 0; i < n(); ++i) {
        if (neurons[i].sup != SUP_DESCENDING) continue;
        if (leg_syn[i] > tct_syn[i] && leg_syn[i] > 0) dn_dom[i] = DN_DOM_LEG;
        else if (tct_syn[i] > 0)                       dn_dom[i] = DN_DOM_FLIGHT;
        else if (streq(neurons[i].nerve, "fl"))        dn_dom[i] = DN_DOM_FLIGHT;
        else                                           dn_dom[i] = DN_DOM_OTHER;
    }

    build_groups();
}

void Connectome::build_csr() {
    in_off.assign(n() + 1, 0);
    for (const Edge& e : edges) ++in_off[e.post + 1];
    for (size_t i = 0; i < n(); ++i) in_off[i + 1] += in_off[i];
}

void Connectome::build_groups() {
    photos.clear(); lamina.clear(); t4t5.clear(); lc_loom.clear(); hs_vs.clear();
    mbons.clear(); dan.clear(); oan.clear(); ser.clear(); dnv.clear(); gf.clear();
    motor.clear(); asc.clear();
    for (size_t i = 0; i < n(); ++i) {
        const Neuron& nn = neurons[i];
        const std::string& ct = s(nn.cell_type);
        if (nn.sup == SUP_OL_SENSORY) photos.push_back((uint32_t)i);
        if (ct == "L1" || ct == "L2" || ct == "L3" || ct == "L4" || ct == "L5")
            lamina.push_back((uint32_t)i);
        if (ct.size() >= 2 && (ct.compare(0, 2, "T4") == 0 || ct.compare(0, 2, "T5") == 0))
            t4t5.push_back((uint32_t)i);
        if (ct == "LC4" || ct.compare(0, 4, "LPLC") == 0) lc_loom.push_back((uint32_t)i);
        if (ct == "HSN" || ct == "HSE" || ct == "HSS" || ct.compare(0, 2, "VS") == 0)
            hs_vs.push_back((uint32_t)i);
        if (streq(nn.cls, "MBON") || ct.compare(0, 4, "MBON") == 0) mbons.push_back((uint32_t)i);
        if (nn.nt == NT_DA)  dan.push_back((uint32_t)i);
        if (nn.nt == NT_OCT) oan.push_back((uint32_t)i);
        if (nn.nt == NT_SER) ser.push_back((uint32_t)i);
        if (nn.sup == SUP_DESCENDING) dnv.push_back((uint32_t)i);
        if (ct == "DNp01") gf.push_back((uint32_t)i);
        if (nn.sup == SUP_VNC_MOTOR || nn.sup == SUP_CB_MOTOR) motor.push_back((uint32_t)i);
        if (nn.sup == SUP_ASCENDING || nn.sup == SUP_SENSORY_ASCENDING)
            asc.push_back((uint32_t)i);
    }
}

void Connectome::shuffle_wiring(uint64_t seed) {
    // Configuration-model control: permute posts among edges. Out-degree of
    // every pre and the global in-degree multiset are preserved; only the
    // wiring changes. Self-loops may appear (rare, kept).
    std::mt19937_64 rng(seed);
    for (size_t i = edges.size(); i > 1; --i) {
        size_t j = (size_t)(rng() % i);
        std::swap(edges[i - 1].post, edges[j].post);
    }
    std::sort(edges.begin(), edges.end(), [](const Edge& a, const Edge& b) {
        return a.post != b.post ? a.post < b.post : a.pre < b.pre;
    });
    build_csr();
}

// ---------------- binary cache ----------------
static const char CACHE_MAGIC[9] = "FLYCNT01";

void Connectome::save(const char* path) const {
    FILE* f = fopen(path, "wb");
    if (!f) { fprintf(stderr, "ERROR: cannot write %s\n", path); return; }
    fwrite(CACHE_MAGIC, 1, 8, f);
    uint32_t ver = 1, N = (uint32_t)n(), M = (uint32_t)m(), sc = (uint32_t)strings.size();
    fwrite(&ver, 4, 1, f);
    fwrite(&N, 4, 1, f);
    fwrite(&M, 4, 1, f);
    fwrite(&raw_rows, 8, 1, f);
    fwrite(&unique_pairs, 8, 1, f);
    fwrite(&total_syn, 8, 1, f);
    fwrite(&sc, 4, 1, f);
    for (const std::string& st : strings) {
        uint32_t L = (uint32_t)st.size();
        fwrite(&L, 4, 1, f);
        fwrite(st.data(), 1, L, f);
    }
    fwrite(neurons.data(), sizeof(Neuron), N, f);
    fwrite(edges.data(), sizeof(Edge), M, f);
    fwrite(dn_dom.data(), 1, N, f);
    auto wvec = [&](const std::vector<uint32_t>& v) {
        uint32_t c = (uint32_t)v.size();
        fwrite(&c, 4, 1, f);
        if (c) fwrite(v.data(), 4, c, f);
    };
    wvec(photos); wvec(lamina); wvec(t4t5); wvec(lc_loom); wvec(hs_vs); wvec(mbons);
    wvec(dan); wvec(oan); wvec(ser); wvec(dnv); wvec(gf); wvec(motor); wvec(asc);
    fclose(f);
    fprintf(stderr, "cache saved: %s\n", path);
}

bool Connectome::load(const char* path) {
    try {
        return load_impl(path);
    } catch (...) {
        fprintf(stderr, "cache corrupt or unreadable: %s\n", path);
        return false;
    }
}

bool Connectome::load_impl(const char* path) {
    FILE* f = fopen(path, "rb");
    if (!f) return false;
    auto rd = [&](void* dst, size_t size, size_t count) {
        return fread(dst, size, count, f) == count;
    };
    char magic[8];
    bool ok = rd(magic, 1, 8) && memcmp(magic, CACHE_MAGIC, 8) == 0;
    uint32_t ver = 0, N = 0, M = 0, sc = 0;
    if (ok) ok = rd(&ver, 4, 1) && ver == 1;
    if (ok) ok = rd(&N, 4, 1) && rd(&M, 4, 1);
    if (ok) ok = rd(&raw_rows, 8, 1) && rd(&unique_pairs, 8, 1) && rd(&total_syn, 8, 1);
    if (ok) ok = rd(&sc, 4, 1);
    if (ok) {
        strings.resize(sc);
        for (uint32_t i = 0; i < sc && ok; ++i) {
            uint32_t L;
            ok = rd(&L, 4, 1);
            if (ok) {
                strings[i].resize(L);
                if (L) ok = rd(&strings[i][0], 1, L);
            }
        }
    }
    if (ok) { neurons.resize(N); ok = rd(neurons.data(), sizeof(Neuron), N); }
    if (ok) { edges.resize(M);   ok = rd(edges.data(), sizeof(Edge), M); }
    if (ok) { dn_dom.resize(N);  ok = rd(dn_dom.data(), 1, N); }
    auto rvec = [&](std::vector<uint32_t>& v) {
        uint32_t c = 0;
        if (!ok) return;
        ok = rd(&c, 4, 1);
        if (!ok) return;
        v.resize(c);
        if (c) ok = rd(v.data(), 4, c);
    };
    rvec(photos); rvec(lamina); rvec(t4t5); rvec(lc_loom); rvec(hs_vs); rvec(mbons);
    rvec(dan); rvec(oan); rvec(ser); rvec(dnv); rvec(gf); rvec(motor); rvec(asc);
    fclose(f);
    if (!ok) { fprintf(stderr, "cache corrupt: %s\n", path); return false; }
    build_csr();
    return true;
}

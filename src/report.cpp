#include "report.h"
#include "csv.h"
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

// Charted columns of the metrics CSV (24th column "lesion" is text, not charted).
static const char* COLS[] = {
    "t", "gf", "lc4", "escape_drive", "escapes_cum", "satisfaction", "behavior",
    "motion", "transient", "contrast", "photo", "lamina", "t4t5", "hsvs", "mbon",
    "da", "oct", "ser", "dnleg", "dntct", "dnall", "motor", "all"
};
static const int NCOLS = (int)(sizeof(COLS) / sizeof(COLS[0]));

void write_html_report(const std::string& csv_path, const std::string& html_path,
                       const Metrics& M, double duration_s, int64_t nframes,
                       const char* wiring_label) {
    // ---- parse the CSV back ----
    std::vector<char> buf;
    if (!csv::read_file(csv_path.c_str(), buf)) {
        printf("[report] cannot read %s\n", csv_path.c_str());
        return;
    }
    char* p = buf.data();
    csv::Field f[32];
    csv::split_line(&p, f, 32);
    int ci[NCOLS];
    for (int c = 0; c < NCOLS; ++c) ci[c] = -1;
    for (size_t i = 0; i < 24 && f[i].p; ++i)
        for (int c = 0; c < NCOLS; ++c)
            if (f[i].is(COLS[c])) { ci[c] = (int)i; break; }

    if (ci[0] < 0) { printf("[report] csv lacks t column\n"); return; }
    std::vector<std::vector<double>> cols(NCOLS);
    int64_t rows = 0;
    while (*p) {
        size_t nf = csv::split_line(&p, f, 32);
        if (nf < 7) continue;
        for (int c = 0; c < NCOLS; ++c)
            if (ci[c] >= 0 && (size_t)ci[c] < nf)
                cols[c].push_back(f[ci[c]].to_double());
        ++rows;
    }
    if (rows < 2) { printf("[report] not enough data rows\n"); return; }
    // downsample to <= 12000 points per series
    int stride = (int)((rows + 11999) / 12000);

    auto num = [](double v, char* tmp) {
        if (!std::isfinite(v)) return std::string("null");  // NaN/Inf guard
        snprintf(tmp, 32, "%.5g", v);
        return std::string(tmp);
    };
    char tmp[32];
    std::string data = "{";
    bool first_col = true;
    for (int c = 0; c < NCOLS; ++c) {
        if (ci[c] < 0) continue;
        if (!first_col) data += ",";
        first_col = false;
        data += "\"" + std::string(COLS[c]) + "\":[";
        for (int64_t i = 0; i < rows; i += stride) {
            if (i) data += ",";
            double v = (i < (int64_t)cols[c].size()) ? cols[c][i] : 0.0;
            data += num(v, tmp);
        }
        data += "]";
    }
    data += "}";

    std::string events = "[";
    for (size_t i = 0; i < M.events.size(); ++i) {
        char e[96];
        snprintf(e, sizeof e, "[%.2f,%.4f,%.4f]", M.events[i].t, M.events[i].gf, M.events[i].lc4);
        if (i) events += ",";
        events += e;
    }
    events += "]";

    double sat_sum = 0, sat_min = 1, sat_max = 0;
    int64_t beh_cnt[BEH_COUNT] = {0};
    int64_t sat_n = 0;
    if (ci[5] >= 0 && ci[6] >= 0) {
        for (int64_t i = 0; i < rows; ++i) {
            if ((size_t)i >= cols[ci[5]].size() || (size_t)i >= cols[ci[6]].size()) continue;
            double s = cols[ci[5]][i];
            sat_sum += s;
            if (s < sat_min) sat_min = s;
            if (s > sat_max) sat_max = s;
            ++sat_n;
            int b = (int)(cols[ci[6]][i] + 0.5);
            if (b >= 0 && b < BEH_COUNT) ++beh_cnt[b];
        }
    }
    std::string summary = "{";
    char sb[512];
    const int64_t esc_n = (int64_t)M.events.size();  // single source: KPI, markers, table
    snprintf(sb, sizeof sb,
             "\"duration\":%.1f,\"frames\":%lld,\"escapes\":%lld,"
             "\"sat_mean\":%.3f,\"sat_min\":%.3f,\"sat_max\":%.3f,"
             "\"wiring\":\"%s\",\"beh\":[",
             duration_s, (long long)nframes, (long long)esc_n,
             sat_n ? sat_sum / sat_n : 0.0, sat_min, sat_max, wiring_label);
    summary += sb;
    for (int b = 0; b < BEH_COUNT; ++b) {
        snprintf(sb, sizeof sb, "%s%.1f", b ? "," : "",
                 rows ? 100.0 * beh_cnt[b] / rows : 0.0);
        summary += sb;
    }
    summary += "]}";

    // ---- HTML template ----
    std::string html = R"HTML(<!DOCTYPE html>
<html><head><meta charset="utf-8"><title>Цифровая муха — отчёт</title>
<style>
body{font-family:"Segoe UI",Arial,sans-serif;background:#12151b;color:#dce3ec;margin:0;padding:22px}
h1{font-size:21px;margin:0 0 4px} .sub{color:#8a97a8;font-size:13px;margin-bottom:14px}
.kpis{display:flex;gap:26px;flex-wrap:wrap;margin:14px 0 20px}
.kpi{background:#1b212b;border-radius:10px;padding:10px 16px;min-width:120px}
.kpi b{font-size:24px;display:block;color:#fff}.kpi span{font-size:12px;color:#8a97a8}
.card{background:#1b212b;border-radius:10px;padding:14px 16px;margin:14px 0}
.card h3{margin:0 0 8px;font-size:15px;font-weight:600}
canvas{width:100%;height:170px;display:block;cursor:crosshair}
.legend{font-size:12px;margin-top:8px;color:#aab6c5}.legend i{display:inline-block;width:10px;height:10px;border-radius:2px;margin:0 5px 0 12px}
#tip{position:fixed;background:#000d;border:1px solid #333c48;border-radius:8px;padding:7px 10px;font-size:12px;pointer-events:none;display:none;z-index:9;white-space:pre}
.foot{font-size:11px;color:#77839a;margin-top:6px;font-style:italic}
.evt{border-collapse:collapse;font-size:12px}
.evt th,.evt td{padding:4px 16px 4px 0;text-align:left;border-bottom:1px solid #2a3340}
.evt th{color:#8a97a8;font-weight:500}
</style></head><body>
<h1>Цифровая муха на коннектоме — результаты просмотра</h1>
<div class="sub" id="sub"></div>
<div class="kpis" id="kpis"></div>
<div class="card"><h3>Попытки побега: гигантское волокно DNp01 и детекторы лунинга LC4/LPLC2</h3>
<canvas id="c1"></canvas><div class="legend" id="l1"></div></div>
<div class="card"><h3>События побега</h3><div id="evtable"></div></div>
<div class="card"><h3>Индекс удовлетворённости (дофамин + MBON − октопамин − серотонин)</h3>
<canvas id="c2"></canvas><div class="legend" id="l2"></div>
<div class="foot">Индекс z-нормирован по бегущему окну: среднее ≈0.5 по построению — смотрите на динамику, а не на среднее.</div></div>
<div class="card"><h3>Поведение</h3><canvas id="c3"></canvas><div class="legend" id="l3"></div>
<div class="foot">Ходьба/полёт/груминг не активируются на статичных сценах — ограничение rate-модели (подробности в README).</div></div>
<div class="card"><h3>Активность нейронных популяций</h3><canvas id="c4"></canvas><div class="legend" id="l4"></div></div>
<div class="card"><h3>Зрительный вход (мушиный глаз)</h3><canvas id="c5"></canvas><div class="legend" id="l5"></div></div>
<div id="tip"></div>
<script>
const D=__DATA__,S=__SUMMARY__,EV=__EVENTS__;
const BEH=["покой","активность","ходьба/груминг","полёт","прыжок/побег"];
const BEHC=["#3b4754","#8d6e63","#388e3c","#1e88e5","#e53935"];
const sub=document.getElementById("sub");
sub.textContent="Проводка: "+(S.wiring==="real wiring"?"реальный коннектом мухи (FlyWire/Princeton, 166 700 нейронов)":"ПЕРЕМЕШАННЫЙ КОНТРОЛЬ (степени сохранены)")+
  " · "+S.duration.toFixed(1)+" c видео · "+S.frames+" кадров · шаг 5 мс · 768 омматидиев";
document.getElementById("kpis").innerHTML=
 `<div class="kpi"><b>${S.escapes}</b><span>попыток побега</span></div>
  <div class="kpi"><b>${S.sat_mean.toFixed(2)}</b><span>удовл. (среднее)</span></div>
  <div class="kpi"><b>${S.sat_min.toFixed(2)}–${S.sat_max.toFixed(2)}</b><span>удовл. (мин–макс)</span></div>`+
 BEH.map((n,i)=>`<div class="kpi"><b style="color:${BEHC[i]}">${S.beh[i].toFixed(1)}%</b><span>${n}</span></div>`).join("");
const N=D.t.length,T0=D.t[0],T1=D.t[N-1];
{const d=document.createElement("div");  // regression test: one source everywhere
 if(S.escapes!==EV.length){d.style.cssText="background:#8b1a1a;color:#fff;padding:10px 14px;border-radius:8px;margin:12px 0";
  d.textContent="REGRESSION FAIL: kpi="+S.escapes+" markers="+EV.length+" table="+EV.length;
  document.body.prepend(d);}}
{const et=document.getElementById("evtable");
 if(!EV.length)et.innerHTML="<div class='foot'>побегов не зарегистрировано</div>";
 else{let h="<table class='evt'><tr><th>время</th><th>пик DNp01 (GF)</th><th>пик LC4/LPLC2</th></tr>";
  for(const e of EV)h+=`<tr><td>${fmt(e[0])} &nbsp;(${e[0].toFixed(1)} с)</td><td>${e[1].toFixed(3)}</td><td>${e[2].toFixed(3)}</td></tr>`;
  h+="</table>";et.innerHTML=h;}}
function setup(cv){const r=cv.getBoundingClientRect(),d=devicePixelRatio||1;
 cv.width=r.width*d;cv.height=r.height*d;const x=cv.getContext("2d");x.scale(d,d);
 return {x,w:r.width,h:r.height};}
function fmt(s){const m=Math.floor(s/60),ss=Math.floor(s%60);return m+":"+String(ss).padStart(2,"0");}
function drawChart(id,series,opt){opt=opt||{};const cv=document.getElementById(id),{x,w,h}=setup(cv);
 const M={l:44,r:10,t:8,b:20};let lo=opt.lo,hi=opt.hi;
 if(lo===undefined||hi===undefined){lo=Infinity;hi=-Infinity;
  for(const s of series)for(const v of D[s.k]){if(v<lo)lo=v;if(v>hi)hi=v;}
  if(!isFinite(lo)){lo=0;hi=1;} if(hi-lo<1e-9){hi=lo+1;} if(opt.zero)lo=Math.min(0,lo);
  const pad=(hi-lo)*0.08;lo-=pad;hi+=pad;}
 x.clearRect(0,0,w,h);x.font="10px Segoe UI";
 x.strokeStyle="#2a3340";x.fillStyle="#77839a";x.lineWidth=1;
 for(let i=0;i<=4;i++){const y=M.t+(h-M.t-M.b)*i/4;
  x.beginPath();x.moveTo(M.l,y);x.lineTo(w-M.r,y);x.stroke();
  const v=hi-(hi-lo)*i/4;
  x.fillText(v>=10?v.toFixed(0):v>=1?v.toFixed(1):v.toFixed(3),4,y+3);}
 for(let i=0;i<=6;i++){const t=T0+(T1-T0)*i/6,px=M.l+(w-M.l-M.r)*i/6;
  x.strokeStyle="#1f2733";x.beginPath();x.moveTo(px,M.t);x.lineTo(px,h-M.b);x.stroke();
  x.fillText(fmt(t),px-14,h-6);}
 const X=t=>M.l+(w-M.l-M.r)*(t-T0)/(T1-T0||1),Y=v=>M.t+(h-M.t-M.b)*(hi-v)/(hi-lo);
 if(opt.fill){const s=series[0];x.beginPath();x.moveTo(X(D.t[0]),Y(D[s.k][0]));
  for(let i=1;i<N;i++)x.lineTo(X(D.t[i]),Y(D[s.k][i]));
  x.lineTo(X(D.t[N-1]),h-M.b);x.lineTo(X(D.t[0]),h-M.b);x.closePath();
  x.fillStyle=s.c+"22";x.fill();}
 for(const s of series){x.strokeStyle=s.c;x.lineWidth=s.wd||1.4;x.beginPath();
  x.moveTo(X(D.t[0]),Y(D[s.k][0]));
  for(let i=1;i<N;i++)x.lineTo(X(D.t[i]),Y(D[s.k][i]));x.stroke();}
 if(opt.events){x.strokeStyle="#ff5252";x.lineWidth=1;
  for(const e of EV){const px=X(e[0]);x.setLineDash([3,3]);
   x.beginPath();x.moveTo(px,M.t);x.lineTo(px,h-M.b);x.stroke();x.setLineDash([]);
   x.fillStyle="#ff5252";x.beginPath();x.arc(px,Y(e[1]),3,0,7);x.fill();}}
 if(opt.band){let i0=0;
  for(let i=1;i<=N;i++){if(i===N||D.behavior[i]!==D.behavior[i0]){
   x.fillStyle=BEHC[Math.max(0,Math.min(4,D.behavior[i0]|0))];
   x.fillRect(X(D.t[i0]),M.t+8,Math.max(1,X(D.t[Math.min(i,N-1)])-X(D.t[i0])),h-M.b-M.t-8);i0=i;}}
  x.strokeStyle="#2a3340";x.strokeRect(M.l,M.t+8,w-M.l-M.r,h-M.b-M.t-8);
  for(const e of EV){x.fillStyle="#fff";x.fillRect(X(e[0])-1,M.t+4,2,4);}}
 cv._geom={M,lo,hi,series,opt};
 const lg=document.getElementById(id.replace("c","l"));
 if(opt.band) lg.innerHTML=BEH.map((n,i)=>`<i style="background:${BEHC[i]}"></i>${n}`).join("")+"<i style=\"background:#fff\"></i>попытка побега";
 else lg.innerHTML=
  series.map(s=>`<i style="background:${s.c}"></i>${s.n}`).join("")+
  (opt.events?"<i style=\"background:#ff5252\"></i>попытка побега":"");}
function redraw(){
 drawChart("c1",[{k:"gf",n:"DNp01 (GF)",c:"#e53935",wd:1.8},{k:"lc4",n:"LC4/LPLC2",c:"#ffb300"}],{events:true,zero:true});
 drawChart("c2",[{k:"satisfaction",n:"удовлетворённость",c:"#ab47bc",wd:1.8}],{lo:0,hi:1,fill:true});
 drawChart("c3",[],{band:true});
 drawChart("c4",[{k:"lamina",n:"ламина",c:"#ffb300"},{k:"t4t5",n:"T4/T5",c:"#7cb342"},
  {k:"mbon",n:"MBON",c:"#26c6da"},{k:"da",n:"дофамин",c:"#ec407a"},
  {k:"oct",n:"октопамин",c:"#ff7043"},{k:"ser",n:"серотонин",c:"#5c6bc0"},
  {k:"dntct",n:"DN→тектулум",c:"#42a5f5"},{k:"motor",n:"моторные",c:"#bdbdbd"}],{zero:true});
 drawChart("c5",[{k:"motion",n:"движение (EMD)",c:"#26a69a"},{k:"transient",n:"транзиенты",c:"#ffa726"},
  {k:"contrast",n:"контраст",c:"#78909c"}],{zero:true});
}
document.querySelectorAll("canvas").forEach(cv=>{
 cv.addEventListener("mousemove",e=>{const g=cv._geom;if(!g)return;
  const r=cv.getBoundingClientRect(),x=e.clientX-r.left;
  const t=T0+(x-g.M.l)/(r.width-g.M.l-g.M.r)*(T1-T0);
  let best=0,bd=1e18;
  for(let i=0;i<N;i++){const d=Math.abs(D.t[i]-t);if(d<bd){bd=d;best=i;}}
  const tip=document.getElementById("tip");let s="t = "+fmt(D.t[best])+"  ("+D.t[best].toFixed(2)+" c)";
  if(g.opt&&g.opt.band)s+="\nповедение: "+BEH[Math.max(0,Math.min(4,D.behavior[best]|0))];
  else for(const sr of g.series)s+="\n"+sr.n+" = "+(+D[sr.k][best]).toFixed(4);
  tip.textContent=s;tip.style.display="block";
  tip.style.left=(e.clientX+14)+"px";tip.style.top=(e.clientY-10)+"px";});
 cv.addEventListener("mouseleave",()=>{document.getElementById("tip").style.display="none";});});
addEventListener("resize",redraw);redraw();
</script></body></html>)HTML";

    // splice in data
    auto put = [&](const char* ph, const std::string& val) {
        size_t pos;
        while ((pos = html.find(ph)) != std::string::npos) html.replace(pos, strlen(ph), val);
    };
    put("__DATA__", data);
    put("__EVENTS__", events);
    put("__SUMMARY__", summary);

    FILE* out = fopen(html_path.c_str(), "wb");
    if (!out) { printf("[report] cannot write %s\n", html_path.c_str()); return; }
    fwrite(html.data(), 1, html.size(), out);
    fclose(out);
    int64_t csv_cum = -1;
    if (ci[4] >= 0 && !cols[ci[4]].empty()) csv_cum = (int64_t)cols[ci[4]].back();
    printf("report: %s (%lld rows)\n", html_path.c_str(), (long long)rows);
    printf("consistency: kpi=%lld table=%lld markers=%lld (recorded csv_cum=%lld)\n",
           (long long)esc_n, (long long)esc_n, (long long)esc_n, (long long)csv_cum);
}

void regenerate_report(const char* csv_path, const char* wiring_label) {
    // Re-detect escape events from the GF series with the requested refractory
    // period (default 1.5 s) — one canonical event list for KPI, markers, table.
    std::vector<char> buf;
    if (!csv::read_file(csv_path, buf)) {
        printf("[report] cannot read %s\n", csv_path);
        return;
    }
    char* p = buf.data();
    csv::Field f[32];
    csv::split_line(&p, f, 32);
    int it = -1, igf = -1, ilc = -1;
    for (size_t i = 0; i < 24 && f[i].p; ++i) {
        if (f[i].is("t")) it = (int)i;
        else if (f[i].is("gf")) igf = (int)i;
        else if (f[i].is("lc4")) ilc = (int)i;
    }
    if (it < 0 || igf < 0) { printf("[report] csv lacks t/gf columns\n"); return; }
    std::vector<double> ts, gfs, lcs;
    while (*p) {
        size_t nf = csv::split_line(&p, f, 32);
        if (nf < 7) continue;
        ts.push_back(f[it].to_double());
        gfs.push_back(f[igf].to_double());
        if (ilc >= 0 && (size_t)ilc < nf) lcs.push_back(f[ilc].to_double());
    }
    Metrics M;
    M.events = detect_escapes(ts, gfs, lcs, g_escape_refractory);
    M.escapes_total = (int64_t)M.events.size();
    printf("[report] re-detected %zu escape events (refractory %.2f s)\n",
           M.events.size(), g_escape_refractory);
    write_html_report(csv_path, "out/report.html", M,
                      ts.empty() ? 0.0 : ts.back() + 1.0 / 30.0, (int64_t)ts.size(),
                      wiring_label);
}

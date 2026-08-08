/* 司南四层壳 web 原型。
 * 与固件同一份 tokens.json（由 design.h 导出）。
 * 目的：没到货也能评审 Rest / Work / Action / Interrupt 的构图与交互。
 */
"use strict";

const cv = document.getElementById("scr");
const ctx = cv.getContext("2d");
const logEl = document.getElementById("log");
const log = (s) => {
  logEl.textContent = `${new Date().toLocaleTimeString()}  ${s}\n` + logEl.textContent;
};

let T = null;   // tokens.json

/* ---------------- 状态（模拟固件 State） ---------------- */

const S = {
  layer: "Rest",        // Rest | Work | Action | Interrupt
  returnLayer: "Rest",
  mood: "Sleep",
  moodAt: 0,
  lastPoke: 0,

  workers: [
    { label: "CC",    state: "run",   quota: 0.44, task: "PLAN.md" },
    { label: "CODEX", state: "run",   quota: 0.62, task: "refactor hal" },
    { label: "MM",    state: "idle",  quota: 0.91, task: "" },
    { label: "GROK",  state: "stall", quota: 0.08, task: "waiting api" },
  ],
  focus: 0,
  approved: 7,
  denied: 2,
  beadFocus: 0,
  beadPhase: 0,

  prompt: null,   // {tool, hint, since, grave}
  passkey: null,
  notice: null,   // {kind, title, body, at}
  settled: null,  // {approved, at}

  talking: false,
  talkAt: 0,
};

const PROMPT_WINDOW = 90000;

/* ---------------- 绘制原语（对应 ring.cpp） ---------------- */

const C = (name) => T.colors[name];
const G = (name) => T.geometry[name];

function polar(r, deg) {
  const rad = ((deg - 90) * Math.PI) / 180;
  return [G("CENTER") + r * Math.cos(rad), G("CENTER") + r * Math.sin(rad)];
}

function arc(r, w, color, start, end, opa = 1) {
  ctx.beginPath();
  const a0 = ((start - 90) * Math.PI) / 180;
  const a1 = ((end - 90) * Math.PI) / 180;
  ctx.arc(G("CENTER"), G("CENTER"), r, a0, a1);
  ctx.strokeStyle = color;
  ctx.globalAlpha = opa;
  ctx.lineWidth = w;
  ctx.lineCap = "round";
  ctx.stroke();
  ctx.globalAlpha = 1;
}

function dot(r, deg, size, color, opa = 1) {
  const [x, y] = polar(r, deg);
  ctx.beginPath();
  ctx.arc(x, y, size, 0, Math.PI * 2);
  ctx.fillStyle = color;
  ctx.globalAlpha = opa;
  ctx.fill();
  ctx.globalAlpha = 1;
}

function text(s, x, y, size, color, align = "center") {
  ctx.fillStyle = color;
  ctx.font = `${size}px ui-monospace, monospace`;
  ctx.textAlign = align;
  ctx.textBaseline = "middle";
  ctx.fillText(s, x, y);
}

function chord(s, color) {
  if (s) text(s, G("CENTER"), G("Y_CHORD"), 13, color || C("BRONZE"));
}

function clear() {
  ctx.fillStyle = C("INK");
  ctx.beginPath();
  ctx.arc(G("CENTER"), G("CENTER"), G("R_MAX"), 0, Math.PI * 2);
  ctx.fill();
}

/* ---------------- 珠串（对应 beads.cpp 重建规则） ---------------- */

function beads() {
  const out = [];
  for (let i = 0; i < S.workers.length; i++) {
    const w = S.workers[i];
    if (w.state === "down") continue;
    let kind = "idle";
    if (w.state === "run") kind = "run";
    else if (w.state === "stall") kind = "stall";
    if (w.quota < 0.1) kind = "low";
    out.push({ kind, label: w.label, detail: w.task, color:
      kind === "run" ? C("MALACHITE") : kind === "stall" || kind === "low" ? C("AMBER") : C("BRONZE_D") });
  }
  if (S.approved) out.push({ kind: "appr", label: "A" + S.approved, detail: "approved today", color: C("MALACHITE") });
  if (S.denied)   out.push({ kind: "deny", label: "D" + S.denied, detail: "denied today", color: C("CINNABAR") });
  while (out.length < 18) out.push({ kind: "empty", color: C("LACQUER") });
  return out.slice(0, 18);
}

function drawBeads(now) {
  const bs = beads();
  bs.forEach((b, i) => {
    const deg = (i * 20 + S.beadPhase * 20) % 360;
    const focused = i === S.beadFocus;
    let opa = b.kind === "empty" ? 0.6 : focused ? 1 : 0.9;
    if ((b.kind === "run" || b.kind === "low") && !focused) {
      opa = 0.55 + 0.45 * Math.sin(now / 1300);   // T_BREATH 呼吸
    }
    dot(G("R_RIM"), deg, focused ? 6.5 : 5, b.color, opa);
    if (focused && b.kind !== "empty") {
      const [x, y] = polar(G("R_RIM"), deg);
      ctx.beginPath();
      ctx.arc(x, y, 7.5, 0, Math.PI * 2);
      ctx.strokeStyle = C("SILK");
      ctx.lineWidth = 1;
      ctx.stroke();
    }
  });
}

/* ---------------- 灵魂点阵（web 版用同心点示意） ---------------- */

function drawGhost(opa, hue) {
  ctx.save();
  ctx.globalAlpha = opa / 255;
  ctx.fillStyle = hue;
  // 点阵小狗的抽象：中心一团逐渐稀疏的点
  for (let i = 0; i < 260; i++) {
    const a = Math.random() * Math.PI * 2;
    const r = 96 * Math.sqrt(Math.random());
    const [x, y] = [G("CENTER") + r * Math.cos(a), G("CENTER") + r * Math.sin(a)];
    ctx.fillRect(x, y, 2, 2);
  }
  ctx.restore();
}

/* ---------------- 四层 ---------------- */

function drawRest(now) {
  // 晕影呼吸
  const amp = S.mood === "Sleep" || S.mood === "Dream" ? 6 : S.mood === "Peek" ? 3 : 0;
  const br = amp ? Math.sin((now / 1000) * ((2 * Math.PI * 18) / 60)) * amp : 0;
  for (let i = 0; i < 24; i++) {
    const t = i / 23;
    const r = G("R_SAFE") - 18 + i * 2 + br;
    arc(r, 3, C("INK"), 0, 359.9, t * t);
  }
  // 分钟弧
  const d = new Date();
  arc(G("R_RIM"), 4, C("BRONZE"), 0, ((d.getMinutes() * 60 + d.getSeconds()) / 3600) * 360);

  drawBeads(now);

  const moodOpa = { Sleep: 66, Dream: 66, Hear: 120, Alert: 24, Guard: 24, Praise: 200, Pet: 200, Peek: 66 };
  const moodHue = { Praise: C("MALACHITE"), Guard: C("CINNABAR"), Pet: C("SILK"), Hear: C("AMBER") };
  drawGhost(moodOpa[S.mood] ?? 66, moodHue[S.mood] ?? C("BRONZE_D"));

  if (S.mood === "Peek") {
    const grd = ctx.createLinearGradient(0, 466 - 190, 0, 466);
    grd.addColorStop(0, "rgba(0,0,0,0)");
    grd.addColorStop(1, "rgba(0,0,0,0.9)");
    ctx.fillStyle = grd;
    ctx.fillRect(0, 466 - 190, 466, 190);
    text(`${String(d.getHours()).padStart(2, "0")}:${String(d.getMinutes()).padStart(2, "0")}`,
         G("CENTER"), G("CENTER") + 118, 52, C("SILK"));
    text(`${String(d.getMonth() + 1).padStart(2, "0")}.${String(d.getDate()).padStart(2, "0")}`,
         G("CENTER"), G("CENTER") + 168, 13, C("BRONZE"));
  }

  const b = beads()[S.beadFocus];
  if (b && b.kind !== "empty") chord(`${b.label}  ${b.detail || ""}`, b.color);
}

function drawWork(now) {
  const n = S.workers.length || 1;
  const span = 360 / n;
  arc(G("R_RIM"), 4, C("MALACHITE"), 0, 359.9);
  S.workers.forEach((w, i) => {
    const start = i * span + 3, full = (i + 1) * span - 3;
    const used = start + (full - start) * w.quota;
    const hue = w.state === "run" ? C("MALACHITE") : w.state === "idle" ? C("BRONZE")
              : w.state === "stall" ? C("AMBER") : C("INDIGO");
    arc(G("R_ORBIT"), G("W_ORBIT") + 2, C("LACQUER"), start, full);
    let opa = 1;
    if (w.quota < 0.1 && w.state !== "down") opa = 0.5 + 0.5 * Math.sin(now / 1300);
    arc(G("R_ORBIT"), G("W_ORBIT"), hue, start, used, opa);
    const [x, y] = polar(G("R_ORBIT_IN") - 22, (start + full) / 2);
    text(w.label, x, y, 13, i === S.focus ? C("SILK") : C("BRONZE_D"));
  });
  const w = S.workers[S.focus];
  if (w) {
    text(Math.round(w.quota * 100) + "", G("CENTER"), G("CENTER") - 24, 64, C("SILK"));
    text(w.label, G("CENTER"), G("CENTER") + 52, 20, C("BRONZE"));
    chord(`${w.state}  ${w.task || "-"}`, C("BRONZE_D"));
  } else {
    text("--", G("CENTER"), G("CENTER") - 24, 64, C("SILK"));
    text("no fleet", G("CENTER"), G("CENTER") + 52, 20, C("BRONZE"));
  }
}

function drawAction() {
  arc(G("R_RIM"), 4, C("BRONZE"), 0, 359.9);
  arc(G("R_RIM"), G("W_RIM"), C("MALACHITE"), 108, 132);  // 石绿导轨 → OK/A 侧
  arc(G("R_RIM"), G("W_RIM"), C("CINNABAR"), 48, 72);      // 朱砂导轨 → NG/B 侧
  const names = ["FAST", "NG", "OK", "PLAN", "AI"];
  names.forEach((nm, i) => {
    const mid = i * 60;
    arc(G("R_ORBIT"), G("W_ORBIT") + 4, C("LACQUER"), mid - 17, mid + 17);
    const [x, y] = polar(G("R_ORBIT_IN") - 26, mid);
    text(nm, x, y, 13, C("BRONZE"));
  });
  arc(58, 3, C("BRONZE_D"), 0, 359.9);  // 闭着的耳朵
  text("TALK", G("CENTER"), G("CENTER") + 84, 13, C("BRONZE_D"));
  chord("A ok · B ng · hold center to talk");
}

function drawInterrupt(now) {
  if (S.settled) {
    const hue = S.settled.approved ? C("MALACHITE") : C("CINNABAR");
    arc(G("R_RIM"), G("W_RIM"), hue, 0, 359.9);
    text(S.settled.approved ? "ALLOWED" : "DENIED", G("CENTER"), G("CENTER") - 62, 32, hue);
    return;
  }
  if (S.passkey) {
    arc(G("R_RIM"), G("W_RIM"), C("AMBER"), 0, 359.9, 0.5 + 0.5 * Math.sin(now / 1300));
    text(S.passkey, G("CENTER"), G("CENTER") - 40, 64, C("SILK"));
    chord("type this code on your Mac", C("AMBER"));
    return;
  }
  if (S.prompt) {
    const p = S.prompt;
    const left = Math.max(0, 1 - (now - p.since) / PROMPT_WINDOW);
    const hue = p.grave ? C("CINNABAR") : C("MALACHITE");
    arc(G("R_RIM"), G("W_RIM"), C("LACQUER"), 0, 359.9);
    arc(G("R_RIM"), G("W_RIM"), hue, 0, left * 360);
    for (let i = 0; i < 36; i++) {
      if (i >= 15 && i <= 21) continue;
      if (p.grave || i % 4 === 0) dot(G("R_ORBIT"), i * 10, 1.5, hue);
    }
    text(p.tool, G("CENTER"), G("CENTER") - 62, 28, hue);
    text(p.hint.slice(0, 24), G("CENTER"), G("CENTER") + 6, 13, C("SILK_D"));
    if (p.grave) text("destructive pattern", G("CENTER"), G("CENTER") + 82, 13, hue);
    chord(p.grave ? "hold A · deny B" : "A allow · B deny", p.grave ? C("CINNABAR") : C("BRONZE"));
    return;
  }
  if (S.notice) {
    const err = S.notice.kind === "error";
    const hue = err ? C("CINNABAR") : C("MALACHITE");
    arc(G("R_RIM"), G("W_RIM"), hue, 0, 359.9);
    text(S.notice.title, G("CENTER"), G("CENTER") - 62, 32, hue);
    text(S.notice.body, G("CENTER"), G("CENTER") + 6, 13, C("SILK_D"));
    if (err) chord("B dismiss", C("BRONZE_D"));
  }
}

function drawTalk(now) {
  ctx.fillStyle = "rgba(0,0,0,0.8)";
  ctx.beginPath();
  ctx.arc(G("CENTER"), G("CENTER"), G("R_MAX"), 0, Math.PI * 2);
  ctx.fill();
  const used = now - S.talkAt;
  arc(G("R_RIM"), 4, C("MALACHITE"), 0, Math.max(0, 360 * (1 - used / 12000)));
  for (let i = 0; i < 20; i++) {
    const v = Math.random();
    const [x, y] = polar(G("R_ORBIT"), i * 18);
    ctx.save();
    ctx.translate(x, y);
    ctx.rotate((i * 18 * Math.PI) / 180);
    ctx.fillStyle = v > 0.55 ? C("MALACHITE") : C("BRONZE");
    ctx.fillRect(-2, -((6 + 40 * v) / 2), 4, 6 + 40 * v);
    ctx.restore();
  }
  chord("listening", C("BRONZE"));
}

/* ---------------- 行为（对应 shell.cpp 路由） ---------------- */

function setMood(m, why) {
  if (S.mood === m) return;
  log(`gaze mood ${S.mood}->${m} (${why})`);
  S.mood = m;
  S.moodAt = performance.now();
}

function setLayer(l) {
  if (S.layer === l) return;
  log(`layer ${S.layer} -> ${l}`);
  S.layer = l;
}

function pushInterrupt() {
  if (S.layer !== "Interrupt") {
    S.returnLayer = S.layer;
    setMood("Alert", "interrupt");
    setLayer("Interrupt");
  }
}

function popInterrupt() {
  setLayer(S.returnLayer);
  if (S.returnLayer === "Rest") setMood("Sleep", "interrupt over");
}

function key(k) {
  S.lastPoke = performance.now();
  if (S.talking) { if (k === "BShort") { S.talking = false; log("talk abort"); } return; }

  if (k === "Combo") {
    if (S.layer === "Rest") setLayer("Work");
    else if (S.layer === "Work") setLayer("Action");
    else if (S.layer === "Action") setLayer("Work");
    return;
  }

  if (S.layer === "Interrupt") {
    if (S.prompt) {
      if (k === "BShort" || (k === "AShort" && !S.prompt.grave) || (k === "ALong" && S.prompt.grave)) {
        const ok = k !== "BShort";
        S.settled = { approved: ok, at: performance.now() };
        if (ok) { S.approved++; setMood("Praise", "approved"); } else S.denied++;
        S.prompt = null;
        log(ok ? "decision: allow" : "decision: deny");
      }
    } else if (S.notice && k === "BShort") {
      S.notice = null;
      popInterrupt();
    }
    return;
  }

  if (S.layer === "Work" && k === "BShort") { S.focus = (S.focus + 1) % S.workers.length; log("focus -> " + S.workers[S.focus].label); }
  if (S.layer === "Work" && k === "AShort") log("hid send OK → enter");
  if (S.layer === "Action" && k === "AShort") log("action OK → enter");
  if (S.layer === "Action" && k === "BShort") log("action NG → esc");
  if (k === "BLong") {
    if (S.layer === "Work") setLayer("Rest");
    else if (S.layer === "Action") setLayer("Work");
    else if (S.layer === "Rest") log("open Settings（原型未实现）");
  }
  if (S.layer === "Rest" && k === "ALong") log("photo lock toggle");
}

/* ---------------- 触摸手势 ---------------- */

let touch = null;

cv.addEventListener("pointerdown", (e) => {
  const r = cv.getBoundingClientRect();
  const x = e.clientX - r.left, y = e.clientY - r.top;
  const dx = x - 233, dy = y - 233;
  touch = { x, y, r: Math.hypot(dx, dy), deg: (Math.atan2(dx, -dy) * 180) / Math.PI, at: performance.now(), acc: 0, last: performance.now() };
  if (touch.deg < 0) touch.deg += 360;
  S.lastPoke = performance.now();
});

cv.addEventListener("pointermove", (e) => {
  if (!touch) return;
  const r = cv.getBoundingClientRect();
  const x = e.clientX - r.left, y = e.clientY - r.top;
  const dx = x - 233, dy = y - 233;
  let deg = (Math.atan2(dx, -dy) * 180) / Math.PI;
  if (deg < 0) deg += 360;
  let d = deg - touch.deg;
  if (d > 180) d -= 360;
  if (d < -180) d += 360;
  touch.deg = deg;
  const now = performance.now();
  const v = (Math.abs(d) / Math.max(now - touch.last, 1)) * 1000;
  touch.last = now;

  if (touch.r > G("R_ORBIT_IN")) {
    touch.acc += d;
    if (Math.abs(touch.acc) >= 20) {
      const dir = touch.acc > 0 ? 1 : -1;
      touch.acc = 0;
      if (S.layer === "Rest") {
        S.beadFocus = (S.beadFocus + dir + 18) % 18;
        log("bead -> " + S.beadFocus);
      } else if (S.layer === "Work") {
        S.focus = (S.focus + dir + S.workers.length) % S.workers.length;
      }
    } else if (v < 240 && S.layer === "Rest" && Math.abs(touch.acc) > 6) {
      setMood("Pet", "rim stroke");
    }
  }
});

cv.addEventListener("pointerup", (e) => {
  if (!touch) return;
  const held = performance.now() - touch.at;
  const r = touch.r;
  if (S.talking) { S.talking = false; log("talk end"); touch = null; return; }
  if (held < 500 && Math.abs(touch.acc) < 8) {
    if (S.layer === "Rest" && r < 150) setMood("Peek", "tap center");
    else if (S.layer === "Work" && r < 150) log("select agent");
    else if (S.layer === "Action" && r > G("R_ORBIT") - 34 && r < G("R_ORBIT") + 34) {
      const names = ["FAST", "NG", "OK", "PLAN", "AI"];
      let best = -1, bd = 24;
      names.forEach((nm, i) => {
        let dd = Math.abs(touch.deg - i * 60);
        dd = Math.min(dd, 360 - dd);
        if (dd < bd) { bd = dd; best = i; }
      });
      if (best >= 0) log("action " + names[best] + " → hid");
    }
  }
  touch = null;
});

cv.addEventListener("pointercancel", () => (touch = null));

/* ---------------- 主循环 ---------------- */

function frame() {
  const now = performance.now();
  clear();

  // 心情回落
  if (S.mood === "Praise" && now - S.moodAt > 1000) setMood("Sleep", "praise done");
  if (S.mood === "Pet" && now - S.moodAt > 1200) setMood("Sleep", "pet done");
  if (S.mood === "Peek" && now - S.moodAt > 6000) setMood("Sleep", "peek timeout");
  if (S.mood === "Sleep" && now - S.lastPoke > 20000) setMood("Dream", "quiet");
  if (S.mood === "Dream" && now - S.moodAt > 5000) { S.beadPhase = (S.beadPhase + 1) % 18; S.moodAt = now; }

  // 中断编排
  const want = S.prompt || S.passkey || S.notice || S.settled;
  if (want && S.layer !== "Interrupt") pushInterrupt();
  if (!want && S.layer === "Interrupt") popInterrupt();
  if (S.settled && now - S.settled.at > 1180) { S.settled = null; popInterrupt(); }
  if (S.notice && S.notice.kind === "done" && now - S.notice.at > 1200) { S.notice = null; popInterrupt(); }

  // 中心长按对讲
  if (touch && !S.talking && touch.r < 60 && now - touch.at >= 500 && S.layer !== "Interrupt") {
    S.talking = true;
    S.talkAt = now;
    log("talk begin");
  }

  switch (S.layer) {
    case "Rest": drawRest(now); break;
    case "Work": drawWork(now); break;
    case "Action": drawAction(); break;
    case "Interrupt": drawInterrupt(now); break;
  }
  if (S.talking) drawTalk(now);

  requestAnimationFrame(frame);
}

/* ---------------- 控件 ---------------- */

document.getElementById("k-a").onclick = () => key("AShort");
document.getElementById("k-al").onclick = () => key("ALong");
document.getElementById("k-b").onclick = () => key("BShort");
document.getElementById("k-bl").onclick = () => key("BLong");
document.getElementById("k-ab").onclick = () => key("Combo");
document.getElementById("e-appr").onclick = () => {
  S.prompt = { tool: "Bash", hint: "rm -rf ~/Downloads/tmp", since: performance.now(), grave: true };
  log("inject approval (grave)");
};
document.getElementById("e-appr2").onclick = () => {
  S.prompt = { tool: "Read", hint: "~/notes/todo.md", since: performance.now(), grave: false };
  log("inject approval (normal)");
};
document.getElementById("e-pair").onclick = () => {
  S.passkey = String(Math.floor(Math.random() * 1000000)).padStart(6, "0");
  log("pairing passkey shown; 5s 后模拟配上");
  setTimeout(() => { S.passkey = null; log("paired"); }, 5000);
};
document.getElementById("e-err").onclick = () => {
  S.notice = { kind: "error", title: "ERROR", body: "codex: api timeout", at: performance.now() };
};
document.getElementById("e-done").onclick = () => {
  S.notice = { kind: "done", title: "DONE", body: "voice: 已注入焦点窗口", at: performance.now() };
};

fetch("tokens.json")
  .then((r) => r.json())
  .then((t) => {
    T = t;
    log("tokens loaded from design.h");
    requestAnimationFrame(frame);
  })
  .catch(() => {
    document.getElementById("log").textContent =
      "tokens.json 加载失败。先跑 python3 scripts/export_design_tokens.py，再用 http.server 打开。";
  });

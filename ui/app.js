/* ==========================================================================
   Saturator UI behaviour.

   Inside the plugin window every control is connected to a plugin parameter:
     knobs   -> "drive" "low" "high" "softness" "mix" "output"   (sliders)
     ON/OFF  -> "driveOn" "lowOn" "highOn" "softnessOn"          (toggles)
     tabs    -> "type"  (Tube / Tape / Transistor / Transformer) (choice)
   The chaos slider is still UI-only.

   In a normal browser (no plugin) everything runs locally, handy for designing.
   ========================================================================== */

(async () => {
  "use strict";

  const $  = (sel, root = document) => root.querySelector(sel);
  const $$ = (sel, root = document) => [...root.querySelectorAll(sel)];

  /* ---------- connection to the plugin ---------- */
  // JUCE adds window.__JUCE__ inside the plugin window. In a regular browser it's missing.
  let Juce = null;
  if (window.__JUCE__) {
    try { Juce = await import("./juce/index.js"); }
    catch (err) { console.warn("Could not load the JUCE bridge, running without the plugin:", err); }
  }
  const sliderStates = {};      // name -> JUCE slider state
  const toggleStates = {};      // name -> JUCE toggle state
  let typeState = null;         // JUCE choice state for the tabs

  /* ---------- fit the fixed-size stage into the window ---------- */
  const STAGE_W = 1035, STAGE_H = 693;
  const stage = $("#stage");
  let scale = 1;

  function fit() {
    scale = Math.min(window.innerWidth / STAGE_W, window.innerHeight / STAGE_H);
    const x = (window.innerWidth  - STAGE_W * scale) / 2;
    const y = (window.innerHeight - STAGE_H * scale) / 2;
    stage.style.transform = `translate(${x}px, ${y}px) scale(${scale})`;
  }
  window.addEventListener("resize", fit);
  fit();

  /* ---------- parameters (all normalised 0..1) ----------
     The starting values match the plugin's defaults (see createParameterLayout in
     PluginProcessor.cpp). Inside the plugin they're replaced by the real values. */
  const PARAMS = {
    // drive runs 0 .. 36 dB; default 6 dB. The circle shows the knob position as a percentage.
    drive:    { value: 6 / 36, format: v => Math.round(v * 100) + "%" },
    low:      { value: 0.5,    format: v => Math.round(v * 100) + "%" },
    high:     { value: 0.5,    format: v => Math.round(v * 100) + "%" },
    softness: { value: 0.5,    format: v => Math.round(v * 100) + "%" },
    mix:      { value: 1.0,    format: v => Math.round(v * 100) + "%" },
    // level out runs -24 dB .. +12 dB; default 0 dB
    output:   { value: 24 / 36, format: v => {
                  const db = Math.round(-24 + v * 36);
                  return (db > 0 ? "+" : "") + db + "db";
                } },
    chaos:    { value: 0.18, format: v => v.toFixed(2) },       // UI-only for now
  };
  for (const p of Object.values(PARAMS)) p.initial = p.value;

  const clamp01 = v => Math.min(1, Math.max(0, v));

  // Change a value (and tell the plugin). fromPlugin = true when the plugin itself changed it.
  function setParam(name, v, fromPlugin = false) {
    const p = PARAMS[name];
    if (!p) return;
    p.value = clamp01(v);
    renderParam(name);
    if (!fromPlugin && sliderStates[name]) sliderStates[name].setNormalisedValue(p.value);
    document.dispatchEvent(new CustomEvent("param", { detail: { name, value: p.value } }));
  }

  // One quick change that isn't a drag (wheel, arrow keys, double-click reset)
  function nudgeParam(name, v) {
    const s = sliderStates[name];
    if (s) s.sliderDragStarted();
    setParam(name, v);
    if (s) s.sliderDragEnded();
  }

  // Connect each knob to its plugin parameter, and follow changes made by the host (automation, presets)
  if (Juce) {
    for (const name of Object.keys(PARAMS)) {
      if (name === "chaos") continue;
      const s = Juce.getSliderState(name);
      sliderStates[name] = s;
      PARAMS[name].value = s.getNormalisedValue();
      s.valueChangedEvent.addListener(() => setParam(name, s.getNormalisedValue(), true));
    }
  }

  function renderParam(name) {
    const p = PARAMS[name];
    const el = $(`.value[data-param="${name}"]`);
    if (el) {
      el.textContent = p.format(p.value);
      el.classList.toggle("small", el.textContent.length > 4);
      el.setAttribute("aria-valuenow", Math.round(p.value * 100));
    }
    if (name === "chaos") drawScribble(p.value);
  }

  /* ---------- circles: drag up/down, shift = fine, double-click = reset ---------- */
  $$(".value[data-param]").forEach(el => {
    const name = el.dataset.param;
    let startY = 0, startV = 0, dragging = false;

    el.addEventListener("pointerdown", e => {
      el.setPointerCapture(e.pointerId);
      el.classList.add("dragging");
      startY = e.clientY;
      startV = PARAMS[name].value;
      dragging = true;
      if (sliderStates[name]) sliderStates[name].sliderDragStarted();   // lets the host record automation
      e.preventDefault();
    });
    el.addEventListener("pointermove", e => {
      if (!el.hasPointerCapture(e.pointerId)) return;
      const range = e.shiftKey ? 1000 : 200;                 // screen px for a full sweep
      setParam(name, startV + (startY - e.clientY) / (range * scale));
    });
    const end = e => {
      el.classList.remove("dragging");
      if (el.hasPointerCapture(e.pointerId)) el.releasePointerCapture(e.pointerId);
      if (dragging && sliderStates[name]) sliderStates[name].sliderDragEnded();
      dragging = false;
    };
    el.addEventListener("pointerup", end);
    el.addEventListener("pointercancel", end);

    el.addEventListener("dblclick", () => nudgeParam(name, PARAMS[name].initial));
    el.addEventListener("wheel", e => {
      e.preventDefault();
      nudgeParam(name, PARAMS[name].value - e.deltaY / (e.shiftKey ? 2000 : 500));
    }, { passive: false });
    el.addEventListener("keydown", e => {
      const step = e.shiftKey ? 0.1 : 0.01;
      if (e.key === "ArrowUp"   || e.key === "ArrowRight") { nudgeParam(name, PARAMS[name].value + step); e.preventDefault(); }
      if (e.key === "ArrowDown" || e.key === "ArrowLeft")  { nudgeParam(name, PARAMS[name].value - step); e.preventDefault(); }
    });
  });

  /* ---------- on/off switches ---------- */
  const switches = {};

  // Show a switch as on or off. fromPlugin = true when the plugin itself changed it.
  function setSwitch(name, on, fromPlugin = false) {
    const btn = $(`.toggle[data-toggle="${name}"]`);
    if (!btn) return;
    btn.classList.toggle("on", on);
    btn.setAttribute("aria-pressed", on);
    $(".knob", btn).textContent = on ? "ON" : "OFF";
    btn.closest(".row").classList.toggle("off", !on);
    switches[name] = on;
    if (!fromPlugin && toggleStates[name]) toggleStates[name].setValue(on);
    document.dispatchEvent(new CustomEvent("toggle", { detail: { name, on } }));
  }

  $$(".toggle[data-toggle]").forEach(btn => {
    const name = btn.dataset.toggle;
    switches[name] = true;
    btn.addEventListener("click", () => setSwitch(name, !btn.classList.contains("on")));

    // plugin parameter is called e.g. "driveOn"
    if (Juce) {
      const s = Juce.getToggleState(name + "On");
      toggleStates[name] = s;
      setSwitch(name, s.getValue(), true);
      s.valueChangedEvent.addListener(() => setSwitch(name, s.getValue(), true));
    }
  });

  /* ---------- chaos slider + scribble ---------- */
  const chaosInput = $("#chaos");
  chaosInput.addEventListener("input", () => setParam("chaos", chaosInput.value / 1000));

  function rng(seed) {                                       // small seeded random: scribble stays the same each time
    return () => {
      seed |= 0; seed = (seed + 0x6D2B79F5) | 0;
      let t = Math.imul(seed ^ (seed >>> 15), 1 | seed);
      t = (t + Math.imul(t ^ (t >>> 7), 61 | t)) ^ t;
      return ((t ^ (t >>> 14)) >>> 0) / 4294967296;
    };
  }

  /* Pen scribble.
     - Thin ink lines.
     - The more chaos, the MORE lines (8 at 0 up to ~200 at max)...
     - ...and the more their directions fan out from "mostly sideways" to every angle,
       while the scribble spreads from a small band to fill the whole box.
     Every line has fixed random properties, so turning chaos up ADDS lines and
     swings them around instead of redrawing a different picture each time. */
  // The scribble canvas is the whole grey CHAOS card (426 x 396 stage px).
  // At low chaos it sits as a small band; at full chaos it covers the entire card.
  const CARD = { w: 426, h: 396 };
  const lerp = (a, b, t) => a + (b - a) * t;
  const MAX_LINES = 800;
  const STRAIGHT_SHARE = 0.5;   // fraction of lines that are perfectly straight (0 = all squiggly, 1 = all straight)
  const SQUIGGLE_AMP   = [1.5, 5];     // how far a squiggle wobbles off its line (stage px, min..max)
  const SQUIGGLE_WAVE  = [9, 24];      // distance between wobbles (stage px, min..max): smaller = tighter squiggle

  function drawScribble(chaos) {
    const g = $("#strokes");
    const NS = "http://www.w3.org/2000/svg";
    const r = rng(11);

    const count  = Math.round(8 + chaos * (MAX_LINES - 8));
    const spread = Math.PI * Math.pow(chaos, 0.85);          // how far lines may turn away from horizontal
    const halfW  = lerp(120, 207, chaos);                    // scribble grows outward from the middle...
    const halfH  = lerp(24, 192, chaos);                     // ...until it reaches the edges of the card
    const cx     = CARD.w / 2;
    const cy     = lerp(172, CARD.h / 2, chaos);             // centre drifts from the small band's spot to the card's middle
    const bx     = lerp(183, 207, chaos);                    // how far lines may reach (half-width / half-height of the limit box)
    const by     = lerp(72, 192, chaos);
    const lim    = { x0: cx - bx, x1: cx + bx, y0: cy - by, y1: cy + by };

    g.textContent = "";

    for (let i = 0; i < MAX_LINES; i++) {
      // always draw the same random numbers for line i, so it is stable as chaos changes
      const px    = cx + (r() * 2 - 1) * halfW;
      const py    = cy + (r() * 2 - 1) * halfH;
      const turn  = r() + r() - 1;                           // -1..1, most lines stay near 0
      const len   = 70 + r() * 230;
      const squiggly = r() >= STRAIGHT_SHARE;                // some lines are straight...
      const amp   = lerp(SQUIGGLE_AMP[0], SQUIGGLE_AMP[1], r());              // ...others wiggle back and forth
      const wave  = (Math.PI * 2) / lerp(SQUIGGLE_WAVE[0], SQUIGGLE_WAVE[1], r());
      const phase = r() * Math.PI * 2;
      const width = 1.1 + r() * 0.8;
      const alpha = 0.7 + r() * 0.3;
      if (i >= count) continue;

      const a  = turn * spread;
      const dx = Math.cos(a), dy = Math.sin(a);

      // keep both ends of the line inside the box
      let half = len / 2;
      if (Math.abs(dx) > 1e-6) half = Math.min(half, Math.min(lim.x1 - px, px - lim.x0) / Math.abs(dx));
      if (Math.abs(dy) > 1e-6) half = Math.min(half, Math.min(lim.y1 - py, py - lim.y0) / Math.abs(dy));
      half = Math.max(half, 6);

      // Walk along the line. Straight lines only need their two end points;
      // squiggly ones get a point every ~3px, pushed sideways by a wobble.
      const steps = squiggly ? Math.max(2, Math.round((half * 2) / 3)) : 1;
      let d = "";
      for (let s = 0; s <= steps; s++) {
        const along = (s / steps * 2 - 1) * half;            // -half .. +half from the middle
        let off = 0;
        if (squiggly) {
          const u = along + half;                            // distance travelled from the start
          off = Math.sin(u * wave + phase) * amp
              + Math.sin(u * wave * 2.3 + phase * 1.7) * amp * 0.35;   // second wave so it isn't perfectly regular
        }
        const x = px + dx * along - dy * off;
        const y = py + dy * along + dx * off;
        d += `${s ? " L" : "M"}${x.toFixed(1)},${y.toFixed(1)}`;
      }

      const path = document.createElementNS(NS, "path");
      path.setAttribute("d", d);
      path.setAttribute("stroke-width", width.toFixed(2));
      path.setAttribute("opacity", alpha.toFixed(2));
      g.appendChild(path);
    }
    $("#disp").setAttribute("scale", "1.4");                 // just enough roughness to feel like pen on paper
  }

  /* ---------- navigation between screens ---------- */
  const TYPES = ["tube", "tape", "transistor", "transformer"];
  const viewHome    = $("#view-home");
  const viewPanel   = $("#view-panel");
  const viewCredits = $("#view-credits");

  let currentView = "home";

  // Show a screen. Opening a saturation type also selects it in the plugin,
  // unless fromPlugin is true (the plugin told us) or it's just the page loading.
  function go(name, fromPlugin = false) {
    if (!TYPES.includes(name) && name !== "credits") name = "home";
    currentView = name;

    viewHome.hidden    = name !== "home";
    viewCredits.hidden = name !== "credits";
    viewPanel.hidden   = !TYPES.includes(name);

    if (TYPES.includes(name)) {
      viewPanel.style.setProperty("--accent", `var(--${name})`);
      $$(".tab", viewPanel).forEach(t => t.classList.toggle("active", t.dataset.go === name));
      if (!fromPlugin && typeState) typeState.setChoiceIndex(TYPES.indexOf(name));
      document.dispatchEvent(new CustomEvent("type", { detail: { name } }));
    }

    // remember where we are, so a reload while editing keeps you on the same screen
    try { history.replaceState(null, "", "#" + name); } catch (_) { /* fine */ }
  }

  document.addEventListener("click", e => {
    const target = e.target.closest("[data-go]");
    if (target) go(target.dataset.go);
  });
  document.addEventListener("keydown", e => { if (e.key === "Escape") go("home"); });
  window.addEventListener("hashchange", () => go(location.hash.replace("#", "") || "home", true));

  // The tabs follow the plugin's "type" parameter (host automation, presets, loading a project)
  if (Juce) {
    typeState = Juce.getComboBoxState("type");
    typeState.valueChangedEvent.addListener(() => {
      const name = TYPES[typeState.getChoiceIndex()];
      if (name && !viewPanel.hidden && name !== currentView) go(name, true);
    });
  }

  /* ---------- start ---------- */
  Object.keys(PARAMS).forEach(renderParam);
  go(location.hash.replace("#", "") || "home", true);      // just showing the page: don't change the plugin's type
})();

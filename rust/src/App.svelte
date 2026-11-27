<script lang="ts">
  import { onMount, tick as rendered } from "svelte";
  import { invoke } from "@tauri-apps/api/core";
  import { listen } from "@tauri-apps/api/event";
  import { getCurrentWindow } from "@tauri-apps/api/window";
  import Calendar from "./Calendar.svelte";
  import { filterText, isView, type EndMode, type Field, type Fields, type Tick, type View } from "./types";

  const win = getCurrentWindow();
  const pad = (n: number) => String(n).padStart(2, "0");
  const hhmm = (m: number) => (m < 0 ? "--:--" : `${pad(Math.floor(m / 60))}:${pad(m % 60)}`);
  const clock = (s: number) => `${pad(Math.floor(s / 3600))}:${pad(Math.floor((s % 3600) / 60))}:${pad(s % 60)}`;
  const money = (v: number, hide: boolean) => (hide ? "****" : `¥${v.toFixed(2)}`);
  const lunchFields: Field[] = ["lunchStart", "lunchEnd"];
  // Local civil date (toISOString would give the UTC day around midnight).
  const localToday = (d = new Date()) => `${d.getFullYear()}-${pad(d.getMonth() + 1)}-${pad(d.getDate())}`;

  let view = $state<View | null>(null);
  let t = $state<Tick | null>(null);
  let fields = $state<Fields>({ start: "", end: "", salary: "", lunchStart: "", lunchEnd: "", resignation: "" });
  let focused: Field | null = null;
  let expanded = $state(false);
  let dragging = $state(false);
  let error = $state("");
  let inlineCalendar = $state(false);
  let dateInput = $state<HTMLInputElement>();
  let resizing: { x: number; scale: number } | null = null;
  let mainEl = $state<HTMLElement>();
  let panelEl = $state<HTMLElement>();

  /** Scrolls to the end and checks the last control is inside the window. */
  function layoutReport() {
    for (const el of [panelEl, mainEl]) if (el) el.scrollTop = el.scrollHeight;
    const rect = (el: Element | null | undefined) => {
      const r = el?.getBoundingClientRect();
      return r ? { top: r.top, bottom: r.bottom, left: r.left, right: r.right } : null;
    };
    const controls = Array.from(panelEl?.querySelectorAll("input, button, select") ?? []);
    // Really visible: the top-most element at its centre is it (or inside it),
    // which catches clipping by any ancestor, not just the window edge.
    const hit = (el: Element | null | undefined) => {
      if (!el) return false;
      const r = el.getBoundingClientRect();
      const top = document.elementFromPoint(r.left + r.width / 2, r.top + r.height / 2);
      return !!top && (top === el || el.contains(top) || top.contains(el));
    };
    return {
      visible: { lastControl: hit(controls.at(-1)), status: hit(panelEl?.querySelector(".status")), header: hit(mainEl?.querySelector("header strong")) },
      viewport: { width: innerWidth, height: innerHeight },
      scale: s?.scalePercent,
      main: rect(mainEl),
      panel: panelEl ? { ...rect(panelEl), scrollHeight: panelEl.scrollHeight, clientHeight: panelEl.clientHeight } : null,
      lastControl: rect(controls.at(-1)),
      status: rect(panelEl?.querySelector(".status")),
      controls: controls.length,
    };
  }

  const s = $derived(view?.settings);

  function show(next: View, keep: Field | null = null) {
    view = next;
    fields = keep ? { ...next.fields, [keep]: fields[keep] } : { ...next.fields };
    error = next.error ?? next.autostart.error ?? "";
  }
  // The clock plays back a one-minute batch computed in Rust (one IPC round
  // trip a minute instead of every second) and stops while hidden.
  let batch: { startMs: number; ticks: Tick[] } | null = null;
  let visible = true;
  let timer: ReturnType<typeof setTimeout> | undefined;
  function advance(): boolean {
    if (!batch) return false;
    const index = Math.floor((Date.now() - batch.startMs) / 1000);
    if (index < 0 || index >= batch.ticks.length) return false;
    t = batch.ticks[index];
    return true;
  }
  async function refresh() {
    try {
      batch = await invoke<{ startMs: number; ticks: Tick[] }>("ticks", { count: 60 });
      advance();
    } catch (e) {
      error = String(e);
    }
  }
  function loop() {
    clearTimeout(timer);
    timer = undefined;
    if (!visible || document.hidden) return; // resumed by the visibility events
    if (!advance()) void refresh();
    timer = setTimeout(loop, 1000 - (Date.now() % 1000) + 5);
  }
  function setVisible(next: boolean) {
    visible = next;
    if (next) void refresh().then(loop);
    else loop();
  }
  // Applies a command's View. Anything else is an error, never a blank widget.
  async function call(cmd: string, args: Record<string, unknown> = {}, keep: Field | null = null) {
    try {
      const next = await invoke<unknown>(cmd, args);
      if (!isView(next)) throw new Error(`${cmd} 返回了无效数据`);
      show(next, keep);
      await refresh();
    } catch (e) {
      error = String(e);
    }
  }

  // Focus rules from the C++ editor: lunch is one interval, so moving
  // between its endpoints only applies a complete pair; anything else
  // commits the field being left.
  async function leave(prev: Field | null, next: Field | null) {
    if (!prev) return;
    if (next && lunchFields.includes(prev) && lunchFields.includes(next)) {
      const v = await invoke<View | null>("live_field", { field: prev, fields: $state.snapshot(fields) });
      if (v) view = v;
      return;
    }
    await call("commit_field", { field: prev, fields: $state.snapshot(fields) }, next);
  }
  async function onfocus(field: Field) {
    const prev = focused;
    focused = field;
    if (prev !== field) await leave(prev, field);
  }
  function onblur(field: Field) {
    setTimeout(() => {
      const active = document.activeElement as HTMLElement | null;
      if (focused === field && !active?.dataset.field) {
        focused = null;
        void leave(field, null);
      }
    });
  }
  async function oninput(field: Field, e: Event) {
    const input = e.currentTarget as HTMLInputElement;
    const text = filterText(field, input.value, view!.settings.endMode);
    if (text !== input.value) input.value = text;
    fields[field] = text;
    error = "";
    if (field === "end" || lunchFields.includes(field)) {
      const v = await invoke<View | null>("live_field", { field, fields: $state.snapshot(fields) });
      if (v) {
        view = v;
        await refresh();
      }
    }
  }
  function onkeydown(field: Field, e: KeyboardEvent) {
    const input = e.currentTarget as HTMLInputElement;
    if (e.key === "Enter") {
      e.preventDefault();
      input.blur();
    } else if (e.key === "Escape") {
      e.preventDefault();
      fields[field] = view!.fields[field];
      input.value = fields[field];
      input.blur();
    } else if (field === "resignation" && (e.key === "F4" || e.key === "ArrowDown")) {
      e.preventDefault();
      void openCalendar();
    }
  }

  async function openCalendar() {
    if (!view?.pinSupported) {
      await call("commit_field", { field: "resignation", fields: $state.snapshot(fields) });
      inlineCalendar = true;
      return;
    }
    const r = dateInput!.getBoundingClientRect();
    focused = null;
    await call("open_calendar", { anchor: { x: r.x, y: r.y, w: r.width, h: r.height }, fields: $state.snapshot(fields) });
  }
  async function inlineFinish(finish: object) {
    inlineCalendar = false;
    await call("calendar_finish", { finish });
    dateInput?.focus();
  }

  async function panel(open: boolean) {
    if (!open && focused) {
      const prev = focused;
      focused = null;
      await leave(prev, null);
    }
    expanded = open;
    inlineCalendar = false;
    await invoke("set_expanded", { expanded: open });
  }
  async function setMode(mode: EndMode) {
    await call("set_end_mode", { mode, fields: $state.snapshot(fields) });
  }
  function windowPrefs(scalePercent: number, opacityPercent: number, persist: boolean) {
    void call("set_window", { scale: scalePercent, opacity: opacityPercent, persist }, focused);
  }
  // The window's left edge stays put while resizing, so the pointer's page x
  // (CSS px, scaled by the zoom) plus its offset from the right edge is the
  // new width; 360 CSS px is 100 %.
  function grip(e: PointerEvent) {
    const el = e.currentTarget as HTMLElement;
    if (e.type === "pointerdown") {
      el.setPointerCapture(e.pointerId);
      resizing = { x: innerWidth - e.clientX, scale: s!.scalePercent };
    } else if (resizing && e.type === "pointermove") {
      const width = (e.clientX + resizing.x) * (s!.scalePercent / 100);
      const next = Math.min(150, Math.max(75, Math.round((width / 360) * 100)));
      if (next !== s!.scalePercent) windowPrefs(next, s!.opacityPercent, false);
    } else if (resizing) {
      resizing = null;
      windowPrefs(s!.scalePercent, s!.opacityPercent, true);
    }
  }

  onMount(() => {
    const clean: (() => void)[] = [];
    async function init() {
      show(await invoke<View>("state"));
      await refresh();
      clean.push(await listen<View>("settings", (e) => show(e.payload, focused)));
      clean.push(
        await listen<View>("calendar-closed", async (e) => {
          show(e.payload);
          await rendered();
          dateInput?.focus();
        }),
      );
      clean.push(await listen<View["update"]>("update", (e) => view && (view.update = e.payload)));
      // Smoke-test hook (OFFWORK_MEASURE): open the calendar the way F4 does.
      clean.push(
        await listen("measure-open-calendar", async () => {
          if (!expanded) await panel(true);
          await rendered();
          await openCalendar();
        }),
      );
      clean.push(
        await listen<string>("ocr-error", async (e) => {
          if (!expanded) await panel(true);
          error = e.payload;
        }),
      );
      clean.push(
        await win.onDragDropEvent((e) => {
          dragging = e.payload.type === "over" || e.payload.type === "enter";
        }),
      );
      clean.push(await listen<boolean>("widget-visibility", (e) => setVisible(e.payload)));
      const onVisibility = () => setVisible(!document.hidden);
      document.addEventListener("visibilitychange", onVisibility);
      clean.push(() => document.removeEventListener("visibilitychange", onVisibility));
      // Smoke-test hook (OFFWORK_MEASURE): expand and report whether every
      // control is reachable inside the real window (scripts/layout_smoke.py).
      clean.push(
        await listen("measure-layout-check", async () => {
          if (!expanded) await panel(true);
          await new Promise((r) => setTimeout(r, 800));
          await invoke("measure_layout", { report: layoutReport() });
        }),
      );
      await rendered();
      await win.show();
      requestAnimationFrame(() => requestAnimationFrame(() => void invoke("first_frame")));
      loop();
    }
    void init().catch(async (e) => {
      error = String(e);
      await win.show();
    });
    return () => {
      visible = false;
      clearTimeout(timer);
      clean.forEach((fn) => fn());
    };
  });

  const title = $derived(
    !t ? "" : t.phase === "Before" ? "距离上班" : t.phase === "After" ? "今日班次结束"
      : s?.deductLunch && !t.onLunch ? "距离下班 · 已扣午休" : "距离下班",
  );
  const big = $derived(!t ? "" : t.onLunch ? "午休中" : t.phase === "After" ? "已下班" : clock(t.countdown));
  const percent = $derived(t ? Math.round(t.progress * 100) : 0);
</script>

{#snippet input(field: Field, label: string, placeholder: string)}
  <label>
    <span>{label}</span>
    <input
      data-field={field}
      value={fields[field]}
      {placeholder}
      inputmode={field === "salary" || (field === "end" && s?.endMode === "duration") ? "decimal" : "numeric"}
      type={field === "salary" && s?.hideEarned ? "password" : "text"}
      autocomplete="off"
      spellcheck="false"
      onfocus={() => onfocus(field)}
      onblur={() => onblur(field)}
      oninput={(e) => oninput(field, e)}
      onkeydown={(e) => onkeydown(field, e)}
    />
  </label>
{/snippet}

<div class="frame" style:opacity={(s?.opacityPercent ?? 100) / 100}>
  <main class:drag={dragging} class:expanded bind:this={mainEl}>
    <header data-tauri-drag-region>
      <img src="/cat.svg" alt="" />
      <div class="title" data-tauri-drag-region>
        <strong data-tauri-drag-region>OffWork</strong>
        <small data-tauri-drag-region>{t ? `${t.date} ${t.time}` : ""}</small>
      </div>
      {#if view?.update.available}
        <button
          class="update"
          aria-label={view.update.installs ? `下载并安装 ${view.update.available}` : `查看新版本 ${view.update.available}`}
          title={view.update.error ?? `发现新版本 ${view.update.available}`}
          disabled={view.update.downloading}
          onclick={() => call("start_update")}
        >
          {view.update.downloading ? (view.update.percent >= 0 ? `${view.update.percent}%` : "下载中") : "↑ 更新"}
        </button>
      {/if}
      <button
        aria-pressed={view?.pinned ?? false}
        disabled={!view?.pinSupported}
        title={view?.pinSupported ? "切换置顶" : "当前桌面不支持置顶"}
        onclick={() => call("set_pinned", { pinned: !view?.pinned })}
      >
        {view?.pinned ? "已置顶" : "普通窗口"}
      </button>
      <button aria-label={view?.tray ? "隐藏到托盘" : "退出"} onclick={() => win.close()}>×</button>
    </header>

    {#if t && s}
      <section class="summary" aria-label="今日概览">
        <div class="row">
          <h1>{title}</h1>
          {#if t.days !== null}<span class="stamp">剩余 {t.days} 天离职</span>{/if}
        </div>
        <time class:words={t.onLunch || t.phase === "After"}>{big}</time>
        <div class="row"><small>今日进度</small><small>{percent}%</small></div>
        <div class="track" role="progressbar" aria-label="今日进度" aria-valuemin={0} aria-valuemax={100} aria-valuenow={percent}>
          <div class="fill" style:width="{t.progress * 100}%"></div>
        </div>
        <div class="row muted"><small>{hhmm(s.startMinutes)} 上班</small><small>{hhmm(t.endMinutes)} 下班</small></div>
        <div class="money">
          <div class="row">
            <small>今天已经赚了</small>
            <button class="eye" aria-pressed={s.hideEarned} aria-label={s.hideEarned ? "显示金额" : "隐藏金额"} onclick={() => call("toggle", { flag: "hideEarned" }, focused)}>
              {s.hideEarned ? "◡ 显示" : "◉ 隐藏"}
            </button>
          </div>
          <div class="row">
            <b>{money(t.earned, s.hideEarned)}</b><small>日薪 {money(s.dailySalary, s.hideEarned)}</small>
          </div>
        </div>
      </section>
      <footer>
        <button class="settings" aria-expanded={expanded} aria-controls="panel" onclick={() => panel(!expanded)}>
          设置 {expanded ? "︿" : "﹀"}
        </button>
        {#if !expanded}<span class="version">v{view?.version}</span>{/if}
      </footer>
    {/if}

    {#if expanded && s && view}
      <form id="panel" class="panel" bind:this={panelEl} onsubmit={(e) => e.preventDefault()}>
        <fieldset>
          <legend>我的班次</legend>
          {@render input("start", "上班时间", "09:00")}
          {@render input("salary", "日薪（元）", "500")}
          <div class="mode" role="radiogroup" aria-label="下班方式">
            <span>下班方式</span>
            <div>
              <button type="button" role="radio" aria-checked={s.endMode === "endTime"} onclick={() => setMode("endTime")}>固定时间</button>
              <button type="button" role="radio" aria-checked={s.endMode === "duration"} onclick={() => setMode("duration")}>按净工时</button>
            </div>
          </div>
          {@render input("end", s.endMode === "duration" ? "净工时（小时）" : "下班时间", s.endMode === "duration" ? "7.5" : "18:00")}
          {@render input("lunchStart", "午休开始", "留空表示无午休")}
          {@render input("lunchEnd", "午休结束", "")}
          <label class="check wide"><input type="checkbox" checked={s.deductLunch} onchange={() => call("toggle", { flag: "deductLunch" }, focused)} />倒计时扣除剩余午休（只改显示）</label>
        </fieldset>
        <fieldset>
          <legend>离职日期</legend>
          <label class="date">
            <span>最后一天（YYYY-MM-DD）</span>
            <span class="pair">
              <input
                bind:this={dateInput}
                data-field="resignation"
                value={fields.resignation}
                placeholder="未设置"
                inputmode="numeric"
                aria-describedby="coverage"
                autocomplete="off"
                onfocus={() => onfocus("resignation")}
                onblur={() => onblur("resignation")}
                oninput={(e) => oninput("resignation", e)}
                onkeydown={(e) => onkeydown("resignation", e)}
              /><button type="button" aria-label="打开日历 (F4)" aria-haspopup="dialog" onclick={openCalendar}>日历</button>
            </span>
          </label>
          <label class="check"><input type="checkbox" checked={s.showResignation} onchange={() => call("toggle", { flag: "showResignation" }, focused)} />显示离职倒计时</label>
          <small id="coverage" class="wide muted">按周一至周五计，不含法定节假日及调休</small>
          {#if inlineCalendar}
            <div class="wide">
              <Calendar date={s.resignationDate} today={localToday()}
                onpick={(date) => inlineFinish({ action: "pick", date })}
                onclear={() => inlineFinish({ action: "clear" })}
                oncancel={() => inlineFinish({ action: "cancel" })} />
            </div>
          {/if}
        </fieldset>
        <fieldset>
          <legend>窗口</legend>
          <label>
            <span>缩放 {s.scalePercent}%</span>
            <input type="range" min="75" max="150" step="5" value={s.scalePercent}
              oninput={(e) => windowPrefs(+e.currentTarget.value, s.opacityPercent, false)}
              onchange={(e) => windowPrefs(+e.currentTarget.value, s.opacityPercent, true)} />
          </label>
          <label>
            <span>不透明度 {s.opacityPercent}%</span>
            <input type="range" min="25" max="100" step="5" value={s.opacityPercent}
              oninput={(e) => windowPrefs(s.scalePercent, +e.currentTarget.value, false)}
              onchange={(e) => windowPrefs(s.scalePercent, +e.currentTarget.value, true)} />
          </label>
          <label class="check wide">
            <input type="checkbox" checked={view.autostart.enabled} onchange={(e) => call("set_autostart", { enabled: e.currentTarget.checked }, focused)} />开机自启
            {#if view.autostart.otherTarget}<small class="muted">（当前指向 {view.autostart.otherTarget}，开启即改为本程序）</small>{/if}
          </label>
        </fieldset>
        <p class="status" class:error={!!error} role={error ? "alert" : "status"}>
          {error || view.notice || `v${view.version} · 设置保存在本机`}
        </p>
      </form>
    {/if}
    {#if !expanded && error}<p class="toast" role="alert">{error}</p>{/if}
    <div class="grip" aria-hidden="true" onpointerdown={grip} onpointermove={grip} onpointerup={grip} onpointercancel={grip}></div>
  </main>
</div>

<style>
  :global(*) {
    box-sizing: border-box;
  }
  :global(html),
  :global(body) {
    margin: 0;
    background: transparent;
    overflow: hidden;
    color: #293e33;
    font: 14px "PingFang SC", "Microsoft YaHei", "Noto Sans CJK SC", sans-serif;
  }
  .frame {
    width: 100%;
  }
  /* The window, not a fixed number, decides the height: Rust zooms the web
     view to the scale and never makes the window taller than the monitor
     work area, so the settings panel scrolls when a large scale does not fit
     (e.g. 150 % on 1920x1080). */
  main {
    position: relative;
    height: 100vh;
    display: flex;
    flex-direction: column;
    padding: 12px 20px 4px;
    border: 1px solid #aba992;
    border-radius: 10px;
    background: #fffbee;
    overflow: hidden;
  }
  main > :global(*) {
    flex-shrink: 0;
  }
  main.expanded {
    overflow-y: auto;
  }
  main.drag {
    outline: 4px dashed #48705a;
    outline-offset: -6px;
    background: #eef3e6;
  }
  main.drag > * {
    visibility: hidden;
  }
  header,
  footer,
  .row {
    display: flex;
    align-items: center;
    justify-content: space-between;
    gap: 8px;
  }
  header {
    padding-bottom: 8px;
    border-bottom: 2px solid #304e40;
    user-select: none;
  }
  header img {
    width: 30px;
    pointer-events: none;
  }
  .title {
    margin-right: auto;
    display: grid;
    line-height: 1.15;
  }
  .title strong {
    font: bold 20px Georgia, serif;
  }
  .title small {
    font-size: 11px;
    color: #566255;
    font-variant-numeric: tabular-nums;
  }
  button {
    font: inherit;
    color: inherit;
    border: 0;
    background: none;
    cursor: pointer;
    padding: 4px 6px;
    border-radius: 3px;
  }
  button:hover:not(:disabled) {
    background: #eae7d7;
  }
  button:disabled {
    opacity: 0.55;
    cursor: default;
  }
  :focus-visible {
    outline: 2px solid #48705a;
    outline-offset: 1px;
  }
  header button,
  small,
  footer {
    font-size: 12px;
  }
  .update {
    color: #823b27;
    border: 1px solid #ad6653;
  }
  .summary {
    padding-top: 12px;
  }
  h1 {
    font-size: 14px;
    margin: 0;
    font-weight: 500;
  }
  .stamp {
    font-size: 12px;
    color: #823b27;
    border: 1px solid #ad6653;
    padding: 2px 6px;
    transform: rotate(-4deg);
    border-radius: 3px;
  }
  time {
    display: block;
    font: 54px/1.12 Georgia, serif;
    letter-spacing: -2px;
    font-variant-numeric: tabular-nums;
    margin: 2px 0 6px;
  }
  .words {
    font-size: 40px;
    line-height: 60px;
    letter-spacing: 0;
  }
  .track {
    height: 7px;
    margin: 6px 0;
    background: repeating-linear-gradient(90deg, #deded1 0 5px, transparent 5px 7px);
  }
  .fill {
    height: 100%;
    background: repeating-linear-gradient(90deg, #48705a 0 5px, transparent 5px 7px);
  }
  .muted,
  footer {
    color: #566255;
  }
  .money {
    margin: 10px 0 4px;
    padding: 6px 0;
    border-block: 1px dashed #a9aa98;
  }
  .money b {
    font: 26px Georgia, serif;
    font-variant-numeric: tabular-nums;
  }
  .eye {
    font-size: 12px;
  }
  footer {
    margin-top: 4px;
  }
  .settings {
    font-weight: bold;
    font-size: 14px;
  }
  .version {
    color: #6f7a6c;
  }
  main > .panel {
    margin-top: 0;
    flex: 1 1 auto;
    min-height: 160px;
    overflow-y: auto;
    overscroll-behavior: contain;
  }
  @media (prefers-reduced-motion: no-preference) {
    .panel {
      animation: fade 160ms ease-out;
    }
  }
  @keyframes fade {
    from {
      opacity: 0;
    }
  }
  fieldset {
    border: 0;
    border-top: 1px dashed #a9aa98;
    padding: 6px 0;
    display: grid;
    grid-template-columns: 1fr 1fr;
    gap: 5px 12px;
    margin: 2px 0 0;
  }
  legend {
    font-weight: 600;
    padding-right: 8px;
    font-size: 13px;
  }
  label,
  .mode {
    display: grid;
    gap: 2px;
    font-size: 12px;
  }
  .wide,
  .date {
    grid-column: 1 / -1;
  }
  input:not([type]),
  input[type="text"],
  input[type="password"],
  .pair input {
    width: 100%;
    min-width: 0;
    padding: 4px 6px;
    border: 1px solid #aaa994;
    background: #fffdf5;
    border-radius: 3px;
    color: inherit;
    font: 14px/1.3 inherit;
    font-variant-numeric: tabular-nums;
  }
  input::placeholder {
    color: #9a9c8c;
  }
  .pair {
    display: flex;
    gap: 6px;
  }
  .pair button,
  .mode button {
    border: 1px solid #aaa994;
    white-space: nowrap;
  }
  .mode div {
    display: flex;
    gap: 4px;
  }
  .mode button {
    flex: 1;
    padding: 4px 2px;
  }
  .mode [aria-checked="true"] {
    background: #385542;
    color: #fff;
  }
  .check {
    display: flex;
    align-items: center;
    gap: 6px;
  }
  input[type="range"] {
    width: 100%;
    accent-color: #48705a;
  }
  .status {
    margin: 4px 0 0;
    font-size: 12px;
    color: #566255;
    min-height: 1.4em;
  }
  .error,
  .toast {
    color: #823b27;
  }
  .toast {
    position: absolute;
    left: 20px;
    right: 20px;
    bottom: 4px;
    margin: 0;
    font-size: 12px;
    background: #fffbee;
  }
  .grip {
    position: absolute;
    right: 0;
    bottom: 0;
    width: 16px;
    height: 16px;
    cursor: nwse-resize;
    background: linear-gradient(135deg, transparent 55%, #aba992 55% 62%, transparent 62% 75%, #aba992 75% 82%, transparent 82%);
  }
</style>

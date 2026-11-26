<script lang="ts">
  import { onMount, tick as render } from "svelte";
  import { invoke } from "@tauri-apps/api/core";
  import { listen } from "@tauri-apps/api/event";
  import { getCurrentWindow, LogicalSize } from "@tauri-apps/api/window";
  interface Settings {
    start: number;
    end: number;
    net: number;
    duration_mode: boolean;
    lunch_start: number;
    lunch_end: number;
    salary: number;
    deduct: boolean;
    stamp: boolean;
    resignation: string;
    hide_money: boolean;
  }
  interface Snapshot {
    phase: "Before" | "Working" | "After";
    remaining: number;
    countdown: number;
    on_lunch: boolean;
    progress: number;
    earned: number;
    end: number;
    days: number | null;
  }
  const win = getCurrentWindow(),
    pad = (n: number) => String(n).padStart(2, "0");
  const hhmm = (n: number) =>
    n < 0 ? "" : pad(Math.floor(n / 60)) + ":" + pad(n % 60);
  function minutes(value: string) {
    if (!value) return -1;
    const [h, m] = value.split(":").map(Number);
    return h * 60 + m;
  }
  let s: Settings,
    draft: Settings,
    v: Snapshot,
    now = new Date(),
    expanded = false,
    pinned = true,
    canPin = true,
    drag = false,
    error = "",
    hours = 7.5;
  function reset() {
    draft = { ...s };
    hours = s.net / 60;
  }
  async function refresh() {
    v = await invoke("tick");
    now = new Date();
  }
  async function panel(open: boolean) {
    expanded = open;
    if (open) reset();
    await win.setSize(new LogicalSize(360, open ? 776 : 384));
  }
  async function persist(value: Settings) {
    await invoke("save", { settings: value });
    s = value;
    await refresh();
    error = "";
  }
  async function saveDraft() {
    try {
      await persist({ ...draft, net: Math.round(hours * 60) });
      await panel(false);
    } catch (e) {
      error = String(e);
    }
  }
  async function hideMoney() {
    try {
      await persist({ ...s, hide_money: !s.hide_money });
      if (draft) draft.hide_money = s.hide_money;
    } catch (e) {
      error = String(e);
    }
  }
  async function pin() {
    try {
      await win.setAlwaysOnTop(!pinned);
      pinned = !pinned;
    } catch {
      error = "当前桌面使用普通窗口";
      pinned = false;
    }
  }
  onMount(() => {
    let alive = true,
      timer: ReturnType<typeof setInterval>;
    const clean: (() => void)[] = [];
    async function init() {
      s = await invoke("initial");
      canPin=await invoke("pin_supported");
      pinned=canPin;
      reset();
      await refresh();
      clean.push(
        await listen<Settings>("ocr-applied", async (e) => {
          s = e.payload;
          reset();
          error = "";
          await refresh();
        }),
      );
      clean.push(
        await listen<string>("ocr-error", async (e) => {
          await panel(true);
          error = e.payload;
        }),
      );
      clean.push(
        await win.onDragDropEvent((e) => {
          drag = e.payload.type === "over" || e.payload.type === "enter";
        }),
      );
      await render();
      await win.show();
      requestAnimationFrame(() =>
        requestAnimationFrame(() => void invoke("first_frame")),
      );
      timer = setInterval(() => {
        if (alive && !document.hidden) void refresh();
      }, 1000);
    }
    void init().catch(async (e) => {
      error = String(e);
      await win.show();
    });
    return () => {
      alive = false;
      clearInterval(timer);
      clean.forEach((fn) => fn());
    };
  });
  $: count = v
    ? hhmm(Math.floor(v.countdown / 60)) + ":" + pad(v.countdown % 60)
    : "";
</script>

<main class:drag>
  <header data-tauri-drag-region>
    <img src="/cat.svg" alt="" /><strong data-tauri-drag-region>
      OffWork
    </strong><button disabled={!canPin} on:click={pin}>
      {pinned ? "已置顶" : "普通窗口"}
    </button><button aria-label="关闭窗口" on:click={() => win.close()}>
      ×
    </button>
  </header>
  {#if v && s}
    <section>
      <div class="row">
        <span class="date">
          {pad(now.getMonth() + 1)}.{pad(now.getDate())} · 周{"日一二三四五六"[
            now.getDay()
          ]}
        </span>{#if v.days !== null}<span class="stamp">
            剩余 {v.days} 天离职
          </span>{/if}
      </div>
      <h1>
        {v.phase === "Before"
          ? "距离上班"
          : v.phase === "After"
            ? "今日班次结束"
            : "距离下班"}
      </h1>
      <time class:words={v.on_lunch || v.phase === "After"}>
        {v.on_lunch ? "午休中" : v.phase === "After" ? "已下班" : count}
      </time>
      <div class="row">
        <small>今日进度</small><small>{Math.round(v.progress * 100)}%</small>
      </div>
      <div
        class="track"
        role="progressbar"
        aria-label="今日进度"
        aria-valuenow={Math.round(v.progress * 100)}
      >
        <div class="fill" style:width={v.progress * 100 + "%"}></div>
      </div>
      <div class="row muted">
        <small>{hhmm(s.start)} 上班</small><small>{hhmm(v.end)} 下班</small>
      </div>
      <div class="money">
        <div class="row">
          <small>今天已经赚了</small><button on:click={hideMoney}>
            {s.hide_money ? "显示金额" : "隐藏金额"}
          </button>
        </div>
        <div class="row">
          <b>{s.hide_money ? "****" : "¥" + v.earned.toFixed(2)}</b><small>
            日薪 {s.hide_money ? "****" : "¥" + s.salary.toFixed(2)}
          </small>
        </div>
      </div>
    </section>
    <footer>
      <button
        class="settings"
        aria-expanded={expanded}
        on:click={() => panel(!expanded)}
      >
        设置 {expanded ? "⌃" : "﹀"}
      </button><span>
        {now.toLocaleTimeString("zh-CN", { hour12: false })} · v0.1.0
      </span>
    </footer>
    {#if expanded}
      <form on:submit|preventDefault={saveDraft}>
        <fieldset>
          <legend>我的班次</legend>
          <label>
            上班时间<input
              type="time"
              required
              value={hhmm(draft.start)}
              on:change={(e) => (draft.start = minutes(e.currentTarget.value))}
            />
          </label>
          <label>
            下班方式<select bind:value={draft.duration_mode}>
              <option value={true}>按净工时</option><option value={false}>
                固定时间
              </option>
            </select>
          </label>
          {#if draft.duration_mode}<label>
              净工时（小时）<input
                type="number"
                min="0.01"
                max="23.99"
                step="any"
                required
                bind:value={hours}
              />
            </label>{:else}<label>
              下班时间<input
                type="time"
                required
                value={hhmm(draft.end)}
                on:change={(e) => (draft.end = minutes(e.currentTarget.value))}
              />
            </label>{/if}
          <label>
            日薪（元）<input
              type={s.hide_money ? "password" : "text"}
              inputmode="decimal"
              required
              value={draft.salary}
              on:input={(e) => (draft.salary = Number(e.currentTarget.value))}
            />
          </label>
          <label>
            午休开始<input
              type="time"
              value={hhmm(draft.lunch_start)}
              on:change={(e) =>
                (draft.lunch_start = minutes(e.currentTarget.value))}
            />
          </label>
          <label>
            午休结束<input
              type="time"
              value={hhmm(draft.lunch_end)}
              on:change={(e) =>
                (draft.lunch_end = minutes(e.currentTarget.value))}
            />
          </label>
        </fieldset>
        <label class="check">
          <input type="checkbox" bind:checked={draft.deduct} />倒计时扣除午休
        </label>
        <fieldset>
          <legend>离职日期</legend><label>
            最后一天<input
              type="date"
              max="9999-12-31"
              min="0001-01-01"
              bind:value={draft.resignation}
            />
          </label><label class="check">
            <input type="checkbox" bind:checked={draft.stamp} />显示离职章
          </label>
        </fieldset>
        <button class="save">保存设置</button>
      </form>{/if}{/if}
  {#if error}<p role="alert">{error}</p>{/if}
</main>

<style>
  :global(*) {
    box-sizing: border-box;
  }
  :global(body) {
    background: #fffbee;
    color: #293e33;
    font:
      14px "PingFang SC",
      "Microsoft YaHei",
      sans-serif;
  }
  main {
    min-height: 100vh;
    padding: 14px 21px 10px;
    border: 1px solid #aba992;
    background: #fffbee;
  }
  main.drag {
    outline: 4px dashed #48705a;
    outline-offset: -5px;
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
    padding-bottom: 10px;
    border-bottom: 2px solid #304e40;
    user-select: none;
  }
  header img {
    width: 29px;
    pointer-events: none;
  }
  strong {
    font:
      bold 21px Georgia,
      serif;
    margin-right: auto;
  }
  button {
    font: inherit;
    color: inherit;
    border: 0;
    background: none;
    cursor: pointer;
    padding: 4px;
  }
  button:hover {
    background: #eae7d7;
  }
  button:focus-visible,
  input:focus-visible,
  select:focus-visible {
    outline: 2px solid #48705a;
  }
  header button,
  .money button,
  small,
  footer {
    font-size: 12px;
  }
  section {
    padding-top: 16px;
  }
  .date {
    font:
      14px Georgia,
      serif;
    letter-spacing: 1px;
  }
  .stamp {
    font-size: 12px;
    color: #823b27;
    border: 1px solid #ad6653;
    padding: 3px 6px;
    transform: rotate(-4deg);
    border-radius: 3px;
  }
  h1 {
    font-size: 14px;
    margin: 14px 0 0;
    font-weight: 500;
  }
  time {
    display: block;
    font:
      54px/1.12 Georgia,
      serif;
    letter-spacing: -2px;
    font-variant-numeric: tabular-nums;
    margin-bottom: 6px;
  }
  .words {
    font-size: 40px;
    line-height: 60px;
  }
  .track {
    height: 7px;
    margin: 6px 0;
    background: repeating-linear-gradient(
      90deg,
      #deded1 0 5px,
      transparent 5px 7px
    );
  }
  .fill {
    height: 100%;
    background: repeating-linear-gradient(
      90deg,
      #48705a 0 5px,
      transparent 5px 7px
    );
  }
  .money {
    margin: 12px 0 6px;
    padding: 7px 0;
    border-block: 1px dashed #a9aa98;
  }
  .money b {
    font:
      26px Georgia,
      serif;
  }
  .muted,
  footer {
    color: #566255;
  }
  .settings {
    font-weight: bold;
    font-size: 14px;
  }
  fieldset {
    border: 0;
    border-top: 1px dashed #a9aa98;
    padding: 8px 0;
    display: grid;
    grid-template-columns: 1fr 1fr;
    gap: 7px 12px;
    margin: 10px 0 0;
  }
  legend {
    font-weight: 600;
    padding-right: 8px;
  }
  label {
    display: grid;
    gap: 3px;
    font-size: 12px;
  }
  input,
  select {
    width: 100%;
    min-width: 0;
    padding: 4px;
    border: 1px solid #aaa994;
    background: #fffdf5;
    border-radius: 3px;
    color: inherit;
    font: 14px inherit;
  }
  .check {
    display: flex;
    align-items: center;
  }
  .check input {
    width: auto;
  }
  .save {
    background: #385542;
    color: white;
    margin-top: 10px;
    border-radius: 3px;
    padding: 6px 12px;
  }
  .save:hover {
    background: #293e33;
  }
  p {
    color: #823b27;
  }
</style>

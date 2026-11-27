<script lang="ts">
  import { onMount, tick, untrack } from "svelte";
  import { format, grid, key, parse, same, addMonths, type Ymd } from "./calendar";

  interface Props {
    date: string;
    today: string;
    onpick: (date: string) => void;
    onclear: () => void;
    oncancel: () => void;
  }
  let { date, today, onpick, onclear, oncancel }: Props = $props();
  // The popup opens once per choice: its inputs are read once.
  const selected = untrack(() => parse(date));
  const now = untrack(() => parse(today)) ?? { y: 2026, m: 1, d: 1 };
  let focus = $state<Ymd>(selected ?? now);
  let gridEl: HTMLElement;
  const cells = $derived(grid(focus));

  async function move(next: Ymd) {
    focus = next;
    await tick();
    gridEl?.querySelector<HTMLButtonElement>("[tabindex='0']")?.focus();
  }
  function onkeydown(e: KeyboardEvent) {
    const result = key(focus, e.key, e.shiftKey);
    if (!result) return;
    e.preventDefault();
    if ("focus" in result) void move(result.focus);
    else if ("pick" in result) onpick(format(result.pick));
    else oncancel();
  }
  function onescape(e: KeyboardEvent) {
    if (e.key === "Escape") {
      e.preventDefault();
      oncancel();
    }
  }
  onMount(() => void move(focus));
</script>

<!-- svelte-ignore a11y_no_noninteractive_element_interactions -->
<div class="calendar" role="dialog" aria-label="选择离职日期" tabindex="-1" onkeydown={onescape}>
  <div class="head">
    <button type="button" tabindex="-1" aria-label="上个月" onclick={() => move(addMonths(focus, -1))}>‹</button>
    <strong aria-live="polite">{focus.y}年{focus.m}月</strong>
    <button type="button" tabindex="-1" aria-label="下个月" onclick={() => move(addMonths(focus, 1))}>›</button>
  </div>
  <div class="grid" role="grid" aria-label="{focus.y}年{focus.m}月" bind:this={gridEl} {onkeydown} tabindex="-1">
    <div class="row" role="row">
      {#each "一二三四五六日" as w}<span role="columnheader" class="w">{w}</span>{/each}
    </div>
    {#each [0, 1, 2, 3, 4, 5] as week}
      <div class="row" role="row">
        {#each cells.slice(week * 7, week * 7 + 7) as cell}
          {#if cell}
            <button
              type="button"
              role="gridcell"
              class:other={cell.m !== focus.m}
              class:today={same(cell, now)}
              aria-selected={same(cell, selected)}
              aria-label="{cell.y}年{cell.m}月{cell.d}日"
              tabindex={same(cell, focus) ? 0 : -1}
              onclick={() => onpick(format(cell))}>{cell.d}</button
            >
          {:else}<span role="gridcell"></span>{/if}
        {/each}
      </div>
    {/each}
  </div>
  <div class="foot">
    <button type="button" onclick={onclear}>清空日期</button>
    <button type="button" onclick={oncancel}>取消 (Esc)</button>
  </div>
</div>

<style>
  .calendar {
    width: 288px;
    padding: 10px 12px;
    background: var(--paper, #fffbee);
    color: var(--ink, #293e33);
    font: 13px "PingFang SC", "Microsoft YaHei", "Noto Sans CJK SC", sans-serif;
  }
  .head,
  .foot,
  .row {
    display: flex;
    align-items: center;
    justify-content: space-between;
  }
  .head {
    margin-bottom: 6px;
    font-family: Georgia, "Songti SC", serif;
    font-size: 15px;
  }
  .row {
    gap: 2px;
  }
  .w,
  .row > [role="gridcell"] {
    width: 36px;
    height: 32px;
    display: grid;
    place-items: center;
  }
  .w {
    height: 22px;
    color: var(--muted, #566255);
    font-size: 12px;
  }
  button {
    font: inherit;
    color: inherit;
    border: 0;
    background: none;
    border-radius: 4px;
    cursor: pointer;
    padding: 4px 8px;
  }
  .row button {
    padding: 0;
    font-variant-numeric: tabular-nums;
  }
  button:hover {
    background: #eae7d7;
  }
  button:focus-visible {
    outline: 2px solid #48705a;
    outline-offset: -2px;
  }
  .other {
    color: #9a9c8c;
  }
  .today {
    text-decoration: underline;
  }
  [aria-selected="true"] {
    background: #385542;
    color: #fff;
  }
  .foot {
    margin-top: 8px;
    border-top: 1px dashed #a9aa98;
    padding-top: 8px;
  }
  .foot button {
    border: 1px solid #aaa994;
  }
</style>

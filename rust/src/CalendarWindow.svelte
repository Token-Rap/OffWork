<script lang="ts">
  import { onMount } from "svelte";
  import { invoke } from "@tauri-apps/api/core";
  import { getCurrentWindow } from "@tauri-apps/api/window";
  import { listen } from "@tauri-apps/api/event";
  import Calendar from "./Calendar.svelte";

  let init = $state<{ date: string; today: string; scale: number } | null>(null);
  let done = false;
  const win = getCurrentWindow();
  function finish(action: object) {
    if (done) return;
    done = true;
    void invoke("calendar_finish", { finish: action });
  }
  onMount(() => {
    // Clicking outside the popup cancels, like the native calendar.
    const blur = () => finish({ action: "cancel" });
    window.addEventListener("blur", blur);
    // Smoke-test hook (OFFWORK_MEASURE): cancel from inside the popup.
    const unlisten = listen("measure-cancel-calendar", () => finish({ action: "cancel" }));
    void invoke<typeof init>("calendar_init").then(async (value) => {
      init = value;
      await win.show();
      await win.setFocus();
    });
    return () => {
      window.removeEventListener("blur", blur);
      void unlisten.then((fn) => fn());
    };
  });
</script>

{#if init}
  <div class="zoom" style:zoom={init.scale / 100}>
    <Calendar
      date={init.date}
      today={init.today}
      onpick={(date) => finish({ action: "pick", date })}
      onclear={() => finish({ action: "clear" })}
      oncancel={() => finish({ action: "cancel" })}
    />
  </div>
{/if}

<style>
  :global(html),
  :global(body) {
    margin: 0;
    background: #fffbee;
    /* The window is smaller than the calendar only when the work area is. */
    overflow: auto;
  }
  .zoom {
    transform-origin: 0 0;
    width: 288px;
  }
</style>

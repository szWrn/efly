import { ref, watch } from "vue";
import { invoke } from "@tauri-apps/api/core";

// ============================================================
// shared settings state
// ============================================================

export const downloadDir     = ref("");
export const defaultPort     = ref(8080);
export const theme           = ref<"system" | "light" | "dark">("system");
export const autoDiscover    = ref(false);
export const overwritePolicy = ref<"overwrite" | "rename" | "skip">("rename");

// ============================================================
// persistence
// ============================================================

interface ConfigPayload {
  download_dir: string;
  default_port: number;
  theme: string;
  auto_discover: boolean;
  overwrite_policy: string;
}

let _loaded = false;

export async function loadConfigFromDisk() {
  if (_loaded) return;
  try {
    const cfg = await invoke<ConfigPayload>("load_config");
    downloadDir.value     = cfg.download_dir;
    defaultPort.value     = cfg.default_port;
    theme.value           = cfg.theme as typeof theme.value;
    autoDiscover.value    = cfg.auto_discover;
    overwritePolicy.value = cfg.overwrite_policy as typeof overwritePolicy.value;
    _loaded = true;
  } catch (e) {
    console.error("[settings] load failed:", e);
  }
}

async function saveConfigToDisk() {
  try {
    await invoke("save_config", { cfg: {
      download_dir:    downloadDir.value,
      default_port:    defaultPort.value,
      theme:           theme.value,
      auto_discover:   autoDiscover.value,
      overwrite_policy: overwritePolicy.value,
    }});
  } catch (e) {
    console.error("[settings] save failed:", e);
  }
}

// auto-save on any change
let _saving = false;
watch(
  [downloadDir, defaultPort, theme, autoDiscover, overwritePolicy],
  () => {
    if (!_loaded) return;
    if (_saving) return;
    _saving = true;
    setTimeout(() => { _saving = false; saveConfigToDisk(); }, 300); // debounce
  },
  { deep: false }
);

// ============================================================
// theme application
// ============================================================

const sysMq = window.matchMedia("(prefers-color-scheme: dark)");

function applyTheme(mode: string) {
  const html = document.documentElement;
  switch (mode) {
    case "dark":
      html.classList.add("dark");
      html.classList.remove("light");
      break;
    case "light":
      html.classList.remove("dark");
      html.classList.add("light");
      break;
    default: // "system"
      html.classList.remove("light");
      html.classList.toggle("dark", sysMq.matches);
  }
}

// initial apply (before config loaded)
applyTheme("system");

// watch setting
watch(theme, applyTheme);

// follow OS changes when in "system" mode
sysMq.addEventListener("change", () => {
  if (theme.value === "system") applyTheme("system");
});

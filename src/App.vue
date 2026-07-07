<script setup lang="ts">
import { ref, onMounted } from "vue";
import { getCurrentWindow } from "@tauri-apps/api/window";
import { invoke } from "@tauri-apps/api/core";
import { open } from "@tauri-apps/plugin-dialog";
import { Minus, FullScreen, Close, Setting } from "@element-plus/icons-vue";
import { loadConfigFromDisk } from "./settings";
import PeerPanel from "./components/PeerPanel.vue";
import TransferPanel from "./components/TransferPanel.vue";
import SettingsDialog from "./components/SettingsDialog.vue";

// ============================================================
// window controls
// ============================================================

const appWindow = getCurrentWindow();
const isMaximized = ref(false);

async function refreshMaximized() { isMaximized.value = await appWindow.isMaximized(); }
function onMinimize() { appWindow.minimize(); }
function onToggleMaximize() { appWindow.toggleMaximize(); }
function onClose() { appWindow.close(); }

// ============================================================
// auto-update check
// ============================================================

interface UpdateResult {
  hasUpdate: boolean;
  version?: string;
  priority?: string;   // "silent" | "warn" | "force"
  changelog?: string;
  bestUrl?: string;
}

const showUpdateDialog = ref(false);
const updateInfo = ref<UpdateResult>({ hasUpdate: false });
const updating = ref(false);
const updateErr = ref("");

async function checkForUpdate() {
  try {
    const info = await invoke<UpdateResult>("check_update");
    updateInfo.value = info;
    if (info.hasUpdate) {
      if (info.priority === "silent") {
        showUpdateDialog.value = false;
      } else {
        // warn / force: show dialog
        showUpdateDialog.value = true;
      }
    }
  } catch (e) {
    console.error("[efly] check_update failed:", e);
  }
}

async function doUpdate(url: string) {
  updating.value = true;
  updateErr.value = "";
  try {
    await invoke("do_update", { url });
    // updater launched — app will exit when it runs
  } catch (e) {
    updateErr.value = String(e);
  } finally {
    updating.value = false;
  }
}

function onUpdateDismiss() {
  if (updateInfo.value.priority === "force") {
    // force update: user must accept or quit
    onClose();
  }
  showUpdateDialog.value = false;
}

// ============================================================
// send-file dialog
// ============================================================

const showSendDialog = ref(false);
const sendFilePath = ref("");
const sendToIp = ref("");
const sendToPort = ref(8080);
const sending = ref(false);
const sendError = ref("");
const showSettings = ref(false);

const peerPanelRef = ref<InstanceType<typeof PeerPanel> | null>(null);
const transferPanelRef = ref<InstanceType<typeof TransferPanel> | null>(null);

function openSendDialog() {
  sendError.value = "";
  sendFilePath.value = "";
  sendToIp.value = "";
  sendToPort.value = 8080;
  const p = peerPanelRef.value?.selectedPeer;
  if (p) {
    sendToIp.value = p.ip;
    sendToPort.value = p.port;
  }
  showSendDialog.value = true;
}

async function pickFile() {
  const selected = await open({
    multiple: false,
    title: "Select file to send",
  });
  if (selected && typeof selected === "string") {
    sendFilePath.value = selected;
  }
}

async function doSendFile() {
  sendError.value = "";
  if (!sendFilePath.value) { sendError.value = "Please enter a file path"; return; }
  if (!sendToIp.value)    { sendError.value = "Please enter target IP"; return; }

  sending.value = true;
  try {
    const fileId = await invoke<number>("send_file", {
      path: sendFilePath.value,
      ip: sendToIp.value,
      port: sendToPort.value,
    });
    // register sender-side transfer
    const name = sendFilePath.value.replace(/^.*[\\/]/, "");
    const size = 0; // will be updated by progress events
    transferPanelRef.value?.sendDone(fileId, sendToIp.value, name, size);
    showSendDialog.value = false;
    sendFilePath.value = "";
  } catch (e) {
    sendError.value = String(e);
  } finally {
    sending.value = false;
  }
}

// ============================================================
// lifecycle
// ============================================================

onMounted(async () => {
  await refreshMaximized();
  appWindow.onResized(() => refreshMaximized());
  // load saved settings
  await loadConfigFromDisk();
  // check for update on startup
  checkForUpdate();
});
</script>

<template>
  <main class="app-shell">
    <!-- 标题栏 -->
    <header class="titlebar" data-tauri-drag-region>
      <div class="titlebar-left" data-tauri-drag-region>
        <span class="app-logo">efly</span>
      </div>
      <div class="titlebar-right">
        <div class="win-btn" title="Settings" @click="showSettings = true">
          <el-icon :size="16"><Setting /></el-icon>
        </div>
        <div class="win-btn" title="Minimize" @click="onMinimize">
          <el-icon :size="16"><Minus /></el-icon>
        </div>
        <div class="win-btn" :title="isMaximized ? 'Restore' : 'Maximize'" @click="onToggleMaximize">
          <el-icon :size="14" class="max-icon" :class="{ restore: isMaximized }"><FullScreen /></el-icon>
        </div>
        <div class="win-btn win-btn-close" title="Close" @click="onClose">
          <el-icon :size="16"><Close /></el-icon>
        </div>
      </div>
    </header>

    <!-- 双栏主体 -->
    <div class="main-content">
      <aside class="left-panel">
        <PeerPanel ref="peerPanelRef" />
      </aside>
      <section class="right-panel">
        <TransferPanel ref="transferPanelRef" @sendFile="openSendDialog" />
      </section>
    </div>

    <!-- 状态栏 -->
    <footer class="statusbar">
      <span class="status-left"><span class="status-dot" /> Ready</span>
      <span class="status-right">efly v0.1.0</span>
    </footer>

    <!-- 发送文件对话框 -->
    <el-dialog v-model="showSendDialog" title="Send File" width="460px" :close-on-click-modal="false">
      <el-form label-position="top" @submit.prevent="doSendFile">
        <el-form-item label="File">
          <div style="display:flex;gap:8px;width:100%">
            <el-input v-model="sendFilePath" placeholder="Select a file..." readonly style="flex:1" />
            <el-button @click="pickFile">Browse...</el-button>
          </div>
        </el-form-item>
        <el-form-item label="Target IP">
          <el-input v-model="sendToIp" placeholder="e.g. 192.168.1.100" />
        </el-form-item>
        <el-form-item label="Port">
          <el-input-number v-model="sendToPort" :min="1" :max="65535" style="width:100%" />
        </el-form-item>
        <div v-if="sendError" style="color:var(--efly-danger);font-size:13px;margin-bottom:8px">{{ sendError }}</div>
      </el-form>
      <template #footer>
        <el-button @click="showSendDialog = false">Cancel</el-button>
        <el-button type="primary" :loading="sending" @click="doSendFile">Send</el-button>
      </template>
    </el-dialog>

    <!-- 设置对话框 -->
    <SettingsDialog v-model:visible="showSettings" />

    <!-- 更新提示对话框 -->
    <el-dialog
      v-model="showUpdateDialog"
      :title="updateInfo.priority === 'force' ? 'Update Required' : 'Update Available'"
      width="420px"
      :close-on-click-modal="false"
      :show-close="updateInfo.priority !== 'force'"
    >
      <div style="margin-bottom:12px">
        <p><strong>Version {{ updateInfo.version }}</strong> is available.</p>
        <p v-if="updateInfo.changelog" style="margin-top:8px;color:var(--efly-text-secondary);font-size:13px;white-space:pre-wrap">{{ updateInfo.changelog }}</p>
      </div>
      <div v-if="updateErr" style="color:var(--efly-danger);font-size:13px;margin-bottom:8px">{{ updateErr }}</div>
      <template #footer>
        <el-button
          v-if="updateInfo.priority !== 'force'"
          @click="onUpdateDismiss"
        >{{ updateInfo.priority === 'warn' ? 'Skip' : 'Cancel' }}</el-button>
        <el-button type="primary" :loading="updating" @click="doUpdate(updateInfo.bestUrl!)">
          Update Now
        </el-button>
      </template>
    </el-dialog>
  </main>
</template>

<style scoped>
.app-shell {
  display: flex; flex-direction: column;
  height: 100vh; width: 100vw; overflow: hidden;
  background: var(--efly-bg);
}

/* ---- titlebar ---- */
.titlebar {
  display: flex; align-items: center; justify-content: space-between;
  height: 32px; padding: 0 8px;
  background: var(--efly-chrome);
  user-select: none; flex-shrink: 0;
}
.titlebar-left { display: flex; align-items: center; gap: 8px; }
.app-logo { font-size: 13px; font-weight: 700; color: var(--efly-chrome-logo); letter-spacing: 1px; }
.titlebar-right { display: flex; align-items: center; gap: 2px; }
.win-btn {
  width: 28px; height: 28px;
  display: flex; align-items: center; justify-content: center;
  border-radius: 4px; cursor: pointer;
  color: var(--efly-chrome-text);
  transition: background .1s, color .1s;
}
.win-btn:hover { background: var(--efly-chrome-btn-hover); color: var(--efly-chrome-logo); }
.win-btn-close:hover { background: #e81123; color: #fff; }
.max-icon.restore { transform: rotate(180deg); }

/* ---- main ---- */
.main-content { display: flex; flex: 1; overflow: hidden; }
.left-panel  { width: 280px; flex-shrink: 0; overflow: hidden; }
.right-panel { flex: 1; overflow: hidden; }

/* ---- statusbar ---- */
.statusbar {
  display: flex; align-items: center; justify-content: space-between;
  height: 26px; padding: 0 12px;
  background: var(--efly-chrome); color: var(--efly-chrome-muted);
  font-size: 11px; flex-shrink: 0;
}
.status-left  { display: flex; align-items: center; gap: 6px; }
.status-dot   { width: 7px; height: 7px; border-radius: 50%; background: var(--efly-success); }
.status-right { color: var(--efly-chrome-muted); }
</style>

<style>
body, html {
  margin: 0; padding: 0;
  height: 100%; width: 100%;
  background: var(--efly-bg);
  color: var(--efly-text);
}
</style>

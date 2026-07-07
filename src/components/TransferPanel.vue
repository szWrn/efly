<script setup lang="ts">
import { ref, onMounted, onUnmounted } from "vue";
import { invoke } from "@tauri-apps/api/core";
import { listen, type UnlistenFn } from "@tauri-apps/api/event";
import { Upload, Delete, Close, Download, WarningFilled, FolderOpened, Folder } from "@element-plus/icons-vue";
import { open } from "@tauri-apps/plugin-dialog";
import { downloadDir } from "../settings";

// ============================================================
// types
// ============================================================

interface Transfer {
  id: number;
  filename: string;
  size: number;
  transferred: number;
  peer: string;
  direction: "send" | "recv";
  status: "announcing" | "transferring" | "done" | "error";
  errorMsg?: string;
}

interface AnnouncePayload {
  fileId: number;
  filename: string;
  fileSize: number;
  fromIp: string;
}

interface ProgressPayload {
  fileId: number;
  total: number;
  transferred: number;
}

interface CompletePayload {
  fileId: number;
  savedPath: string;
}

interface ErrorPayload {
  fileId: number;
  code: number;
  message: string;
}

// ============================================================
// state
// ============================================================

const transfers = ref<Transfer[]>([]);

let unlisteners: UnlistenFn[] = [];

// ---- find / upsert helper ----
function ensure(id: number, dir: "send" | "recv", peer: string, filename: string, size: number) {
  let t = transfers.value.find(x => x.id === id);
  if (!t) {
    t = { id, filename, size, transferred: 0, peer, direction: dir, status: "announcing" };
    transfers.value.push(t);
  }
  return t;
}

// ============================================================
// event handlers
// ============================================================

function onAnnounce(p: AnnouncePayload) {
  ensure(p.fileId, "recv", p.fromIp, p.filename, p.fileSize);
}

function onProgress(p: ProgressPayload) {
  const t = transfers.value.find(x => x.id === p.fileId);
  if (t) {
    t.size = p.total;
    t.transferred = p.transferred;
    if (t.status === "announcing") t.status = "transferring";
  }
}

function onComplete(p: CompletePayload) {
  const t = transfers.value.find(x => x.id === p.fileId);
  if (t) {
    t.status = "done";
    t.transferred = t.size;
  }
}

function onError(p: ErrorPayload) {
  const t = transfers.value.find(x => x.id === p.fileId);
  if (t) {
    t.status = "error";
    t.errorMsg = p.message;
  }
}

// ============================================================
// actions
// ============================================================

async function doAccept(id: number) {
  try {
    await invoke("accept_transfer", { fileId: id, saveDir: downloadDir.value });
  } catch (e) {
    console.error("accept failed:", e);
  }
}

async function doReject(id: number) {
  try {
    await invoke("reject_transfer", { fileId: id });
    transfers.value = transfers.value.filter(x => x.id !== id);
  } catch (e) {
    console.error("reject failed:", e);
  }
}

async function doCancel(id: number) {
  try {
    await invoke("cancel_transfer", { fileId: id });
  } catch (e) {
    console.error("cancel failed:", e);
  }
}

function doClearDone() {
  transfers.value = transfers.value.filter(x => x.status !== "done" && x.status !== "error");
}

async function pickFolder() {
  const selected = await open({ directory: true, title: "Select download folder" });
  if (selected && typeof selected === "string") {
    downloadDir.value = selected;
  }
}

// ============================================================
// emits
// ============================================================

const emit = defineEmits<{
  (e: "sendFile"): void;
}>();

// ============================================================
// lifecycle
// ============================================================

onMounted(async () => {
  unlisteners.push(await listen<AnnouncePayload>("efly:transfer-announce", e => onAnnounce(e.payload)));
  unlisteners.push(await listen<ProgressPayload>("efly:transfer-progress", e => onProgress(e.payload)));
  unlisteners.push(await listen<CompletePayload>("efly:transfer-complete", e => onComplete(e.payload)));
  unlisteners.push(await listen<ErrorPayload>("efly:error", e => onError(e.payload)));
});

onUnmounted(() => { unlisteners.forEach(fn => fn()); });

// ============================================================
// formatters
// ============================================================

function fmtSize(bytes: number): string {
  const u = ["B","KB","MB","GB"];
  let s = bytes, i = 0;
  while (s >= 1024 && i < 3) { s /= 1024; i++; }
  return s.toFixed(i > 0 ? 1 : 0) + " " + u[i];
}
function pct(t: Transfer) { return t.size ? Math.round(t.transferred / t.size * 100) : 0; }
function statusText(s: string) {
  return { announcing:"Waiting", transferring:"Transferring", done:"Done", error:"Failed" }[s] ?? s;
}
function statusTag(s: string) {
  return s === "done" ? "success" : s === "error" ? "danger" : s === "announcing" ? "warning" : "";
}

// expose for parent
defineExpose({ transfers, downloadDir, sendDone: (id: number, peer: string, name: string, size: number) => {
  ensure(id, "send", peer, name, size);
}});
</script>

<template>
  <div class="transfer-panel">
    <!-- 工具栏 -->
    <div class="panel-toolbar">
      <span class="panel-title">Transfers</span>
      <div class="toolbar-actions">
        <span class="input-label">Save to:</span>
        <el-input
          v-model="downloadDir" size="small" style="width:150px" placeholder="Downloads"
        />
        <el-button size="small" :icon="Folder" @click="pickFolder" />
        <el-button size="small" :icon="Upload" type="primary" @click="emit('sendFile')" />
        <el-button size="small" :icon="Delete" @click="doClearDone" :disabled="!transfers.some(t => t.status==='done' || t.status==='error')" />
      </div>
    </div>

    <!-- 传入通知 -->
    <div
      v-for="t in transfers.filter(x => x.direction==='recv' && x.status==='announcing')"
      :key="'announce-'+t.id"
      class="announce-banner"
    >
      <el-icon :size="18"><WarningFilled /></el-icon>
      <span class="announce-text">
        Incoming: <strong>{{ t.filename }}</strong> ({{ fmtSize(t.size) }}) from {{ t.peer }}
      </span>
      <div class="announce-actions">
        <el-button size="small" type="success" @click="doAccept(t.id)">Accept</el-button>
        <el-button size="small" type="danger" @click="doReject(t.id)">Reject</el-button>
      </div>
    </div>

    <!-- 传输列表 -->
    <div class="transfer-list">
      <el-scrollbar>
        <div v-if="transfers.length === 0" class="empty-hint">
          <el-icon :size="32"><FolderOpened /></el-icon>
          <p>No transfers yet</p>
        </div>
        <div v-for="t in transfers" :key="t.id" class="transfer-card">
          <div class="transfer-icon">
            <el-icon :size="20"><Upload v-if="t.direction==='send'" /><Download v-else /></el-icon>
          </div>
          <div class="transfer-info">
            <div class="transfer-top">
              <span class="transfer-name">{{ t.filename }}</span>
              <el-tag :type="statusTag(t.status)" size="small">{{ statusText(t.status) }}</el-tag>
            </div>
            <div class="transfer-meta">
              <span>{{ t.direction==='send' ? 'to' : 'from' }} {{ t.peer }}</span>
              <span>{{ fmtSize(t.transferred) }} / {{ fmtSize(t.size) }}</span>
            </div>
            <el-progress
              v-if="t.status==='transferring'" :percentage="pct(t)"
              :stroke-width="4" :show-text="false" class="transfer-progress"
            />
            <div v-if="t.status==='error'" class="error-msg">{{ t.errorMsg }}</div>
          </div>
          <div class="transfer-actions">
            <el-button
              v-if="t.status==='transferring' || t.status==='announcing'"
              size="small" type="danger" :icon="Close" circle @click="doCancel(t.id)"
            />
          </div>
        </div>
      </el-scrollbar>
    </div>
  </div>
</template>

<style scoped>
.transfer-panel {
  display: flex; flex-direction: column; height: 100%;
  background: var(--efly-surface);
}
.panel-toolbar {
  display: flex; align-items: center; justify-content: space-between;
  padding: 10px 16px; border-bottom: 1px solid var(--efly-border);
}
.panel-title { font-size: 14px; font-weight: 600; color: var(--efly-text); }
.toolbar-actions { display: flex; gap: 4px; align-items: center; }
.toolbar-actions :deep(.el-button) { margin: 0; }
.toolbar-actions :deep(.el-button--small) {
  width: 24px; height: 24px; padding: 0;
}
.input-label { font-size: 12px; color: var(--efly-text-secondary); white-space: nowrap; margin-right:2px; }

.announce-banner {
  display: flex; align-items: center; gap: 8px;
  padding: 10px 16px;
  background: var(--efly-warning-bg);
  border-bottom: 1px solid var(--efly-warning-border);
}
.announce-banner .el-icon { color: var(--efly-warning-icon); }
.announce-text { flex: 1; font-size: 13px; color: var(--efly-text); }
.announce-actions { display: flex; gap: 6px; }

.transfer-list { flex: 1; overflow: hidden; }

.transfer-card {
  display: flex; align-items: center; gap: 12px;
  padding: 12px 16px; border-bottom: 1px solid var(--efly-border-light);
}
.transfer-card:hover { background: var(--efly-hover); }

.transfer-icon {
  width: 36px; height: 36px; border-radius: 8px;
  background: var(--efly-primary-bg); color: var(--efly-primary);
  display: flex; align-items: center; justify-content: center;
}
.transfer-info { flex: 1; min-width: 0; }
.transfer-top { display: flex; align-items: center; gap: 8px; }
.transfer-name {
  font-size: 13px; font-weight: 500; color: var(--efly-text);
  overflow: hidden; text-overflow: ellipsis; white-space: nowrap;
}
.transfer-meta {
  font-size: 11px; color: var(--efly-text-secondary);
  margin-top: 2px; display: flex; gap: 12px;
}
.transfer-progress { margin-top: 4px; }
.error-msg { font-size: 11px; color: var(--efly-danger); margin-top: 2px; }
.transfer-actions { flex-shrink: 0; }
.empty-hint { text-align: center; padding: 40px 20px; color: var(--efly-text-secondary); }
.empty-hint p { margin: 6px 0 0; font-size: 13px; }
</style>

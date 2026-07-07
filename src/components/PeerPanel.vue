<script setup lang="ts">
import { ref, onMounted, onUnmounted } from "vue";
import { invoke } from "@tauri-apps/api/core";
import { listen, type UnlistenFn } from "@tauri-apps/api/event";
import { Monitor, CircleCheckFilled } from "@element-plus/icons-vue";
import { defaultPort } from "../settings";

// ============================================================
// types
// ============================================================

interface Peer {
  ip: string;
  port: number;
  seen: number;
}
interface PeerFoundPayload {
  ip: string;
  port: number;
}

// ============================================================
// state
// ============================================================

const peers = ref<Peer[]>([]);
const discovering = ref(false);
const selectedIdx = ref(-1);
const selectedPeer = ref<Peer | null>(null);

let unlistenPeer: UnlistenFn | null = null;

// ============================================================
// discovery
// ============================================================

async function toggleDiscovery() {
  if (discovering.value) await stopDiscovery();
  else await startDiscovery();
}

async function startDiscovery() {
  try {
    await invoke("start_discovery", { port: defaultPort.value });
    discovering.value = true;
  } catch (e) { console.error("start_discovery:", e); }
}

async function stopDiscovery() {
  try { await invoke("stop_discovery"); } catch (e) { console.error(e); }
  discovering.value = false;
  peers.value = [];
  selectedIdx.value = -1;
  selectedPeer.value = null;
}

function onPeerFound(p: PeerFoundPayload) {
  const key = `${p.ip}:${p.port}`;
  const exist = peers.value.find(x => `${x.ip}:${x.port}` === key);
  if (exist) { exist.seen++; }
  else { peers.value.push({ ip: p.ip, port: p.port, seen: 1 }); }
}

function selectPeer(idx: number) {
  if (selectedIdx.value === idx) {
    selectedIdx.value = -1;
    selectedPeer.value = null;
  } else {
    selectedIdx.value = idx;
    selectedPeer.value = peers.value[idx];
  }
}

// ============================================================
// lifecycle
// ============================================================

onMounted(async () => {
  unlistenPeer = await listen<PeerFoundPayload>("efly:peer-found", e => onPeerFound(e.payload));
});

onUnmounted(async () => {
  if (unlistenPeer) unlistenPeer();
  if (discovering.value) await stopDiscovery();
});

defineExpose({ selectedPeer });
</script>

<template>
  <div class="peer-panel">
    <div class="panel-toolbar">
      <span class="panel-title">Devices</span>
      <div class="discovery-controls">
        <el-input v-model="defaultPort" size="small" style="width:90px" placeholder="Port" :disabled="discovering" />
        <el-button size="small" :type="discovering ? 'danger' : 'primary'" @click="toggleDiscovery">
          {{ discovering ? "Stop" : "Discover" }}
        </el-button>
      </div>
    </div>

    <div class="peer-list">
      <el-scrollbar>
        <div v-if="peers.length === 0" class="empty-hint">
          <el-icon :size="32"><Monitor /></el-icon>
          <p>{{ discovering ? "Listening for peers..." : "No devices discovered" }}</p>
          <p class="sub">{{ discovering ? "Waiting for broadcasts..." : "Start discovery to find LAN peers" }}</p>
        </div>
        <div
          v-for="(p, i) in peers" :key="i"
          class="peer-card" :class="{ selected: selectedIdx === i }"
          @click="selectPeer(i)"
        >
          <div class="peer-icon"><el-icon :size="22"><Monitor /></el-icon></div>
          <div class="peer-info">
            <div class="peer-name">{{ p.ip }}</div>
            <div class="peer-addr">{{ p.ip }}:{{ p.port }}</div>
          </div>
          <div class="peer-meta">
            <el-tag size="small" type="info">seen {{ p.seen }}x</el-tag>
            <el-icon v-if="selectedIdx === i" class="check-icon" color="var(--efly-primary)" :size="18"><CircleCheckFilled /></el-icon>
          </div>
        </div>
      </el-scrollbar>
    </div>
  </div>
</template>

<style scoped>
.peer-panel {
  display: flex; flex-direction: column; height: 100%;
  border-right: 1px solid var(--efly-border);
  background: var(--efly-surface-alt);
}
.panel-toolbar {
  display: flex; align-items: center; justify-content: space-between;
  padding: 10px 12px; border-bottom: 1px solid var(--efly-border);
  background: var(--efly-surface);
}
.panel-title { font-size: 14px; font-weight: 600; color: var(--efly-text); }
.discovery-controls { display: flex; gap: 6px; align-items: center; }
.peer-list { flex: 1; overflow: hidden; }

.peer-card {
  display: flex; align-items: center; gap: 10px;
  padding: 10px 12px; cursor: pointer; transition: background .15s;
}
.peer-card:hover { background: var(--efly-hover); }
.peer-card.selected { background: var(--efly-primary-bg); }

.peer-icon {
  width: 36px; height: 36px; border-radius: 8px;
  background: var(--efly-primary-light); color: var(--efly-primary);
  display: flex; align-items: center; justify-content: center;
}
.peer-info { flex: 1; min-width: 0; }
.peer-name { font-size: 13px; font-weight: 500; color: var(--efly-text); }
.peer-addr { font-size: 11px; color: var(--efly-text-secondary); margin-top: 1px; }
.peer-meta { display: flex; align-items: center; gap: 6px; }
.check-icon { flex-shrink: 0; }
.empty-hint { text-align: center; padding: 40px 20px; color: var(--efly-text-secondary); }
.empty-hint p { margin: 6px 0 0; font-size: 13px; }
.empty-hint .sub { font-size: 11px; color: var(--efly-text-muted); }
</style>

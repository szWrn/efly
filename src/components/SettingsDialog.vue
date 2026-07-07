<script setup lang="ts">
import { open } from "@tauri-apps/plugin-dialog";
import {
  downloadDir, defaultPort, theme, autoDiscover, overwritePolicy,
} from "../settings";

const visible = defineModel<boolean>("visible", { default: false });

async function pickSaveFolder() {
  const selected = await open({ directory: true, title: "Select default save folder" });
  if (selected && typeof selected === "string") {
    downloadDir.value = selected;
  }
}
</script>

<template>
  <el-dialog v-model="visible" title="Settings" width="480px" :close-on-click-modal="false">
    <el-form label-position="left" label-width="140px">

      <!-- 接收：默认保存目录 -->
      <el-form-item label="Default save dir">
        <div style="display:flex;gap:6px;width:100%">
          <el-input v-model="downloadDir" size="small" placeholder="Downloads" style="flex:1" />
          <el-button size="small" @click="pickSaveFolder">Browse...</el-button>
        </div>
      </el-form-item>

      <!-- 传输：默认端口 -->
      <el-form-item label="Default port">
        <el-input-number v-model="defaultPort" :min="1" :max="65535" size="small" style="width:120px" />
      </el-form-item>

      <!-- 外观：主题 -->
      <el-form-item label="Theme">
        <el-radio-group v-model="theme" size="small">
          <el-radio-button value="system">System</el-radio-button>
          <el-radio-button value="light">Light</el-radio-button>
          <el-radio-button value="dark">Dark</el-radio-button>
        </el-radio-group>
      </el-form-item>

      <!-- 通用：启动时自动发现 -->
      <el-form-item label="Auto-discover">
        <el-switch v-model="autoDiscover" size="small" />
      </el-form-item>

      <!-- 通用：文件覆盖策略 -->
      <el-form-item label="File overwrite">
        <el-radio-group v-model="overwritePolicy" size="small">
          <el-radio-button value="overwrite">Overwrite</el-radio-button>
          <el-radio-button value="rename">Auto-rename</el-radio-button>
          <el-radio-button value="skip">Skip</el-radio-button>
        </el-radio-group>
      </el-form-item>

    </el-form>
    <template #footer>
      <el-button type="primary" @click="visible = false">OK</el-button>
    </template>
  </el-dialog>
</template>

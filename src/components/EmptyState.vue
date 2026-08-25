<script setup lang="ts">
import type { RecentFile } from '../services/recentFiles'
import { formatRecentTime } from '../services/recentFiles'

defineProps<{ recentFiles: RecentFile[]; recentLoading?: boolean }>()
const emit = defineEmits<{
  open: []
  openRecent: [id: string]
  clearRecent: []
}>()
</script>

<template>
  <div class="empty-state">
    <div class="empty-content">
      <div class="empty-icon">PDF</div>
      <h1>打开一个 PDF 文档</h1>
      <p>选择本地 PDF，开始阅读。</p>
      <button class="primary" @click="emit('open')">打开 PDF</button>

      <section v-if="recentFiles.length || recentLoading" class="recent-files" aria-label="最近文件">
        <header>
          <span>最近文件</span>
          <button v-if="recentFiles.length" class="recent-clear" @click="emit('clearRecent')">
            清除
          </button>
        </header>
        <p v-if="recentLoading && !recentFiles.length" class="recent-loading">正在读取…</p>
        <button
          v-for="file in recentFiles"
          :key="file.id"
          class="recent-file"
          :title="file.name"
          @click="emit('openRecent', file.id)"
        >
          <span class="recent-file-icon">PDF</span>
          <span class="recent-file-name">{{ file.name }}</span>
          <time :datetime="new Date(file.lastOpened).toISOString()">
            {{ formatRecentTime(file.lastOpened) }}
          </time>
        </button>
      </section>
    </div>
  </div>
</template>

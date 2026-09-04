<script setup lang="ts">
import { onBeforeUnmount, ref, watch } from 'vue'
import PdfThumbnail from './PdfThumbnail.vue'
import PdfOutline from './PdfOutline.vue'
import { viewerState } from '../stores/viewerState'

const props = defineProps<{ viewer: any }>()
const thumbnailsReady = ref(false)
let firstFrameHandle: number | null = null
let secondFrameHandle: number | null = null

function cancelThumbnailSchedule() {
  if (firstFrameHandle !== null) {
    cancelAnimationFrame(firstFrameHandle)
    firstFrameHandle = null
  }
  if (secondFrameHandle !== null) {
    cancelAnimationFrame(secondFrameHandle)
    secondFrameHandle = null
  }
}

function scheduleThumbnails() {
  cancelThumbnailSchedule()
  thumbnailsReady.value = false
  if (!viewerState.document || !viewerState.contentReady) return
  // Give the target page a paint opportunity before mounting every thumbnail.
  firstFrameHandle = requestAnimationFrame(() => {
    firstFrameHandle = null
    secondFrameHandle = requestAnimationFrame(() => {
      secondFrameHandle = null
      thumbnailsReady.value = true
    })
  })
}

function setMode(mode: 'thumbnail' | 'outline') {
  viewerState.sidebarMode = mode
  if (mode === 'outline' && viewerState.contentReady) {
    void props.viewer?.ensureOutline()
  }
}

watch(
  () => [viewerState.document, viewerState.contentReady] as const,
  () => scheduleThumbnails(),
  { immediate: true },
)

watch(
  () => [viewerState.document, viewerState.sidebarMode, viewerState.contentReady] as const,
  ([, mode, contentReady]) => {
    if (mode === 'outline' && contentReady) void props.viewer?.ensureOutline()
  },
  { immediate: true },
)

onBeforeUnmount(cancelThumbnailSchedule)
</script>

<template>
  <aside class="sidebar">
    <div class="sidebar-tabs">
      <button :class="{ active: viewerState.sidebarMode === 'thumbnail' }" @click="setMode('thumbnail')">缩略图</button>
      <button :class="{ active: viewerState.sidebarMode === 'outline' }" @click="setMode('outline')">目录</button>
    </div>
    <div v-if="viewerState.sidebarMode === 'thumbnail'" class="thumbnail-list">
      <template v-if="thumbnailsReady">
        <PdfThumbnail v-for="page in viewerState.pageCount" :key="page" :page="page" :viewer="viewer" />
      </template>
      <p v-else class="muted sidebar-placeholder">正在准备缩略图…</p>
    </div>
    <PdfOutline
      v-else
      :items="viewerState.outline"
      :loading="viewerState.outlineLoading"
      :viewer="viewer"
    />
  </aside>
</template>

<script setup lang="ts">
import { ref } from 'vue'
import { appearanceState, toggleEyeCareMode } from '../services/eyeCare'
import { isDesktop } from '../services/native'
import { viewerState } from '../stores/viewerState'
defineProps<{ name: string; viewer: any }>(); const emit = defineEmits<{ open: []; about: []; integration: []; pageTools: [] }>(); const page = ref(''); const menuOpen = ref(false)
function go(viewer: any) { viewer?.setPage(Number(page.value)); page.value = '' }
function toggleEyeCare() { toggleEyeCareMode(); menuOpen.value = false }
function openPageTools() { menuOpen.value = false; emit('pageTools') }
</script>
<template>
  <header class="toolbar">
    <span v-if="viewerState.documentName" class="document-name" :title="name">{{ name }}</span>
    <button @click="emit('open')">打开</button><span class="divider"/>
    <button title="上一页" :disabled="!viewerState.pageCount" @click="viewer?.previousPage()">‹</button>
    <form @submit.prevent="go(viewer)"><input v-model="page" class="page-input" :placeholder="String(viewerState.pageNumber)" :disabled="!viewerState.pageCount" /></form>
    <span class="muted">/ {{ viewerState.pageCount }}</span>
    <button title="下一页" :disabled="!viewerState.pageCount" @click="viewer?.nextPage()">›</button><span class="divider"/>
    <button :disabled="!viewerState.pageCount" @click="viewer?.zoomOut()">−</button>
    <span class="scale">{{ Math.round(viewerState.scale * 100) }}%</span>
    <button :disabled="!viewerState.pageCount" @click="viewer?.zoomIn()">+</button>
    <button :disabled="!viewerState.pageCount" @click="viewer?.fitWidth()">适合宽度</button>
    <button :disabled="!viewerState.pageCount" @click="viewer?.fitPage()">适合页面</button>
    <span class="toolbar-spacer"/>
    <button :disabled="!viewerState.pageCount" @click="viewerState.sidebarVisible = !viewerState.sidebarVisible">侧栏</button>
    <button :disabled="!viewerState.pageCount" @click="viewerState.searchVisible = true">搜索</button>
    <div class="app-menu">
      <button aria-label="更多" :aria-expanded="menuOpen" @click="menuOpen = !menuOpen">⋯</button>
      <div v-if="menuOpen" class="app-menu-popover" role="menu">
        <button
          class="menu-check-item"
          role="menuitemcheckbox"
          :aria-checked="appearanceState.eyeCareEnabled"
          title="降低 PDF 页面纯白背景的刺激"
          @click="toggleEyeCare"
        >
          <span class="menu-check" aria-hidden="true">{{ appearanceState.eyeCareEnabled ? '✓' : '' }}</span>
          护眼模式
        </button>
        <span class="menu-separator" role="separator"/>
        <button
          role="menuitem"
          :disabled="!viewerState.pageCount || !isDesktop || viewerState.transforming"
          :title="!isDesktop ? '页面整理功能当前仅桌面版支持' : undefined"
          @click="openPageTools"
        >页面整理…</button>
        <button role="menuitem" @click="menuOpen = false; emit('integration')">Windows 集成…</button>
        <button role="menuitem" @click="menuOpen = false; emit('about')">关于 lw.PDF</button>
      </div>
    </div>
  </header>
</template>

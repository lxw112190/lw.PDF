<script setup lang="ts">
import { computed, onMounted, onUnmounted, ref } from 'vue'
import AppToolbar from './components/AppToolbar.vue'
import EmptyState from './components/EmptyState.vue'
import LoadingOverlay from './components/LoadingOverlay.vue'
import PdfSidebar from './components/PdfSidebar.vue'
import PdfViewerHost from './components/PdfViewerHost.vue'
import SearchBox from './components/SearchBox.vue'
import { openBrowserPdf, openDesktopPdf, revokeDesktopFile } from './services/native'
import { viewerState } from './stores/viewerState'
const viewer = ref<any>(null); const input = ref<HTMLInputElement | null>(null)
const sourceName = computed(() => viewerState.documentName || 'lw.PDF')
function requestOpen() { if (window.lw) void openNative(); else input.value?.click() }
async function openNative() { try { const source = await openDesktopPdf(); if (source) await viewer.value?.open(source) } catch { viewerState.error = '无法打开所选文件，请确认它仍可访问。' } }
async function selectBrowserFile(event: Event) { const file = (event.target as HTMLInputElement).files?.[0]; if (file) await viewer.value?.open(await openBrowserPdf(file)); (event.target as HTMLInputElement).value = '' }
function onKeydown(event: KeyboardEvent) { if (event.ctrlKey && event.key.toLowerCase() === 'o') { event.preventDefault(); requestOpen() }; if (event.ctrlKey && event.key.toLowerCase() === 'f') { event.preventDefault(); viewerState.searchVisible = true }; if (event.ctrlKey && ['+', '='].includes(event.key)) { event.preventDefault(); viewer.value?.zoomIn() }; if (event.ctrlKey && event.key === '-') { event.preventDefault(); viewer.value?.zoomOut() }; if (event.ctrlKey && event.key === '0') { event.preventDefault(); viewer.value?.setScale(1) }; if (event.key === 'PageUp') { event.preventDefault(); viewer.value?.previousPage() }; if (event.key === 'PageDown') { event.preventDefault(); viewer.value?.nextPage() }; if (event.key === 'Home' && !event.ctrlKey) viewer.value?.setPage(1); if (event.key === 'End' && !event.ctrlKey) viewer.value?.setPage(viewerState.pageCount); if (event.key === 'Escape') { viewerState.searchVisible = false; viewerState.error = null } }
onMounted(() => window.addEventListener('keydown', onKeydown)); onUnmounted(() => { window.removeEventListener('keydown', onKeydown); void revokeDesktopFile() })
</script>
<template><div class="app-shell"><AppToolbar :name="sourceName" :viewer="viewer" @open="requestOpen" /><main class="workspace"><PdfSidebar v-if="viewerState.pageCount && viewerState.sidebarVisible" :viewer="viewer" /><section class="viewer-area"><PdfViewerHost ref="viewer" /><EmptyState v-if="!viewerState.pageCount && !viewerState.loading" @open="requestOpen" /><LoadingOverlay v-if="viewerState.loading" /></section></main><SearchBox v-if="viewerState.searchVisible && viewerState.pageCount" :viewer="viewer" /><input ref="input" class="sr-only" type="file" accept="application/pdf,.pdf" @change="selectBrowserFile" /><div v-if="viewerState.error" class="error-toast" role="alert">{{ viewerState.error }}</div></div></template>

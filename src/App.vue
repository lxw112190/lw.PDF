<script setup lang="ts">
import { computed, onMounted, onUnmounted, ref, watch } from 'vue'
import AboutDialog from './components/AboutDialog.vue'
import AnnotationToolbar from './components/AnnotationToolbar.vue'
import AppToolbar from './components/AppToolbar.vue'
import EmptyState from './components/EmptyState.vue'
import LoadingOverlay from './components/LoadingOverlay.vue'
import PageOrganizer from './components/PageOrganizer.vue'
import PageOrganizerExitDialog from './components/PageOrganizerExitDialog.vue'
import PageToolsDialog from './components/PageToolsDialog.vue'
import PdfSidebar from './components/PdfSidebar.vue'
import PdfViewerHost from './components/PdfViewerHost.vue'
import SearchBox from './components/SearchBox.vue'
import WindowsIntegrationDialog from './components/WindowsIntegrationDialog.vue'
import { isPdfFile } from './services/fileDrop'
import {
  nativeFileToPdfSource,
  openBrowserPdf,
  openDesktopPdf,
  revokeDesktopFile,
  type NativeFile,
} from './services/native'
import {
  reversePdfPages,
  rotatePdfPages,
  type PdfPageSelection,
  type PdfRotationDirection,
} from './services/pdfTransform'
import {
  clearRecentFiles,
  loadRecentFiles,
  openRecentFile,
  recentFilesState,
} from './services/recentFiles'
import { viewerState } from './stores/viewerState'
import {
  closePageOrganizer,
  pageOrganizerState,
  resetPageOrganizer,
} from './stores/pageOrganizerState'
import { savePagePlan } from './services/pageOrganizerSave'

const viewer = ref<any>(null)
const input = ref<HTMLInputElement | null>(null)
const aboutVisible = ref(false)
const integrationVisible = ref(false)
const pageToolsVisible = ref(false)
const dropActive = ref(false)
const pageOrganizerExitVisible = ref(false)
let dragDepth = 0

const documentDirty = computed(() =>
  viewerState.annotationDirty || viewerState.annotationSaving ||
  pageOrganizerState.dirty || pageOrganizerState.saving,
)

async function openSource(source: any, position: any = null, bypassDirtyGuard = false) {
  const opened = await viewer.value?.open(source, position, bypassDirtyGuard)
  if (opened && pageOrganizerState.active) closePageOrganizer()
  return opened
}

type PageToolsPayload =
  | { kind: 'reversePages' }
  | { kind: 'rotatePages'; direction: PdfRotationDirection; pages: PdfPageSelection }

async function applyPageTools(payload: PageToolsPayload) {
  const grantId = viewerState.currentGrantId
  if (!grantId) {
    viewerState.error = '当前 PDF 文件权限已失效，请重新打开文件后再试。'
    return
  }
  viewerState.transforming = true
  try {
    const result = payload.kind === 'reversePages'
      ? await reversePdfPages(grantId)
      : await rotatePdfPages(grantId, payload.direction, payload.pages)
    if (result.cancelled) return
    pageToolsVisible.value = false
    await openSource(result.source)
  } catch (error) {
    viewerState.error = error instanceof Error
      ? error.message
      : 'PDF 整理失败，请确认文件未损坏且未受密码保护。'
  } finally {
    viewerState.transforming = false
  }
}

function openPageOrganizer() {
  if (!viewerState.pageCount || !window.lw || viewerState.annotationDirty) return
  if (!viewerState.pageEditAllowed) {
    viewerState.error = '当前 PDF 禁止页面整理。'
    return
  }
  pageToolsVisible.value = false
  viewerState.searchVisible = false
  viewerState.sidebarVisible = false
  viewerState.annotationToolbarVisible = false
  viewer.value?.setAnnotationMode(0)
  resetPageOrganizer(viewerState.pageCount, viewerState.pageNumber, viewerState.pageEditAllowed)
}

function leavePageOrganizer() {
  if (pageOrganizerState.saving) return
  if (pageOrganizerState.dirty) {
    pageOrganizerExitVisible.value = true
    return
  }
  closePageOrganizer()
}

function discardPageOrganizer() {
  pageOrganizerExitVisible.value = false
  closePageOrganizer()
}

async function savePageOrganizer() {
  const grantId = viewerState.currentGrantId
  if (!grantId || pageOrganizerState.saving || !pageOrganizerState.dirty) return
  const focusedId = pageOrganizerState.focusedId
  const focusedSourcePage = pageOrganizerState.pages.find(item => item.id === focusedId)?.sourcePage ?? viewerState.pageNumber
  const finalPage = Math.max(1, pageOrganizerState.pages.findIndex(item => item.id === focusedId) + 1)
  pageOrganizerState.saving = true
  try {
    const result = await savePagePlan(grantId, pageOrganizerState.pages, focusedSourcePage)
    if (result.cancelled) return
    closePageOrganizer()
    await viewer.value?.open(result.source, {
      pageNumber: Math.min(result.pageNumber || finalPage, viewerState.pageCount || result.pageNumber || finalPage),
      scale: viewerState.scale,
      left: 0,
      top: 0,
      updatedAt: Date.now(),
    }, true)
  } catch (error) {
    viewerState.error = error instanceof Error ? error.message : '页面整理保存失败。'
  } finally {
    pageOrganizerState.saving = false
  }
}

const sourceName = computed(() => viewerState.documentName || 'lw.PDF')

function requestOpen() {
  if (window.lw) void openNative()
  else input.value?.click()
}

async function openNative() {
  try {
    const source = await openDesktopPdf()
    if (source) await openSource(source)
  } catch {
    viewerState.error = '无法打开所选文件，请确认它仍可访问。'
  }
}

async function saveAnnotations() {
  await viewer.value?.saveAnnotations()
}

async function openNativeFile(file: NativeFile) {
  if (!file?.id || !file.name || !file.url) return
  try {
    await openSource(nativeFileToPdfSource(file))
  } catch {
    viewerState.error = '无法打开指定的 PDF 文件，请确认它仍可访问。'
  }
}

async function openRecent(id: string) {
  try {
    const source = await openRecentFile(id)
    if (source) await openSource(source)
  } catch {
    await loadRecentFiles()
    viewerState.error = '该最近文件已移动、删除或暂时无法访问。'
  }
}

async function clearRecent() {
  if (!window.confirm('清除全部最近文件记录？这不会删除本地 PDF。')) return
  try {
    await clearRecentFiles()
  } catch {
    viewerState.error = '暂时无法清除最近文件。'
  }
}

async function selectBrowserFile(event: Event) {
  const target = event.target as HTMLInputElement
  const file = target.files?.[0]
  if (file) await openSource(await openBrowserPdf(file))
  target.value = ''
}

function hasDroppedFiles(event: DragEvent) {
  return Array.from(event.dataTransfer?.types ?? []).includes('Files')
}

function usesBrowserDrop(event: DragEvent) {
  return !window.lw && hasDroppedFiles(event)
}

function onDragEnter(event: DragEvent) {
  if (!usesBrowserDrop(event)) return
  event.preventDefault()
  dragDepth++
  dropActive.value = true
}

function onDragOver(event: DragEvent) {
  if (!usesBrowserDrop(event)) return
  event.preventDefault()
  if (event.dataTransfer) event.dataTransfer.dropEffect = 'copy'
}

function onDragLeave(event: DragEvent) {
  if (!usesBrowserDrop(event)) return
  event.preventDefault()
  dragDepth = Math.max(0, dragDepth - 1)
  if (!dragDepth) dropActive.value = false
}

async function onDrop(event: DragEvent) {
  if (!usesBrowserDrop(event)) return
  event.preventDefault()
  dragDepth = 0
  dropActive.value = false
  const file = Array.from(event.dataTransfer?.files ?? []).find(isPdfFile)
  if (!file) {
    viewerState.error = '仅支持拖入 PDF 文件。'
    return
  }
  try {
    await openSource(await openBrowserPdf(file))
  } catch {
    viewerState.error = '无法打开拖入的 PDF 文件。'
  }
}

function onKeydown(event: KeyboardEvent) {
  const target = event.target as HTMLElement | null
  const editing = target instanceof HTMLInputElement ||
    target instanceof HTMLTextAreaElement || !!target?.isContentEditable
  if (event.ctrlKey && event.key.toLowerCase() === 'o') {
    event.preventDefault()
    requestOpen()
    return
  }
  if (event.ctrlKey && event.key.toLowerCase() === 'f') {
    event.preventDefault()
    if (pageOrganizerState.active) return
    viewerState.searchVisible = true
    return
  }
  if (event.ctrlKey && event.key.toLowerCase() === 's') {
    event.preventDefault()
    if (pageOrganizerState.active) void savePageOrganizer()
    else void saveAnnotations()
    return
  }
  if (event.key === 'Escape') {
    if (viewerState.annotationMode !== 0) viewer.value?.setAnnotationMode(0)
    viewerState.searchVisible = false
    viewerState.error = null
    aboutVisible.value = false
    integrationVisible.value = false
    if (!viewerState.transforming) pageToolsVisible.value = false
    return
  }
  if (editing) return
  if (event.ctrlKey && ['+', '='].includes(event.key)) {
    event.preventDefault()
    viewer.value?.zoomIn()
  }
  if (event.ctrlKey && event.key === '-') {
    event.preventDefault()
    viewer.value?.zoomOut()
  }
  if (event.ctrlKey && event.key === '0') {
    event.preventDefault()
    viewer.value?.setScale(1)
  }
  if (event.key === 'PageUp') {
    event.preventDefault()
    viewer.value?.previousPage()
  }
  if (event.key === 'PageDown') {
    event.preventDefault()
    viewer.value?.nextPage()
  }
  if (event.key === 'Home' && !event.ctrlKey) viewer.value?.setPage(1)
  if (event.key === 'End' && !event.ctrlKey) viewer.value?.setPage(viewerState.pageCount)
}

function onBeforeUnload(event: BeforeUnloadEvent) {
  viewer.value?.flushReadingPosition()
  if (viewerState.annotationDirty || viewerState.annotationSaving || pageOrganizerState.dirty || pageOrganizerState.saving) {
    event.preventDefault()
    event.returnValue = ''
  }
}

watch(() => [viewerState.documentName, viewerState.annotationDirty, pageOrganizerState.dirty] as const, ([name, annotationDirty, organizerDirty]) => {
  const dirty = annotationDirty || organizerDirty
  document.title = name ? `${dirty ? '* ' : ''}${name} - lw.PDF` : 'lw.PDF'
})

watch(documentDirty, dirty => {
  void window.lw?.invoke('app.documentDirty', {
    dirty,
    annotationDirty: viewerState.annotationDirty || viewerState.annotationSaving,
    organizerDirty: pageOrganizerState.dirty || pageOrganizerState.saving,
  }).catch(() => {})
}, { immediate: true })

function onDesktopFileOpened(payload: unknown) {
  void openNativeFile(payload as NativeFile)
}

onMounted(() => {
  window.addEventListener('keydown', onKeydown)
  window.addEventListener('dragenter', onDragEnter)
  window.addEventListener('dragover', onDragOver)
  window.addEventListener('dragleave', onDragLeave)
  window.addEventListener('drop', onDrop)
  window.addEventListener('beforeunload', onBeforeUnload)
  window.lw?.on('file.opened', onDesktopFileOpened)
  void loadRecentFiles()
})

onUnmounted(() => {
  window.removeEventListener('keydown', onKeydown)
  window.removeEventListener('dragenter', onDragEnter)
  window.removeEventListener('dragover', onDragOver)
  window.removeEventListener('dragleave', onDragLeave)
  window.removeEventListener('drop', onDrop)
  window.removeEventListener('beforeunload', onBeforeUnload)
  window.lw?.off('file.opened', onDesktopFileOpened)
  viewer.value?.flushReadingPosition()
  void revokeDesktopFile()
})
</script>

<template>
  <div class="app-shell">
    <AppToolbar
      v-if="!pageOrganizerState.active"
      :name="sourceName"
      :viewer="viewer"
      @open="requestOpen"
      @about="aboutVisible = true"
      @integration="integrationVisible = true"
      @page-organizer="openPageOrganizer"
    />
    <PageOrganizer v-if="pageOrganizerState.active" @close="leavePageOrganizer" @save="savePageOrganizer" />
    <PageOrganizerExitDialog
      v-if="pageOrganizerExitVisible"
      @save="pageOrganizerExitVisible = false; void savePageOrganizer()"
      @discard="discardPageOrganizer"
      @cancel="pageOrganizerExitVisible = false"
    />
    <AnnotationToolbar v-if="!pageOrganizerState.active" :viewer="viewer" @save="saveAnnotations" />
    <!-- Keep PdfViewerHost mounted while the organizer is open. Its unmount
         hook closes the PDF document, which would leave every organizer
         thumbnail without a PDFDocumentProxy. v-show hides the reader while
         preserving the live document and reading state. -->
    <main v-show="!pageOrganizerState.active" class="workspace">
      <PdfSidebar
        v-if="viewerState.pageCount && viewerState.sidebarVisible"
        :viewer="viewer"
      />
      <section class="viewer-area">
        <PdfViewerHost ref="viewer" />
        <EmptyState
          v-if="!viewerState.pageCount && !viewerState.loading"
          :recent-files="recentFilesState.files"
          :recent-loading="recentFilesState.loading"
          @open="requestOpen"
          @open-recent="openRecent"
          @clear-recent="clearRecent"
        />
        <LoadingOverlay v-if="viewerState.loading" />
      </section>
    </main>
    <SearchBox
      v-if="viewerState.searchVisible && viewerState.pageCount"
      :viewer="viewer"
    />
    <AboutDialog v-if="aboutVisible" @close="aboutVisible = false" />
    <WindowsIntegrationDialog
      v-if="integrationVisible"
      @close="integrationVisible = false"
    />
    <PageToolsDialog
      v-if="pageToolsVisible"
      :page-number="viewerState.pageNumber"
      :page-count="viewerState.pageCount"
      :transforming="viewerState.transforming"
      @close="pageToolsVisible = false"
      @apply="applyPageTools"
    />
    <input
      ref="input"
      class="sr-only"
      type="file"
      accept="application/pdf,.pdf"
      @change="selectBrowserFile"
    />
    <div v-if="dropActive" class="drop-overlay">释放鼠标打开 PDF</div>
    <div v-if="viewerState.error" class="error-toast" role="alert">
      {{ viewerState.error }}
    </div>
  </div>
</template>

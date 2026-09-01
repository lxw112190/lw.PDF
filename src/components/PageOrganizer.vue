<script setup lang="ts">
import { computed, onMounted, onUnmounted } from 'vue'
import PageOrganizerThumbnail from './PageOrganizerThumbnail.vue'
import {
  clonePagePlan,
  movePageGroup,
  reversePagePlan,
  rotatePagePlan,
  selectPageRange,
  snapshotPagePlan,
} from '../services/pageOrganizer'
import {
  pageOrganizerHistory,
  pageOrganizerState,
  syncHistoryState,
  updatePageOrganizerDirty,
} from '../stores/pageOrganizerState'
import { isDesktop } from '../services/native'

const emit = defineEmits<{ close: []; save: [] }>()
const pages = computed(() => pageOrganizerState.pages)
const selectedCount = computed(() => pageOrganizerState.selectedIds.length)
let pointerDrag: { itemId: string; pointerId: number; startX: number; startY: number; active: boolean } | null = null
let suppressClick = false

function replacePages(next: ReturnType<typeof clonePagePlan>) {
  const before = snapshotPagePlan(pageOrganizerState.pages)
  if (JSON.stringify(before.pages) === JSON.stringify(next)) return
  pageOrganizerHistory.push(before)
  pageOrganizerState.pages = next
  updatePageOrganizerDirty()
  syncHistoryState()
}

function select(itemId: string, event: MouseEvent) {
  if (suppressClick) {
    suppressClick = false
    return
  }
  const current = pageOrganizerState.selectedIds
  if (event.shiftKey && pageOrganizerState.anchorId) {
    pageOrganizerState.selectedIds = selectPageRange(pageOrganizerState.pages, pageOrganizerState.anchorId, itemId)
  } else if (event.ctrlKey || event.metaKey) {
    pageOrganizerState.selectedIds = current.includes(itemId)
      ? current.filter(id => id !== itemId)
      : [...current, itemId]
    pageOrganizerState.anchorId = itemId
  } else {
    pageOrganizerState.selectedIds = [itemId]
    pageOrganizerState.anchorId = itemId
  }
  pageOrganizerState.focusedId = itemId
}

function reverse() {
  replacePages(reversePagePlan(pageOrganizerState.pages))
}

function rotate(delta: 90 | -90 | 180) {
  const ids = pageOrganizerState.selectedIds.length
    ? pageOrganizerState.selectedIds
    : pageOrganizerState.focusedId ? [pageOrganizerState.focusedId] : []
  replacePages(rotatePagePlan(pageOrganizerState.pages, ids, delta))
}

function undo() {
  const next = pageOrganizerHistory.undo(snapshotPagePlan(pageOrganizerState.pages))
  if (!next) return
  pageOrganizerState.pages = clonePagePlan(next.pages)
  updatePageOrganizerDirty()
  syncHistoryState()
}

function redo() {
  const next = pageOrganizerHistory.redo(snapshotPagePlan(pageOrganizerState.pages))
  if (!next) return
  pageOrganizerState.pages = clonePagePlan(next.pages)
  updatePageOrganizerDirty()
  syncHistoryState()
}

function beginPointerDrag(itemId: string, event: PointerEvent) {
  if (pageOrganizerState.saving || !pageOrganizerState.editable) return
  if (!pageOrganizerState.selectedIds.includes(itemId)) {
    pageOrganizerState.selectedIds = [itemId]
    pageOrganizerState.anchorId = itemId
    pageOrganizerState.focusedId = itemId
  }
  pageOrganizerState.dragIds = [...pageOrganizerState.selectedIds]
  pointerDrag = {
    itemId,
    pointerId: event.pointerId,
    startX: event.clientX,
    startY: event.clientY,
    active: false,
  }
}

function updatePointerDrop(event: PointerEvent) {
  const element = document.elementFromPoint(event.clientX, event.clientY) as HTMLElement | null
  const card = element?.closest('.page-organizer-card') as HTMLElement | null
  const targetId = card?.dataset.pageId
  if (!targetId || !pointerDrag?.active) return
  const rect = card.getBoundingClientRect()
  pageOrganizerState.dropTargetId = targetId
  pageOrganizerState.dropBefore = event.clientY < rect.top + rect.height / 2
  const scroll = card.closest('.page-organizer-scroll') as HTMLElement | null
  if (scroll) {
    const bounds = scroll.getBoundingClientRect()
    if (event.clientY < bounds.top + 48) scroll.scrollBy({ top: -24 })
    else if (event.clientY > bounds.bottom - 48) scroll.scrollBy({ top: 24 })
  }
}

function pointerMove(itemId: string, event: PointerEvent) {
  if (!pointerDrag || pointerDrag.itemId !== itemId || pointerDrag.pointerId !== event.pointerId) return
  const distance = Math.hypot(event.clientX - pointerDrag.startX, event.clientY - pointerDrag.startY)
  if (!pointerDrag.active && distance < 6) return
  if (!pointerDrag.active) {
    pointerDrag.active = true
    suppressClick = true
  }
  event.preventDefault()
  updatePointerDrop(event)
}

function finishPointerDrag(event: PointerEvent) {
  if (!pointerDrag || pointerDrag.pointerId !== event.pointerId) return
  if (pointerDrag.active && pageOrganizerState.dropTargetId) {
    replacePages(movePageGroup(
      pageOrganizerState.pages,
      pageOrganizerState.dragIds,
      pageOrganizerState.dropTargetId,
      pageOrganizerState.dropBefore,
    ))
  }
  pointerDrag = null
  pageOrganizerState.dragIds = []
  pageOrganizerState.dropTargetId = null
}

function onKeydown(event: KeyboardEvent) {
  if (event.target instanceof HTMLInputElement || event.target instanceof HTMLTextAreaElement) return
  if (event.ctrlKey && event.key.toLowerCase() === 'a') {
    event.preventDefault()
    pageOrganizerState.selectedIds = pages.value.map(item => item.id)
    return
  }
  if (event.ctrlKey && event.key.toLowerCase() === 'z') {
    event.preventDefault()
    undo()
  } else if (event.ctrlKey && event.key.toLowerCase() === 'y') {
    event.preventDefault()
    redo()
  } else if (event.key === 'Escape') {
    event.preventDefault()
    if (pageOrganizerState.selectedIds.length) pageOrganizerState.selectedIds = []
    else emit('close')
  }
}

onMounted(() => window.addEventListener('keydown', onKeydown))
onUnmounted(() => window.removeEventListener('keydown', onKeydown))
</script>

<template>
  <section class="page-organizer" aria-label="页面整理">
    <header class="page-organizer-toolbar">
      <button type="button" :disabled="pageOrganizerState.saving" @click="emit('close')">← 返回阅读</button>
      <span class="page-organizer-title">整理页面<span v-if="pageOrganizerState.dirty"> *</span></span>
      <span class="page-organizer-spacer" />
      <button type="button" title="撤销" :disabled="!pageOrganizerState.canUndo || pageOrganizerState.saving" @click="undo">↶ 撤销</button>
      <button type="button" title="重做" :disabled="!pageOrganizerState.canRedo || pageOrganizerState.saving" @click="redo">↷ 重做</button>
      <button type="button" :disabled="!pages.length || pageOrganizerState.saving" @click="reverse">反转全部</button>
      <button type="button" :disabled="!selectedCount || pageOrganizerState.saving" @click="rotate(-90)">↶ 左转</button>
      <button type="button" :disabled="!selectedCount || pageOrganizerState.saving" @click="rotate(90)">↷ 右转</button>
      <span v-if="pageOrganizerState.dirty" class="page-organizer-dirty">未保存</span>
      <button type="button" class="page-organizer-save" :disabled="!pageOrganizerState.dirty || pageOrganizerState.saving || !pageOrganizerState.editable || !isDesktop" @click="emit('save')">
        {{ pageOrganizerState.saving ? '保存中…' : '另存为' }}
      </button>
    </header>
    <div class="page-organizer-scroll">
      <div class="page-organizer-grid">
        <PageOrganizerThumbnail
          v-for="(item, index) in pages"
          :key="item.id"
          :item="item"
          :index="index"
          :selected="pageOrganizerState.selectedIds.includes(item.id)"
          :focused="pageOrganizerState.focusedId === item.id"
          :dragging="pageOrganizerState.dragIds.includes(item.id)"
          :drop-before="pageOrganizerState.dropTargetId === item.id && pageOrganizerState.dropBefore"
          :drop-after="pageOrganizerState.dropTargetId === item.id && !pageOrganizerState.dropBefore"
          @select="select(item.id, $event)"
          @pointer-down="beginPointerDrag(item.id, $event)"
          @pointer-move="pointerMove(item.id, $event)"
          @pointer-up="finishPointerDrag"
          @pointer-cancel="finishPointerDrag"
        />
      </div>
    </div>
  </section>
</template>

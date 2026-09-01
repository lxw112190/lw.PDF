<script setup lang="ts">
import { nextTick, onBeforeUnmount, onMounted, ref, watch } from 'vue'
import { viewerState } from '../stores/viewerState'
import type { PageEditItem } from '../services/pageOrganizer'

const props = defineProps<{
  item: PageEditItem
  index: number
  selected: boolean
  focused: boolean
  dragging: boolean
  dropBefore: boolean
  dropAfter: boolean
}>()
const emit = defineEmits<{
  select: [event: MouseEvent]
  pointerDown: [event: PointerEvent]
  pointerMove: [event: PointerEvent]
  pointerUp: [event: PointerEvent]
  pointerCancel: [event: PointerEvent]
}>()

const root = ref<HTMLElement | null>(null)
const canvas = ref<HTMLCanvasElement | null>(null)
let observer: IntersectionObserver | undefined
let renderTask: { cancel?: () => void; promise?: Promise<unknown> } | undefined
let renderGeneration = 0
let nearViewport = false

function clearCanvas() {
  renderTask?.cancel?.()
  renderTask = undefined
  if (canvas.value) {
    canvas.value.width = 0
    canvas.value.height = 0
  }
}

function cancelRender() {
  renderTask?.cancel?.()
  renderTask = undefined
}

async function render() {
  const generation = ++renderGeneration
  const document = viewerState.document
  const target = canvas.value
  if (!document || !target) return
  clearCanvas()
  try {
    const page = await document.getPage(props.item.sourcePage)
    if (generation !== renderGeneration || !canvas.value) return
    const rotation = ((page.rotate + props.item.rotation) % 360 + 360) % 360
    const viewport = page.getViewport({ scale: 0.18, rotation })
    target.width = Math.ceil(viewport.width)
    target.height = Math.ceil(viewport.height)
    const context = target.getContext('2d')
    if (!context) return
    renderTask = page.render({ canvasContext: context, viewport })
    await renderTask.promise
  } catch (error: any) {
    if (error?.name !== 'RenderingCancelledException') {
      console.error('Page organizer thumbnail render failed', error)
    }
  } finally {
    renderTask = undefined
  }
}

function onIntersect(entries: IntersectionObserverEntry[]) {
  nearViewport = entries[0]?.isIntersecting === true
  if (nearViewport) void render()
  // Keep an already rendered bitmap when it leaves the viewport. Clearing it
  // here makes a transient WebView2 observer update look like a blank thumbnail
  // and forces the user to scroll away and back before it is painted again.
  else cancelRender()
}

function onPointerDown(event: PointerEvent) {
  if (event.isPrimary && event.button === 0) {
    root.value?.setPointerCapture?.(event.pointerId)
    emit('pointerDown', event)
  }
}

function onPointerUp(event: PointerEvent) {
  if (root.value?.hasPointerCapture?.(event.pointerId)) {
    root.value.releasePointerCapture(event.pointerId)
  }
  emit('pointerUp', event)
}

function onPointerCancel(event: PointerEvent) {
  if (root.value?.hasPointerCapture?.(event.pointerId)) {
    root.value.releasePointerCapture(event.pointerId)
  }
  emit('pointerCancel', event)
}

function isNearScrollViewport(): boolean {
  const card = root.value
  const scroll = card?.closest('.page-organizer-scroll') as HTMLElement | null
  if (!card || !scroll) return true
  const cardRect = card.getBoundingClientRect()
  const scrollRect = scroll.getBoundingClientRect()
  return cardRect.bottom >= scrollRect.top - 400 && cardRect.top <= scrollRect.bottom + 400
}

onMounted(() => {
  // WebView2 can occasionally skip the first IntersectionObserver notification
  // while a newly shown view is settling. Do an explicit first viewport check so
  // the initial thumbnails are never left blank.
  void nextTick(() => {
    nearViewport = isNearScrollViewport()
    // Paint the first visible batch eagerly. IntersectionObserver remains in
    // charge of thumbnails that enter the viewport later.
    if (nearViewport || props.index < 40) void render()
  })
  if (typeof IntersectionObserver === 'undefined') return
  observer = new IntersectionObserver(onIntersect, {
    root: root.value?.closest('.page-organizer-scroll'),
    rootMargin: '400px',
  })
  if (root.value) observer.observe(root.value)
})

watch(() => [props.item.sourcePage, props.item.rotation, viewerState.document], () => {
  if (nearViewport) void render()
})

onBeforeUnmount(() => {
  ++renderGeneration
  observer?.disconnect()
  clearCanvas()
})
</script>

<template>
  <!-- Pointer events are the primary reorder path. WebView2 can start an
       HTML5 drag session without delivering dragover/drop to Vue. -->
  <div
    ref="root"
    class="page-organizer-card"
    :class="{ selected, focused, dragging, 'drop-before': dropBefore, 'drop-after': dropAfter }"
    :data-page-id="item.id"
    draggable="false"
    role="button"
    tabindex="0"
    :aria-label="`第 ${index + 1} 页，原第 ${item.sourcePage} 页`"
    @click="emit('select', $event)"
    @pointerdown="onPointerDown"
    @pointermove="emit('pointerMove', $event)"
    @pointerup="onPointerUp"
    @pointercancel="onPointerCancel"
    @dragstart.prevent.stop
  >
    <span v-if="selected" class="page-organizer-check" aria-hidden="true">✓</span>
    <span class="page-organizer-preview"><canvas ref="canvas" draggable="false" /></span>
    <span class="page-organizer-page-number">第 {{ index + 1 }} 页</span>
    <span class="page-organizer-source-number">原第 {{ item.sourcePage }} 页<span v-if="item.rotation"> · {{ item.rotation }}°</span></span>
  </div>
</template>

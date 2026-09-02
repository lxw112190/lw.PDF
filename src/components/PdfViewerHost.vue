<script setup lang="ts">
import { onMounted, onUnmounted, ref } from 'vue'
import { PdfViewerController } from '../services/pdfViewer'
import { viewerState } from '../stores/viewerState'
const container = ref<HTMLDivElement | null>(null); const element = ref<HTMLDivElement | null>(null); const controller = new PdfViewerController()
onMounted(() => controller.init(container.value!, element.value!)); onUnmounted(() => { void controller.close() })
function onWheel(event: WheelEvent) {
  if (!event.ctrlKey || !viewerState.pageCount || event.deltaY === 0) return
  event.preventDefault()
  if (event.deltaY < 0) controller.zoomIn()
  else controller.zoomOut()
}
defineExpose(controller)
</script>
<template><div ref="container" class="pdf-viewer-container" @wheel="onWheel"><div ref="element" class="pdfViewer" /></div></template>

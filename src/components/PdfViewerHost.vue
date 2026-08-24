<script setup lang="ts">
import { onMounted, onUnmounted, ref } from 'vue'
import { PdfViewerController } from '../services/pdfViewer'
const container = ref<HTMLDivElement | null>(null); const element = ref<HTMLDivElement | null>(null); const controller = new PdfViewerController()
onMounted(() => controller.init(container.value!, element.value!)); onUnmounted(() => { void controller.close() })
defineExpose(controller)
</script>
<template><div ref="container" class="pdf-viewer-container"><div ref="element" class="pdfViewer" /></div></template>

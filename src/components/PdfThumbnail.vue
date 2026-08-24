<script setup lang="ts">
import { onBeforeUnmount, onMounted, ref } from 'vue'
import { viewerState } from '../stores/viewerState'
const props = defineProps<{ page: number; viewer: any }>(); const root = ref<HTMLButtonElement | null>(null); const canvas = ref<HTMLCanvasElement | null>(null); let observer: IntersectionObserver | undefined; let rendered = false
async function render() { if (rendered || !viewerState.document || !canvas.value) return; rendered = true; try { const pdfPage = await viewerState.document.getPage(props.page); const viewport = pdfPage.getViewport({ scale: .22 }); const context = canvas.value.getContext('2d')!; canvas.value.width = Math.ceil(viewport.width); canvas.value.height = Math.ceil(viewport.height); await pdfPage.render({ canvasContext: context, viewport }).promise } catch (error) { rendered = false; console.error('Thumbnail render failed', error) } }
onMounted(() => { observer = new IntersectionObserver(entries => { if (entries[0]?.isIntersecting) { void render(); observer?.disconnect() } }, { root: root.value?.parentElement, rootMargin: '320px' }); if (root.value) observer.observe(root.value) }); onBeforeUnmount(() => observer?.disconnect())
</script>
<template><button ref="root" class="thumbnail" :class="{ selected: viewerState.pageNumber === page }" @click="viewer?.setPage(page)"><canvas ref="canvas"/><span>{{ page }}</span></button></template>

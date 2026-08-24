<script setup lang="ts">
import PdfThumbnail from './PdfThumbnail.vue'
import PdfOutline from './PdfOutline.vue'
import { viewerState } from '../stores/viewerState'
defineProps<{ viewer: any }>()
</script>
<template><aside class="sidebar"><div class="sidebar-tabs"><button :class="{ active: viewerState.sidebarMode === 'thumbnail' }" @click="viewerState.sidebarMode = 'thumbnail'">缩略图</button><button :class="{ active: viewerState.sidebarMode === 'outline' }" @click="viewerState.sidebarMode = 'outline'">目录</button></div><div v-if="viewerState.sidebarMode === 'thumbnail'" class="thumbnail-list"><PdfThumbnail v-for="page in viewerState.pageCount" :key="page" :page="page" :viewer="viewer" /></div><PdfOutline v-else :items="viewerState.outline" :viewer="viewer" /></aside></template>

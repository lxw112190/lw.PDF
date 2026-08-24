<script setup lang="ts">
import type { OutlineItem } from '../types/pdf'
defineProps<{ items: OutlineItem[]; viewer: any }>()
</script>
<template><div class="outline"><p v-if="!items.length" class="muted">此文档没有目录。</p><ul v-else><li v-for="(item, index) in items" :key="`${index}-${item.title}-${String(item.dest ?? '')}`"><button @click="item.dest && viewer?.goToDestination(item.dest)">{{ item.title || '未命名章节' }}</button><PdfOutline v-if="item.items?.length" :items="item.items" :viewer="viewer" /></li></ul></div></template>

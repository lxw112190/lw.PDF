<script setup lang="ts">
import { nextTick, ref, watch } from 'vue'
import { viewerState } from '../stores/viewerState'
const props = defineProps<{ viewer: any }>(); const input = ref<HTMLInputElement | null>(null); const query = ref(viewerState.searchQuery)
watch(query, value => props.viewer?.find(value)); nextTick(() => input.value?.focus())
</script>
<template><div class="search-box"><input ref="input" v-model="query" placeholder="搜索 PDF..." @keydown.enter.prevent="viewer?.find(query)" /><span>{{ viewerState.searchCurrent }} / {{ viewerState.searchTotal }}</span><button @click="viewer?.find(query, true)">↑</button><button @click="viewer?.find(query)">↓</button><button @click="viewerState.searchVisible = false">×</button></div></template>

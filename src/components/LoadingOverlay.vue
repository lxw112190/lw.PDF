<template><div class="loading"><i/>正在打开 {{ viewerState.documentName || 'PDF 文档' }}</div></template>
<script setup lang="ts">import { viewerState } from '../stores/viewerState'</script>

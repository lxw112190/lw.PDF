<script setup lang="ts">const emit = defineEmits<{ open: [] }>()</script>
<template><div class="empty-state"><div class="empty-icon">PDF</div><h1>打开一个 PDF 文档</h1><p>选择本地 PDF，开始阅读。</p><button class="primary" @click="emit('open')">打开 PDF</button></div></template>

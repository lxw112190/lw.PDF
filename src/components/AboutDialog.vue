<script setup lang="ts">
import { onMounted, onUnmounted } from 'vue'
import packageInfo from '../../package.json'
defineEmits<{ close: [] }>()
function onKeydown(event: KeyboardEvent) { if (event.key === 'Escape') window.dispatchEvent(new CustomEvent('lw-pdf-close-dialog')) }
function openExternal(url: string) { if (window.lw) { void window.lw.invoke('app.openExternal', { url }); return } window.open(url, '_blank', 'noopener,noreferrer') }
onMounted(() => window.addEventListener('keydown', onKeydown)); onUnmounted(() => window.removeEventListener('keydown', onKeydown))
</script>
<template><div class="dialog-backdrop" @mousedown.self="$emit('close')"><section class="dialog" role="dialog" aria-modal="true" aria-labelledby="about-title"><button class="dialog-close" aria-label="关闭" @click="$emit('close')">×</button><img class="about-icon" src="/app-icon.png" alt=""/><h2 id="about-title">lw.PDF</h2><p class="dialog-version">版本 {{ packageInfo.version }}</p><p>简洁、轻量的本地 PDF 查看器。</p><div class="dialog-actions"><a class="primary" href="https://github.com/lxw112190/lw.PDF" target="_blank" rel="noreferrer" @click.prevent="openExternal('https://github.com/lxw112190/lw.PDF')">GitHub 项目主页</a><a href="https://github.com/lxw112190/lw.PDF/releases/latest" target="_blank" rel="noreferrer" @click.prevent="openExternal('https://github.com/lxw112190/lw.PDF/releases/latest')">查看最新版本</a></div><p class="dialog-meta">作者：天天代码码天天<br/>MIT License · Copyright © 2026</p></section></div></template>

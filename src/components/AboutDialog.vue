<script setup lang="ts">
import { onMounted, onUnmounted } from 'vue'
import packageInfo from '../../package.json'

const sponsorImageUrl = new URL('../../docs/assets/sponsor.jpg', import.meta.url).href

defineEmits<{ close: [] }>()
function onKeydown(event: KeyboardEvent) { if (event.key === 'Escape') window.dispatchEvent(new CustomEvent('lw-pdf-close-dialog')) }
function openExternal(url: string) { if (window.lw) { void window.lw.invoke('app.openExternal', { url }); return } window.open(url, '_blank', 'noopener,noreferrer') }
onMounted(() => window.addEventListener('keydown', onKeydown)); onUnmounted(() => window.removeEventListener('keydown', onKeydown))
</script>
<template><div class="dialog-backdrop" @mousedown.self="$emit('close')"><section class="dialog about-dialog" role="dialog" aria-modal="true" aria-labelledby="about-title"><button class="dialog-close" aria-label="关闭" @click="$emit('close')">×</button><img class="about-icon" src="/app-icon.png" alt=""/><h2 id="about-title">lw.PDF</h2><p class="dialog-version about-version">版本 {{ packageInfo.version }}</p><p class="about-description">简洁、轻量的本地 PDF 查看器。</p><div class="about-support"><div class="about-contact"><h3>联系与支持</h3><dl><div><dt>作者</dt><dd>天天代码码天天</dd></div><div><dt>QQ</dt><dd>819069052</dd></div><div><dt>QQ群</dt><dd>C# 人工智能实践<small>群号：758616458</small></dd></div></dl><p>如果项目对你有帮助，可以扫码支持后续维护。</p></div><figure class="about-sponsor"><svg viewBox="280 350 558 558" role="img" aria-label="微信赞助二维码"><image :href="sponsorImageUrl" x="0" y="0" width="1118" height="1536" /></svg><figcaption>微信扫码支持维护</figcaption></figure></div><div class="dialog-actions"><a class="primary" href="https://github.com/lxw112190/lw.PDF" target="_blank" rel="noreferrer" @click.prevent="openExternal('https://github.com/lxw112190/lw.PDF')">GitHub 项目主页</a><a href="https://github.com/lxw112190/lw.PDF/releases/latest" target="_blank" rel="noreferrer" @click.prevent="openExternal('https://github.com/lxw112190/lw.PDF/releases/latest')">查看最新版本</a></div><p class="dialog-meta about-meta">MIT License · Copyright © 2026</p></section></div></template>

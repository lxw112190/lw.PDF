<script setup lang="ts">
import { ref } from 'vue'
import type { PdfPageSelection, PdfRotationDirection } from '../services/pdfTransform'

const props = defineProps<{ pageNumber: number; pageCount: number; transforming: boolean }>()
const emit = defineEmits<{
  close: []
  apply: [payload: { kind: 'reversePages' } | { kind: 'rotatePages'; direction: PdfRotationDirection; pages: PdfPageSelection }]
}>()

const target = ref<'current' | 'all' | 'range'>('current')
const range = ref('')
const rangeHint = ref('')

const rangePattern = /^\s*\d+(\s*-\s*\d+)?(\s*,\s*\d+(\s*-\s*\d+)?)*\s*$/

function applyReverse() {
  if (props.transforming) return
  emit('apply', { kind: 'reversePages' })
}

function applyRotate(direction: PdfRotationDirection) {
  if (props.transforming) return
  if (target.value === 'current') {
    emit('apply', { kind: 'rotatePages', direction, pages: { kind: 'single', page: props.pageNumber } })
    return
  }
  if (target.value === 'all') {
    emit('apply', { kind: 'rotatePages', direction, pages: { kind: 'all' } })
    return
  }
  if (!rangePattern.test(range.value)) {
    rangeHint.value = '页面范围格式不正确，请输入如 1,3,5-8。'
    return
  }
  rangeHint.value = ''
  emit('apply', { kind: 'rotatePages', direction, pages: { kind: 'range', value: range.value } })
}
</script>

<template>
  <div class="dialog-backdrop" @mousedown.self="!transforming && emit('close')">
    <section class="dialog page-tools-dialog" role="dialog" aria-modal="true" aria-labelledby="page-tools-title">
      <button class="dialog-close" aria-label="关闭" :disabled="transforming" @click="emit('close')">×</button>
      <h2 id="page-tools-title">页面工具</h2>

      <fieldset class="tool-block" :disabled="transforming">
        <legend>页面顺序</legend>
        <p class="tool-hint">将最后一页变为第一页，适合扫描顺序完全颠倒的 PDF。</p>
        <button class="primary" @click="applyReverse">反转全部页面顺序并另存为</button>
      </fieldset>

      <fieldset class="tool-block" :disabled="transforming">
        <legend>页面旋转</legend>
        <div class="rotate-targets">
          <label><input v-model="target" type="radio" value="current" /> 当前页（第 {{ pageNumber }} 页）</label>
          <label><input v-model="target" type="radio" value="all" /> 全部页面</label>
          <label><input v-model="target" type="radio" value="range" /> 指定页面
            <input
              v-model="range"
              class="range-input"
              type="text"
              placeholder="1,3,5-8"
              :disabled="target !== 'range'"
              @input="rangeHint = ''"
            />
          </label>
        </div>
        <p v-if="rangeHint" class="dialog-error">{{ rangeHint }}</p>
        <div class="rotate-actions">
          <button @click="applyRotate('left90')">向左旋转 90°</button>
          <button @click="applyRotate('rotate180')">旋转 180°</button>
          <button @click="applyRotate('right90')">向右旋转 90°</button>
        </div>
      </fieldset>

      <p v-if="transforming" class="tool-busy">正在整理 PDF…</p>

      <div class="dialog-actions">
        <span class="dialog-spacer" />
        <button :disabled="transforming" @click="emit('close')">关闭</button>
      </div>
    </section>
  </div>
</template>

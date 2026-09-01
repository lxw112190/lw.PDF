<script setup lang="ts">
import * as pdfjsLib from 'pdfjs-dist'
import { computed } from 'vue'
import { viewerState } from '../stores/viewerState'

defineProps<{ viewer: any }>()
const emit = defineEmits<{ save: [] }>()
const enabled = computed(() => viewerState.pageCount > 0 && viewerState.annotationEditable)
const modes = [
  { value: pdfjsLib.AnnotationEditorType.NONE, label: '选择' },
  { value: pdfjsLib.AnnotationEditorType.HIGHLIGHT, label: '高亮' },
  { value: pdfjsLib.AnnotationEditorType.FREETEXT, label: '文字' },
  { value: pdfjsLib.AnnotationEditorType.INK, label: '手写' },
]
</script>

<template>
  <div v-if="viewerState.pageCount && viewerState.annotationToolbarVisible" class="annotation-toolbar" role="toolbar" aria-label="批注工具">
    <span class="annotation-label">批注</span>
    <span v-if="!viewerState.annotationEditable" class="annotation-readonly">当前 PDF 禁止修改</span>
    <button
      v-for="mode in modes"
      :key="mode.value"
      :class="{ active: viewerState.annotationMode === mode.value }"
      :disabled="!enabled"
      @click="viewer?.setAnnotationMode(mode.value)"
    >{{ mode.label }}</button>
    <button
      :disabled="!enabled || !viewerState.annotationCanUndo"
      title="撤销"
      @click="viewer?.undoAnnotation()"
    >撤销</button>
    <button
      :disabled="!enabled || !viewerState.annotationCanRedo"
      title="重做"
      @click="viewer?.redoAnnotation()"
    >重做</button>
    <button
      :disabled="!enabled || !viewerState.annotationHasSelection"
      title="删除选中的批注"
      @click="viewer?.deleteAnnotation()"
    >删除</button>
    <span class="toolbar-spacer"/>
    <span v-if="viewerState.annotationDirty" class="annotation-dirty">未保存</span>
    <button
      class="save-annotation"
      :disabled="!enabled || !viewerState.annotationDirty || viewerState.annotationSaving"
      @click="emit('save')"
    >{{ viewerState.annotationSaving ? '保存中…' : '另存批注' }}</button>
  </div>
</template>

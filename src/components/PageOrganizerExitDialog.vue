<script setup lang="ts">
defineEmits<{ save: []; discard: []; cancel: [] }>()
</script>

<template>
  <div class="dialog-backdrop" @mousedown.self="$emit('cancel')">
    <section class="dialog page-organizer-exit-dialog" role="dialog" aria-modal="true" aria-labelledby="page-organizer-exit-title">
      <h2 id="page-organizer-exit-title">页面整理尚未保存</h2>
      <p>返回阅读前，是否保存当前页面顺序和旋转修改？</p>
      <div class="dialog-actions">
        <button class="primary" type="button" @click="$emit('save')">另存为并返回</button>
        <button type="button" @click="$emit('discard')">放弃修改</button>
        <button type="button" @click="$emit('cancel')">继续编辑</button>
      </div>
    </section>
  </div>
</template>

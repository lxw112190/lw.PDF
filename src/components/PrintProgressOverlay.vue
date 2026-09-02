<script setup lang="ts">
import { printState } from '../stores/printState'

const emit = defineEmits<{ cancel: [] }>()
</script>

<template>
  <div v-if="printState.active && printState.phase === 'preparing'" class="print-progress-backdrop" role="presentation">
    <section class="print-progress-dialog" role="dialog" aria-modal="true" aria-labelledby="print-progress-title">
      <h2 id="print-progress-title">正在准备打印…</h2>
      <p>{{ printState.current }} / {{ printState.total }}</p>
      <button type="button" @click="emit('cancel')">取消</button>
    </section>
  </div>
</template>

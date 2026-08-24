<script setup lang="ts">
import { computed, onMounted, onUnmounted, ref } from 'vue'
import { desktop, type FileAssociationStatus } from '../services/desktop'

const emit = defineEmits<{ close: [] }>()
const info = ref<FileAssociationStatus | null>(null)
const busy = ref(false)
const error = ref('')

const stateLabel = computed(() => {
  if (!info.value) return '正在检查'
  if (info.value.defaultApplication) return '默认应用'
  if (info.value.current) return '已注册'
  return info.value.registered ? '需要修复' : '未注册'
})

async function refresh() {
  error.value = ''
  try {
    info.value = await desktop.association.status()
  } catch (cause) {
    error.value = cause instanceof Error ? cause.message : '无法读取 Windows 集成状态。'
  }
}

async function openDefaultApps() {
  try {
    await desktop.association.openDefaultApps()
  } catch (cause) {
    error.value = cause instanceof Error ? cause.message : '无法打开默认应用设置。'
  }
}

async function update(action: 'register' | 'unregister') {
  if (busy.value) return
  busy.value = true
  error.value = ''
  try {
    await desktop.association[action]()
    await refresh()
    if (action === 'register' && info.value?.current && !info.value.defaultApplication) {
      await openDefaultApps()
    }
  } catch (cause) {
    error.value = cause instanceof Error ? cause.message : 'Windows 集成操作失败。'
  } finally {
    busy.value = false
  }
}

function onWindowFocus() {
  if (!busy.value) void refresh()
}

onMounted(() => {
  window.addEventListener('focus', onWindowFocus)
  void refresh()
})
onUnmounted(() => window.removeEventListener('focus', onWindowFocus))
</script>

<template>
  <div class="dialog-backdrop" @mousedown.self="!busy && emit('close')">
    <section class="dialog integration-dialog" role="dialog" aria-modal="true" aria-labelledby="integration-title">
      <button class="dialog-close" aria-label="关闭" :disabled="busy" @click="emit('close')">×</button>
      <div class="integration-heading">
        <div>
          <h2 id="integration-title">Windows 集成</h2>
          <p>注册打开方式，并由你决定是否设为默认 PDF 应用。</p>
        </div>
        <span class="integration-status" :class="{ current: info?.current, repair: info?.registered && !info?.current }">{{ stateLabel }}</span>
      </div>
      <ul>
        <li>右键菜单显示“使用 lw.PDF 打开”</li>
        <li>Windows“打开方式”显示 lw.PDF</li>
        <li>支持双击打开 .pdf 文件</li>
      </ul>
      <p v-if="info?.defaultApplication" class="integration-note">lw.PDF 已是当前默认 PDF 应用，双击 PDF 文件将使用 lw.PDF 打开。</p>
      <div v-else-if="info?.current" class="path-warning">
        <strong>还差最后一步</strong>
        <span>请在 Windows 设置中将 .pdf 的默认应用选择为 lw.PDF。注册本身不会替换你原来的默认软件。</span>
      </div>
      <p v-else class="integration-note">注册会添加 lw.PDF 的打开方式和右键菜单，但 Windows 要求你在系统设置中亲自确认默认应用。</p>
      <div v-if="info?.registered && !info.current" class="path-warning">
        <strong>检测到程序位置发生变化</strong>
        <span :title="info.registeredExecutablePath || ''">{{ info.registeredExecutablePath || '原注册路径不可用' }}</span>
      </div>
      <p v-if="error" class="dialog-error">{{ error }}</p>
      <div class="dialog-actions">
        <button
          v-if="info?.current"
          :class="{ primary: !info.defaultApplication }"
          :disabled="busy"
          @click="openDefaultApps"
        >{{ info.defaultApplication ? '默认应用设置…' : '设为默认 PDF 应用…' }}</button>
        <span class="dialog-spacer" />
        <button v-if="info?.registered" :disabled="busy" @click="update('unregister')">取消注册</button>
        <button v-if="!info?.current" class="primary" :disabled="!info || busy" @click="update('register')">
          {{ busy ? '正在处理…' : info?.registered ? '修复关联' : '注册 Windows 集成' }}
        </button>
        <button :disabled="busy" @click="emit('close')">完成</button>
      </div>
    </section>
  </div>
</template>

import { reactive } from 'vue'
import type { PdfSource } from '../types/pdf'
import { nativeFileToPdfSource, type NativeFile } from './native'

export interface RecentFile {
  id: string
  name: string
  lastOpened: number
}

interface RecentListResult { files: RecentFile[] }
interface RecentOpenResult { file: NativeFile }

export const recentFilesState = reactive({
  files: [] as RecentFile[],
  loading: false,
})

function validRecentFile(value: unknown): value is RecentFile {
  if (!value || typeof value !== 'object') return false
  const file = value as Partial<RecentFile>
  return typeof file.id === 'string' && /^[0-9a-f]{32}$/.test(file.id) &&
    typeof file.name === 'string' && file.name.length > 0 && file.name.length <= 1024 &&
    typeof file.lastOpened === 'number' && Number.isFinite(file.lastOpened) && file.lastOpened > 0
}

export async function loadRecentFiles(): Promise<void> {
  if (!window.lw) {
    recentFilesState.files = []
    return
  }
  recentFilesState.loading = true
  try {
    const result = await window.lw.invoke<RecentListResult>('recent.list', {})
    recentFilesState.files = Array.isArray(result.files)
      ? result.files.filter(validRecentFile).slice(0, 10)
      : []
  } catch {
    recentFilesState.files = []
  } finally {
    recentFilesState.loading = false
  }
}

export async function confirmRecentFile(grantId?: string): Promise<void> {
  if (!window.lw || !grantId) return
  try {
    await window.lw.invoke('recent.confirmOpen', { grantId })
    await loadRecentFiles()
  } catch {
    // Recent-file persistence is secondary to successfully displaying the PDF.
  }
}

export async function openRecentFile(id: string): Promise<PdfSource | null> {
  if (!window.lw || !/^[0-9a-f]{32}$/.test(id)) return null
  const result = await window.lw.invoke<RecentOpenResult>('recent.open', { id })
  return result.file ? nativeFileToPdfSource(result.file) : null
}

export async function clearRecentFiles(): Promise<void> {
  if (!window.lw) return
  await window.lw.invoke('recent.clear', {})
  recentFilesState.files = []
}

export function formatRecentTime(timestamp: number, now = Date.now()): string {
  const elapsed = Math.max(0, now - timestamp)
  if (elapsed < 60_000) return '刚刚'
  if (elapsed < 3_600_000) return `${Math.floor(elapsed / 60_000)} 分钟前`
  if (elapsed < 86_400_000) return `${Math.floor(elapsed / 3_600_000)} 小时前`
  if (elapsed < 7 * 86_400_000) return `${Math.floor(elapsed / 86_400_000)} 天前`
  return new Intl.DateTimeFormat('zh-CN', { year: 'numeric', month: 'short', day: 'numeric' })
    .format(new Date(timestamp))
}

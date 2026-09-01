import { nativeFileToPdfSource, type NativeFile } from './native'
import type { PdfSource } from '../types/pdf'
import type { PageEditItem } from './pageOrganizer'

interface PageEditSaveResult {
  cancelled: boolean
  file?: NativeFile
  pageNumber?: number
}

export async function savePagePlan(
  sourceGrantId: string, pages: PageEditItem[], focusedSourcePage: number,
): Promise<{ cancelled: true } | { cancelled: false; source: PdfSource; pageNumber: number }> {
  if (!window.lw) throw new Error('页面整理功能当前仅桌面版支持。')
  let result: PageEditSaveResult
  try {
    result = await window.lw.invoke<PageEditSaveResult>('pdf.pageEditSaveAs', {
      sourceGrantId,
      plan: { pages: pages.map(({ sourcePage, rotation }) => ({ sourcePage, rotation })) },
      focusedSourcePage,
    })
  } catch (error: any) {
    const code = error?.code
    const messages: Record<string, string> = {
      PDF_SOURCE_UNAVAILABLE: '当前 PDF 文件权限已失效，请重新打开文件后再试。',
      PDF_PAGE_PLAN_INVALID: '页面整理方案无效，请重新操作。',
      PDF_PAGE_EDIT_NOT_ALLOWED: '当前 PDF 禁止页面整理。',
      PDF_PASSWORD_REQUIRED: '当前版本暂不支持整理受密码保护的 PDF。',
      PDF_OUTPUT_WRITE_FAILED: '无法写入目标文件，请检查磁盘空间或文件权限。',
      PDF_SAME_FILE: '无法覆盖原文件，请选择其他保存位置。',
    }
    throw new Error(messages[code] ?? error?.message ?? '页面整理保存失败。')
  }
  if (result.cancelled) return { cancelled: true }
  if (!result.file?.id || !result.file.name || !result.file.url) {
    throw new Error('页面整理失败，Native 未返回生成的 PDF。')
  }
  return {
    cancelled: false,
    source: nativeFileToPdfSource(result.file),
    pageNumber: Number.isInteger(result.pageNumber) && (result.pageNumber ?? 0) > 0
      ? result.pageNumber!
      : 1,
  }
}

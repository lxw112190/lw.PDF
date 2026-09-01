import type { PdfSource } from '../types/pdf'
import { nativeFileToPdfSource, type NativeFile } from './native'

export type PdfRotationDirection = 'left90' | 'right90' | 'rotate180'

export type PdfPageSelection =
  | { kind: 'all' }
  | { kind: 'single'; page: number }
  | { kind: 'range'; value: string }

export type PdfTransformResult =
  | { cancelled: true }
  | { cancelled: false; source: PdfSource }

interface TransformSaveAsResult { cancelled: boolean; file?: NativeFile }

const errorMessages: Record<string, string> = {
  PDF_SOURCE_UNAVAILABLE: '当前 PDF 文件权限已失效，请重新打开文件后再试。',
  PDF_PAGE_RANGE_INVALID: '页面范围格式不正确，请输入如 1,3,5-8。',
  PDF_PASSWORD_REQUIRED: '当前版本暂不支持整理受密码保护的 PDF。',
  PDF_OUTPUT_WRITE_FAILED: '无法写入目标文件，请检查磁盘空间或文件权限。',
  PDF_UNSAVED_ANNOTATIONS: '当前 PDF 有未保存的批注，请先另存批注后再整理页面。',
}

export function transformErrorMessage(code: string, message?: string): string {
  if (errorMessages[code]) return errorMessages[code]
  return message && message.length > 0
    ? message
    : 'PDF 整理失败，请确认文件未损坏且未受密码保护。'
}

async function invokeTransform(
  sourceGrantId: string,
  operation: Record<string, unknown>,
): Promise<PdfTransformResult> {
  if (typeof window === 'undefined' || !window.lw) throw new Error('页面整理功能当前仅桌面版支持。')
  try {
    const result = await window.lw.invoke<TransformSaveAsResult>('pdf.transformSaveAs', {
      sourceGrantId,
      operation,
    })
    if (result.cancelled) return { cancelled: true }
    const file = result.file
    if (!file?.id || !file.name || !file.url) throw new Error('PDF 整理失败，请确认文件未损坏且未受密码保护。')
    return { cancelled: false, source: nativeFileToPdfSource(file) }
  } catch (error: unknown) {
    const code = typeof (error as { code?: unknown })?.code === 'string'
      ? (error as { code: string }).code
      : ''
    const message = error instanceof Error ? error.message : undefined
    throw new Error(transformErrorMessage(code, message))
  }
}

export function reversePdfPages(sourceGrantId: string): Promise<PdfTransformResult> {
  return invokeTransform(sourceGrantId, { kind: 'reversePages' })
}

export function rotatePdfPages(
  sourceGrantId: string,
  direction: PdfRotationDirection,
  pages: PdfPageSelection,
): Promise<PdfTransformResult> {
  return invokeTransform(sourceGrantId, { kind: 'rotatePages', direction, pages })
}

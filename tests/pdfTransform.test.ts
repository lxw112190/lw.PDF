import { afterEach, describe, expect, it, vi } from 'vitest'
import {
  reversePdfPages,
  rotatePdfPages,
  transformErrorMessage,
  type PdfRotationDirection,
  type PdfPageSelection,
} from '../src/services/pdfTransform'

type Invoke = (method: string, params?: Record<string, unknown>) => Promise<unknown>

function mockNative(responses: Record<string, unknown>): { invoke: ReturnType<typeof vi.fn<Invoke>> } {
  const invoke = vi.fn<Invoke>(async (_method, _params) => {
    const key = JSON.stringify(_params)
    if (key in responses) return responses[key]
    throw new Error(`no mock for ${key}`)
  })
  ;(globalThis as Record<string, unknown>).window = { lw: { invoke } }
  return { invoke }
}

afterEach(() => {
  delete (globalThis as Record<string, unknown>).window
})

function reverseParams() {
  return JSON.stringify({ sourceGrantId: 'grant-1', operation: { kind: 'reversePages' } })
}

describe('pdfTransform', () => {
  it('sends a reversePages request and converts the result to a PdfSource', async () => {
    const file = { id: 'grant-2', name: 'contract_倒序.pdf', size: 12345, mime: 'application/pdf', url: 'https://file.lwpdf/grant-2/document.pdf' }
    mockNative({ [reverseParams()]: { cancelled: false, file } })
    const result = await reversePdfPages('grant-1')
    expect(result).toEqual({
      cancelled: false,
      source: { kind: 'url', name: 'contract_倒序.pdf', url: file.url, grantId: 'grant-2', size: 12345 },
    })
  })

  it('reports a cancelled save dialog without an error', async () => {
    mockNative({ [reverseParams()]: { cancelled: true } })
    await expect(reversePdfPages('grant-1')).resolves.toEqual({ cancelled: true })
  })

  it.each<[PdfRotationDirection, PdfPageSelection, Record<string, unknown>]>([
    ['right90', { kind: 'all' }, { kind: 'rotatePages', direction: 'right90', pages: { kind: 'all' } }],
    ['left90', { kind: 'single', page: 3 }, { kind: 'rotatePages', direction: 'left90', pages: { kind: 'single', page: 3 } }],
    ['rotate180', { kind: 'range', value: '1,3,5-8' }, { kind: 'rotatePages', direction: 'rotate180', pages: { kind: 'range', value: '1,3,5-8' } }],
  ])('sends rotate requests for %s %j', async (_direction, pages, operation) => {
    const { invoke } = mockNative({ [JSON.stringify({ sourceGrantId: 'grant-1', operation })]: { cancelled: true } })
    await rotatePdfPages('grant-1', _direction, pages)
    expect(invoke).toHaveBeenCalledWith('pdf.transformSaveAs', { sourceGrantId: 'grant-1', operation })
  })

  it('maps native error codes to user-facing messages', async () => {
    const { invoke } = mockNative({})
    invoke.mockRejectedValueOnce(Object.assign(new Error('native'), { code: 'PDF_PAGE_RANGE_INVALID' }))
    await expect(rotatePdfPages('grant-1', 'right90', { kind: 'range', value: 'x' }))
      .rejects.toThrow('页面范围格式不正确，请输入如 1,3,5-8。')
  })

  it('keeps a native Chinese message for errors without a mapped code', async () => {
    const { invoke } = mockNative({})
    invoke.mockRejectedValueOnce(Object.assign(new Error('无法覆盖原文件，请选择其他保存位置。'), { code: 'PDF_TRANSFORM_FAILED' }))
    await expect(reversePdfPages('grant-1')).rejects.toThrow('无法覆盖原文件，请选择其他保存位置。')
  })

  it('falls back to a generic message for unknown errors', async () => {
    const { invoke } = mockNative({})
    invoke.mockRejectedValueOnce(Object.assign(new Error(), { code: 'UNKNOWN_CODE' }))
    await expect(reversePdfPages('grant-1')).rejects.toThrow('PDF 整理失败，请确认文件未损坏且未受密码保护。')
  })

  it('reports desktop-only support in browser mode', async () => {
    delete (globalThis as Record<string, unknown>).window
    await expect(reversePdfPages('grant-1')).rejects.toThrow('页面整理功能当前仅桌面版支持。')
  })
})

describe('transformErrorMessage', () => {
  it('maps known codes and prefers the native message otherwise', () => {
    expect(transformErrorMessage('PDF_PASSWORD_REQUIRED')).toBe('当前版本暂不支持整理受密码保护的 PDF。')
    expect(transformErrorMessage('PDF_OUTPUT_WRITE_FAILED')).toContain('磁盘空间')
    expect(transformErrorMessage('X', 'custom')).toBe('custom')
    expect(transformErrorMessage('X')).toContain('PDF 整理失败')
  })
})

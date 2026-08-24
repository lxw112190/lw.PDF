import { describe, expect, it } from 'vitest'
import { isPdfFile } from '../src/services/fileDrop'
import { openBrowserPdf } from '../src/services/native'

describe('PDF drag and drop', () => {
  it('accepts PDF names case-insensitively even when Windows omits the MIME type', () => {
    expect(isPdfFile({ name: '设计说明.PDF', type: '' })).toBe(true)
  })

  it('rejects non-PDF files even if their MIME type is misleading', () => {
    expect(isPdfFile({ name: 'notes.txt', type: 'application/pdf' })).toBe(false)
  })

  it('turns a dropped browser file into PDF.js byte data', async () => {
    const file = {
      name: 'dropped.pdf',
      arrayBuffer: async () => new Uint8Array([0x25, 0x50, 0x44, 0x46]).buffer,
    } as File
    const source = await openBrowserPdf(file)
    expect(source.kind).toBe('data')
    if (source.kind === 'data') expect(Array.from(source.data)).toEqual([0x25, 0x50, 0x44, 0x46])
  })
})

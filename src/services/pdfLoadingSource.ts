import type { PdfSource } from '../types/pdf'

export const nativeRangeChunkSize = 256 * 1024

export function createPdfLoadingSource(source: PdfSource) {
  if (source.kind === 'data') return { data: source.data }
  return {
    url: source.url,
    disableStream: true,
    rangeChunkSize: nativeRangeChunkSize,
  }
}

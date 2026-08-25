import type { PdfSource } from '../types/pdf'

export const nativeRangeChunkSize = 256 * 1024

export function createPdfAssetOptions(origin = typeof window !== 'undefined'
  ? window.location.origin
  : 'https://app.lwpdf') {
  return {
    cMapUrl: new URL('/pdfjs/cmaps/', origin).href,
    cMapPacked: true,
    standardFontDataUrl: new URL('/pdfjs/standard_fonts/', origin).href,
    useSystemFonts: true,
  }
}

export function createPdfLoadingSource(source: PdfSource, origin?: string) {
  const assets = createPdfAssetOptions(origin)
  if (source.kind === 'data') return { data: source.data, ...assets }
  return {
    url: source.url,
    disableStream: true,
    rangeChunkSize: nativeRangeChunkSize,
    ...assets,
  }
}

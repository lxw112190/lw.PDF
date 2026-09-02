import { describe, expect, it } from 'vitest'
import {
  createPdfRenderQualityPolicy,
  formatRenderQualityMessage,
  inspectCanvasQuality,
  renderQualityLogKey,
  shouldReportRenderQuality,
} from '../src/services/renderQuality'

function canvas(width: number, height: number, cssWidth: number, cssHeight: number) {
  return {
    width,
    height,
    getBoundingClientRect: () => ({ width: cssWidth, height: cssHeight }),
  }
}

describe('PDF render quality diagnostics', () => {
  it('keeps the first phase behavior identical to PDF.js defaults', () => {
    expect(createPdfRenderQualityPolicy()).toEqual({
      maxCanvasPixels: undefined,
      enableHWA: false,
    })
  })

  it('reports a one-to-one HiDPI canvas as healthy', () => {
    const sample = inspectCanvasQuality(canvas(2000, 2800, 1000, 1400), 1, 2)!
    expect(sample.qualityRatio).toBe(1)
    expect(sample.restrictedLikely).toBe(false)
    expect(shouldReportRenderQuality(sample)).toBe(false)
  })

  it('detects likely restricted scaling', () => {
    const sample = inspectCanvasQuality(canvas(1500, 2100, 1000, 1400), 2, 2)!
    expect(sample.qualityRatio).toBe(0.75)
    expect(sample.restrictedLikely).toBe(true)
    expect(shouldReportRenderQuality(sample)).toBe(true)
  })

  it('does not warn when the backing canvas is larger than required', () => {
    const sample = inspectCanvasQuality(canvas(3000, 4200, 1000, 1400), 3, 2)!
    expect(sample.qualityRatio).toBe(1.5)
    expect(shouldReportRenderQuality(sample)).toBe(false)
  })

  it('rejects invisible or invalid canvases', () => {
    expect(inspectCanvasQuality(canvas(0, 10, 100, 100), 1, 2)).toBeNull()
    expect(inspectCanvasQuality(canvas(100, 100, 0, 100), 1, 2)).toBeNull()
  })

  it('deduplicates by document generation, page, zoom and DPR', () => {
    const sample = inspectCanvasQuality(canvas(1500, 2100, 1000, 1400), 8, 2)!
    expect(renderQualityLogKey(4, sample, 3)).toBe('4:8:300:200')
    expect(formatRenderQualityMessage(sample, 3, false)).toContain('ratio=0.75')
  })
})

export const QUALITY_WARNING_RATIO = 0.92

export interface PdfRenderQualityPolicy {
  /** Leave undefined during the diagnostic phase to preserve PDF.js defaults. */
  maxCanvasPixels: number | undefined
  enableHWA: boolean
}

export interface CanvasQualitySample {
  pageNumber: number
  cssWidth: number
  cssHeight: number
  canvasWidth: number
  canvasHeight: number
  devicePixelRatio: number
  scaleX: number
  scaleY: number
  qualityRatio: number
  restrictedLikely: boolean
}

interface CanvasQualityTarget {
  width: number
  height: number
  getBoundingClientRect(): { width: number; height: number }
}

export function createPdfRenderQualityPolicy(): PdfRenderQualityPolicy {
  return {
    // Phase 1 is diagnostics-only. The rendering behavior remains exactly
    // PDF.js 4.10.38's default until real-world measurements justify a cap.
    maxCanvasPixels: undefined,
    enableHWA: false,
  }
}

export function inspectCanvasQuality(
  canvas: CanvasQualityTarget,
  pageNumber: number,
  devicePixelRatio = typeof window === 'undefined' ? 1 : window.devicePixelRatio,
): CanvasQualitySample | null {
  const rect = canvas.getBoundingClientRect()
  if (!Number.isFinite(rect.width) || !Number.isFinite(rect.height) ||
      rect.width <= 0 || rect.height <= 0 ||
      !Number.isFinite(canvas.width) || !Number.isFinite(canvas.height) ||
      canvas.width <= 0 || canvas.height <= 0) {
    return null
  }

  const dpr = Number.isFinite(devicePixelRatio) && devicePixelRatio > 0
    ? devicePixelRatio
    : 1
  const expectedWidth = rect.width * dpr
  const expectedHeight = rect.height * dpr
  const scaleX = canvas.width / expectedWidth
  const scaleY = canvas.height / expectedHeight
  const qualityRatio = Math.min(scaleX, scaleY)

  return {
    pageNumber,
    cssWidth: rect.width,
    cssHeight: rect.height,
    canvasWidth: canvas.width,
    canvasHeight: canvas.height,
    devicePixelRatio: dpr,
    scaleX,
    scaleY,
    qualityRatio,
    restrictedLikely: qualityRatio < QUALITY_WARNING_RATIO,
  }
}

export function shouldReportRenderQuality(
  sample: CanvasQualitySample,
  detailed = false,
): boolean {
  return detailed || sample.restrictedLikely
}

export function renderQualityLogKey(
  generation: number,
  sample: CanvasQualitySample,
  scale: number,
): string {
  return [
    generation,
    sample.pageNumber,
    Math.round(scale * 100),
    Math.round(sample.devicePixelRatio * 100),
  ].join(':')
}

export function formatRenderQualityMessage(
  sample: CanvasQualitySample,
  scale: number,
  eyeCareEnabled: boolean,
): string {
  const pixels = (sample.canvasWidth * sample.canvasHeight) / (1024 * 1024)
  return [
    `page=${sample.pageNumber}`,
    `zoom=${Math.round(scale * 100)}%`,
    `dpr=${sample.devicePixelRatio.toFixed(2)}`,
    `css=${Math.round(sample.cssWidth)}x${Math.round(sample.cssHeight)}`,
    `canvas=${sample.canvasWidth}x${sample.canvasHeight}`,
    `ratio=${sample.qualityRatio.toFixed(2)}`,
    `pixels=${pixels.toFixed(1)}MiP`,
    `eyeCare=${eyeCareEnabled}`,
  ].join(' ')
}

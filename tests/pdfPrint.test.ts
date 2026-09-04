import { describe, expect, it } from 'vitest'
import {
  createPrintPageStyle,
  hasEqualPageSizes,
  LOW_QUALITY_PRINT_DPI,
  PDF_POINTS_PER_INCH,
  printPixels,
  printSafeMargin,
  PRINT_DPI,
  PRINT_SAFE_MARGIN_PT,
  PrintCancelledError,
} from '../src/services/pdfPrint'

const a4 = { width: 595, height: 842, rotation: 0 }

describe('PDF print planning', () => {
  it('converts PDF points to print pixels at the selected DPI', () => {
    expect(printPixels(595, PRINT_DPI)).toBe(1239)
    expect(printPixels(842, PRINT_DPI)).toBe(1754)
    expect(printPixels(595, LOW_QUALITY_PRINT_DPI)).toBe(793)
    expect(PDF_POINTS_PER_INCH).toBe(72)
  })

  it('detects equal and mixed page sizes', () => {
    expect(hasEqualPageSizes([a4, { ...a4, rotation: 180 }])).toBe(true)
    expect(hasEqualPageSizes([a4, { width: 842, height: 1191, rotation: 0 }])).toBe(false)
  })

  it('creates a zero-margin first-page print rule', () => {
    expect(createPrintPageStyle(a4)).toBe('@page { size: 595pt 842pt; margin: 0; }')
  })

  it('reserves a printer-safe inset without overwhelming small pages', () => {
    expect(printSafeMargin(a4)).toBe(PRINT_SAFE_MARGIN_PT)
    expect(printSafeMargin({ width: 200, height: 400, rotation: 0 })).toBe(10)
  })

  it('exposes a distinct cancellation error', () => {
    expect(new PrintCancelledError()).toBeInstanceOf(Error)
    expect(new PrintCancelledError().name).toBe('PrintCancelledError')
  })
})

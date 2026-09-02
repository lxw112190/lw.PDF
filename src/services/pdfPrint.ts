import * as pdfjsLib from 'pdfjs-dist'
import type { PDFDocumentProxy, PDFPageProxy } from 'pdfjs-dist'

export const PRINT_DPI = 150
export const LOW_QUALITY_PRINT_DPI = 96
export const PDF_POINTS_PER_INCH = 72

export interface PdfPrintPageOverview {
  width: number
  height: number
  rotation: number
}

export interface PdfPrintProgress {
  current: number
  total: number
}

export interface PdfPrintOptions {
  dpi?: number
  onProgress?: (progress: PdfPrintProgress) => void
  onDialog?: () => void
}

export class PrintCancelledError extends Error {
  constructor() {
    super('PDF printing was cancelled')
    this.name = 'PrintCancelledError'
  }
}

export function printPixels(points: number, dpi: number): number {
  return Math.max(1, Math.floor(points * dpi / PDF_POINTS_PER_INCH))
}

export function hasEqualPageSizes(pages: PdfPrintPageOverview[]): boolean {
  if (pages.length < 2) return true
  const first = pages[0]
  return pages.slice(1).every(page =>
    page.width === first.width && page.height === first.height)
}

export function createPrintPageStyle(page: PdfPrintPageOverview): string {
  return `@page { size: ${page.width}pt ${page.height}pt; margin: 0; }`
}

function waitForImage(image: HTMLImageElement): Promise<void> {
  if (image.complete) {
    return image.naturalWidth > 0
      ? Promise.resolve()
      : Promise.reject(new Error('Printed page image failed to load'))
  }
  return new Promise((resolve, reject) => {
    image.addEventListener('load', () => resolve(), { once: true })
    image.addEventListener('error', () => reject(new Error('Printed page image failed to load')), { once: true })
  })
}

function canvasToBlob(canvas: HTMLCanvasElement): Promise<Blob> {
  return new Promise((resolve, reject) => {
    canvas.toBlob(blob => blob ? resolve(blob) : reject(new Error('Printed page image could not be encoded')), 'image/png')
  })
}

function waitForLayout(): Promise<void> {
  return new Promise(resolve => {
    requestAnimationFrame(() => requestAnimationFrame(() => setTimeout(resolve, 0)))
  })
}

export class PdfPrintService {
  private container: HTMLDivElement | null = null
  private pageStyle: HTMLStyleElement | null = null
  private renderTask: { cancel: () => void; promise: Promise<unknown> } | null = null
  private objectUrls: string[] = []
  private cancelled = false

  get active(): boolean {
    return this.container !== null
  }

  async print(
    pdfDocument: PDFDocumentProxy,
    pagesOverview: PdfPrintPageOverview[],
    options: PdfPrintOptions = {},
  ): Promise<void> {
    if (this.active) throw new Error('A PDF print operation is already active')
    if (!pagesOverview.length) throw new Error('PDF has no printable pages')
    this.cancelled = false
    const dpi = options.dpi && Number.isFinite(options.dpi) && options.dpi > 0
      ? options.dpi : PRINT_DPI
    const printAnnotationStorage = pdfDocument.annotationStorage.print
    const optionalContentConfigPromise = pdfDocument.getOptionalContentConfig({ intent: 'print' })
    const scratchCanvas = document.createElement('canvas')
    const context = scratchCanvas.getContext('2d')
    if (!context) throw new Error('Unable to create print canvas')

    this.container = document.createElement('div')
    this.container.id = 'lwpdf-print-container'
    document.body.append(this.container)
    this.pageStyle = document.createElement('style')
    this.pageStyle.id = 'lwpdf-print-page-style'
    this.pageStyle.textContent = createPrintPageStyle(pagesOverview[0])
    document.head.append(this.pageStyle)

    try {
      for (let index = 0; index < pagesOverview.length; index += 1) {
        this.throwIfCancelled()
        const page = await pdfDocument.getPage(index + 1)
        await this.renderPage(
          page,
          pagesOverview[index],
          dpi,
          scratchCanvas,
          context,
          optionalContentConfigPromise,
          printAnnotationStorage,
        )
        const blob = await canvasToBlob(scratchCanvas)
        this.throwIfCancelled()
        const url = URL.createObjectURL(blob)
        this.objectUrls.push(url)
        const image = document.createElement('img')
        image.src = url
        image.alt = `PDF 第 ${index + 1} 页`
        const printedPage = document.createElement('div')
        printedPage.className = 'printed-page'
        printedPage.style.width = `${pagesOverview[index].width}pt`
        printedPage.style.height = `${pagesOverview[index].height}pt`
        printedPage.append(image)
        this.container.append(printedPage)
        await waitForImage(image)
        options.onProgress?.({ current: index + 1, total: pagesOverview.length })
      }
      this.throwIfCancelled()
      document.body.classList.add('lwpdf-printing')
      options.onDialog?.()
      await waitForLayout()
      await this.performPrint()
    } finally {
      scratchCanvas.width = 0
      scratchCanvas.height = 0
      this.cleanup()
    }
  }

  cancel(): void {
    this.cancelled = true
    this.renderTask?.cancel()
  }

  private async renderPage(
    page: PDFPageProxy,
    overview: PdfPrintPageOverview,
    dpi: number,
    canvas: HTMLCanvasElement,
    context: CanvasRenderingContext2D,
    optionalContentConfigPromise: ReturnType<PDFDocumentProxy['getOptionalContentConfig']>,
    printAnnotationStorage: any,
  ): Promise<void> {
    const units = dpi / PDF_POINTS_PER_INCH
    const viewport = page.getViewport({ scale: 1, rotation: overview.rotation })
    canvas.width = printPixels(viewport.width, dpi)
    canvas.height = printPixels(viewport.height, dpi)
    context.save()
    context.fillStyle = '#fff'
    context.fillRect(0, 0, canvas.width, canvas.height)
    context.restore()
    this.renderTask = page.render({
      canvasContext: context,
      viewport,
      transform: [units, 0, 0, units, 0, 0],
      intent: 'print',
      annotationMode: pdfjsLib.AnnotationMode.ENABLE_STORAGE,
      optionalContentConfigPromise,
      printAnnotationStorage,
      background: '#fff',
    })
    try {
      await this.renderTask.promise
    } catch (error) {
      if (this.cancelled || error instanceof pdfjsLib.RenderingCancelledException) {
        throw new PrintCancelledError()
      }
      throw error
    } finally {
      this.renderTask = null
    }
  }

  private async performPrint(): Promise<void> {
    await new Promise<void>((resolve, reject) => {
      let finished = false
      const finish = () => {
        if (finished) return
        finished = true
        window.removeEventListener('afterprint', finish)
        mediaQuery?.removeEventListener?.('change', onMediaChange)
        resolve()
      }
      const onMediaChange = (event: MediaQueryListEvent) => {
        if (!event.matches) finish()
      }
      const mediaQuery = typeof window.matchMedia === 'function' ? window.matchMedia('print') : null
      window.addEventListener('afterprint', finish, { once: true })
      mediaQuery?.addEventListener?.('change', onMediaChange)
      setTimeout(() => {
        try {
          window.print()
        } catch (error) {
          window.removeEventListener('afterprint', finish)
          mediaQuery?.removeEventListener?.('change', onMediaChange)
          reject(error)
        }
      }, 0)
    })
  }

  private throwIfCancelled(): void {
    if (this.cancelled) throw new PrintCancelledError()
  }

  private cleanup(): void {
    document.body.classList.remove('lwpdf-printing')
    this.container?.remove()
    this.pageStyle?.remove()
    for (const url of this.objectUrls) URL.revokeObjectURL(url)
    this.objectUrls = []
    this.container = null
    this.pageStyle = null
    this.renderTask = null
    this.cancelled = false
  }
}

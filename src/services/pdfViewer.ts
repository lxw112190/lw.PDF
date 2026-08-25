import * as pdfjsLib from 'pdfjs-dist'
import { markRaw } from 'vue'
import type { PDFDocumentLoadingTask } from 'pdfjs-dist'
import { EventBus, PDFFindController, PDFLinkService, PDFViewer } from 'pdfjs-dist/web/pdf_viewer.mjs'
import workerUrl from 'pdfjs-dist/build/pdf.worker.min.mjs?url'
import { viewerState } from '../stores/viewerState'
import { revokeDesktopFile, setCurrentGrant } from './native'
import { createPdfLoadingSource } from './pdfLoadingSource'
import { ReadingPositionStore, normalizeReadingPosition, type ReadingPosition } from './readingPosition'
import { confirmRecentFile } from './recentFiles'
import { createFindCommand } from './search'
import type { PdfSource } from '../types/pdf'

pdfjsLib.GlobalWorkerOptions.workerSrc = workerUrl

const zoomLevels = [0.5, .67, .75, .8, .9, 1, 1.1, 1.25, 1.5, 1.75, 2, 2.5, 3]

interface ViewAreaEvent {
  location?: {
    pageNumber?: unknown
    scale?: unknown
    left?: unknown
    top?: unknown
  }
}

export class PdfViewerController {
  private viewer!: PDFViewer
  private linkService!: PDFLinkService
  private findController!: PDFFindController
  private eventBus!: EventBus
  private loadingTask: PDFDocumentLoadingTask | null = null
  private source: PdfSource | null = null
  private generation = 0
  private readonly positions = new ReadingPositionStore()
  private fingerprint: string | null = null
  private pendingRestore: { generation: number; position: ReadingPosition | null } | null = null
  private pendingSave: { fingerprint: string; position: ReadingPosition } | null = null
  private saveTimer: number | null = null
  private suppressPositionSave = true

  init(container: HTMLDivElement, element: HTMLDivElement) {
    const eventBus = this.eventBus = new EventBus()
    this.linkService = new PDFLinkService({ eventBus })
    this.findController = new PDFFindController({ linkService: this.linkService, eventBus })
    this.viewer = new PDFViewer({
      container,
      viewer: element,
      eventBus,
      linkService: this.linkService,
      findController: this.findController,
      textLayerMode: 1,
    })
    this.linkService.setViewer(this.viewer)
    eventBus.on('pagesinit', () => this.restoreReadingPosition())
    eventBus.on('updateviewarea', (event: ViewAreaEvent) => this.captureReadingPosition(event))
    eventBus.on('pagechanging', (event: { pageNumber: number }) => {
      viewerState.pageNumber = event.pageNumber
    })
    eventBus.on('scalechanging', (event: { scale: number }) => {
      if (typeof event.scale === 'number') viewerState.scale = event.scale
    })
    eventBus.on('updatefindmatchescount', (event: any) => {
      viewerState.searchCurrent = event.matchesCount.current
      viewerState.searchTotal = event.matchesCount.total
    })
  }

  async open(source: PdfSource) {
    await this.close()
    const id = ++this.generation
    viewerState.loading = true
    viewerState.error = null
    try {
      this.source = source
      setCurrentGrant(source.kind === 'url' ? source.grantId : undefined)
      const task = this.loadingTask = pdfjsLib.getDocument(createPdfLoadingSource(source))
      task.onPassword = (update: (password: string) => void) => {
        const password = window.prompt('请输入 PDF 密码')
        if (password !== null) update(password)
        else void task.destroy()
      }
      const pdf = await task.promise
      if (id !== this.generation) {
        await pdf.destroy()
        return
      }

      this.fingerprint = pdf.fingerprints.find(value => !!value) ?? null
      this.pendingRestore = {
        generation: id,
        position: this.fingerprint ? this.positions.get(this.fingerprint) : null,
      }
      this.suppressPositionSave = true
      this.viewer.setDocument(pdf)
      this.linkService.setDocument(pdf)
      this.findController.setDocument(pdf)
      viewerState.document = markRaw(pdf)
      viewerState.documentName = source.name
      viewerState.pageCount = pdf.numPages
      viewerState.pageNumber = 1

      await confirmRecentFile(source.kind === 'url' ? source.grantId : undefined)
      void window.lw?.invoke('diagnostics.info', {
        area: 'pdf.open',
        message: `pages=${pdf.numPages}`,
      }).catch(() => {})
      try {
        viewerState.outline = (await pdf.getOutline() ?? []) as any
      } catch {
        viewerState.outline = []
      }
    } catch (error: any) {
      console.error('PDF load failed', error)
      if (id === this.generation && error?.name !== 'AbortException') {
        const message = typeof error?.message === 'string'
          ? error.message
          : String(error?.name || 'Unknown PDF error')
        void window.lw?.invoke('diagnostics.error', { area: 'pdf.open', message }).catch(() => {})
        const reason = typeof error?.message === 'string' ? `（${error.message}）` : ''
        viewerState.error = error?.name === 'PasswordException'
          ? '需要正确的 PDF 密码。'
          : `无法读取该 PDF，文件可能已损坏或授权已失效。${reason}`
      }
    } finally {
      if (id === this.generation) viewerState.loading = false
    }
  }

  async close() {
    this.flushReadingPosition()
    this.generation++
    this.pendingRestore = null
    this.fingerprint = null
    this.suppressPositionSave = true
    ;(this.viewer as any)?.setDocument(null)
    ;(this.linkService as any)?.setDocument(null)
    ;(this.findController as any)?.setDocument(null)
    if (this.loadingTask) {
      try {
        await this.loadingTask.destroy()
      } catch {
      }
      this.loadingTask = null
    }
    if (this.source?.kind === 'url') await revokeDesktopFile(this.source.grantId)
    this.source = null
    viewerState.document = null
    viewerState.documentName = ''
    viewerState.pageCount = 0
    viewerState.outline = []
    viewerState.searchQuery = ''
    viewerState.searchCurrent = 0
    viewerState.searchTotal = 0
  }

  flushReadingPosition() {
    if (this.saveTimer !== null) {
      window.clearTimeout(this.saveTimer)
      this.saveTimer = null
    }
    if (!this.pendingSave) return
    this.positions.save(this.pendingSave.fingerprint, this.pendingSave.position)
    this.pendingSave = null
  }

  setPage(page: number) {
    if (this.viewer) {
      this.viewer.currentPageNumber = Math.min(
        Math.max(Math.round(page) || 1, 1),
        viewerState.pageCount || 1,
      )
    }
  }

  nextPage() { this.setPage(viewerState.pageNumber + 1) }
  previousPage() { this.setPage(viewerState.pageNumber - 1) }
  setScale(scale: number | string) {
    if (this.viewer) this.viewer.currentScaleValue = String(scale)
  }
  zoomIn() { this.setScale(zoomLevels.find(value => value > viewerState.scale + .001) ?? 3) }
  zoomOut() { this.setScale([...zoomLevels].reverse().find(value => value < viewerState.scale - .001) ?? .5) }
  fitWidth() { this.setScale('page-width') }
  fitPage() { this.setScale('page-fit') }
  find(query: string) { this.dispatchFind(query, '') }
  findNext(query: string) { this.dispatchFind(query, 'again') }
  findPrevious(query: string) { this.dispatchFind(query, 'again', true) }

  private restoreReadingPosition() {
    const pending = this.pendingRestore
    this.pendingRestore = null
    if (!pending || pending.generation !== this.generation) return
    const position = pending.position
    if (position) {
      const pageNumber = Math.min(position.pageNumber, viewerState.pageCount || 1)
      this.viewer.currentScaleValue = String(position.scale)
      this.viewer.scrollPageIntoView({
        pageNumber,
        destArray: [null, { name: 'XYZ' }, position.left, position.top, null],
        ignoreDestinationZoom: true,
      } as any)
    } else {
      this.viewer.currentScaleValue = 'page-width'
    }
    const generation = this.generation
    window.setTimeout(() => {
      if (generation === this.generation) this.suppressPositionSave = false
    }, 0)
  }

  private captureReadingPosition(event: ViewAreaEvent) {
    if (this.suppressPositionSave || !this.fingerprint || !event.location) return
    const position = normalizeReadingPosition({
      pageNumber: event.location.pageNumber,
      scale: event.location.scale,
      left: event.location.left,
      top: event.location.top,
      updatedAt: Date.now(),
    })
    if (!position) return
    this.pendingSave = { fingerprint: this.fingerprint, position }
    if (this.saveTimer !== null) window.clearTimeout(this.saveTimer)
    this.saveTimer = window.setTimeout(() => this.flushReadingPosition(), 500)
  }

  private dispatchFind(query: string, type: '' | 'again', findPrevious = false) {
    viewerState.searchQuery = query
    this.eventBus.dispatch('find', {
      source: this,
      ...createFindCommand(query, type, findPrevious),
    } as any)
  }

  goToDestination(destination: string | unknown[]) {
    this.linkService.goToDestination(destination)
  }
}

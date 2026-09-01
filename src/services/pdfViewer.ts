import * as pdfjsLib from 'pdfjs-dist'
import { markRaw } from 'vue'
import type { PDFDocumentLoadingTask } from 'pdfjs-dist'
import { EventBus, PDFFindController, PDFLinkService, PDFViewer } from 'pdfjs-dist/web/pdf_viewer.mjs'
import workerUrl from 'pdfjs-dist/build/pdf.worker.min.mjs?url'
import { viewerState } from '../stores/viewerState'
import { pageOrganizerState } from '../stores/pageOrganizerState'
import { nativeFileToPdfSource, revokeDesktopFile, setCurrentGrant, type NativeFile } from './native'
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
  private annotationUiManager: any = null
  private annotationSaving = false
  private latestPosition: ReadingPosition | null = null

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
      annotationEditorMode: pdfjsLib.AnnotationEditorType.NONE,
      annotationEditorHighlightColors: '#fff176, #ffb74d, #81d4fa, #ce93d8',
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
    eventBus.on('annotationeditoruimanager', (event: { uiManager?: any }) => {
      this.annotationUiManager = event.uiManager ?? null
    })
    eventBus.on('annotationeditormodechanged', (event: { mode?: number | { mode?: number } }) => {
      const mode = typeof event.mode === 'number' ? event.mode : event.mode?.mode
      if (typeof mode === 'number') viewerState.annotationMode = mode
    })
    eventBus.on('annotationeditorstateschanged', (event: { details?: Record<string, unknown> }) => {
      const details = event.details ?? {}
      viewerState.annotationCanUndo = details.hasSomethingToUndo === true
      viewerState.annotationCanRedo = details.hasSomethingToRedo === true
      viewerState.annotationHasSelection = details.hasSelectedEditor === true
    })
  }

  async open(source: PdfSource, positionOverride: ReadingPosition | null = null, bypassDirtyGuard = false) {
    if (!bypassDirtyGuard && !(await this.confirmBeforeReplace())) {
      if (source.kind === 'url' && source.grantId) await revokeDesktopFile(source.grantId)
      return false
    }
    await this.close()
    const id = ++this.generation
    viewerState.loading = true
    viewerState.error = null
    try {
      this.source = source
      setCurrentGrant(source.kind === 'url' ? source.grantId : undefined)
      viewerState.currentGrantId = source.kind === 'url' ? source.grantId : undefined
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
        position: positionOverride ?? (this.fingerprint ? this.positions.get(this.fingerprint) : null),
      }
      this.suppressPositionSave = true
      this.viewer.setDocument(pdf)
      this.linkService.setDocument(pdf)
      this.findController.setDocument(pdf)
      viewerState.document = markRaw(pdf)
      viewerState.documentName = source.name
      viewerState.pageCount = pdf.numPages
      viewerState.pageNumber = 1
      let permissions: number[] | null = null
      try {
        permissions = await pdf.getPermissions()
      } catch {
        permissions = null
      }
      // PDF.js disables annotation editors without MODIFY_CONTENTS, while
      // the PDF permission bit for annotations is MODIFY_ANNOTATIONS. Require
      // both when a permission list is present so the toolbar never advertises
      // an operation that the document does not authorize.
      const canEditAnnotations = !permissions || (
        permissions.includes(pdfjsLib.PermissionFlag.MODIFY_CONTENTS) &&
        permissions.includes(pdfjsLib.PermissionFlag.MODIFY_ANNOTATIONS)
      )
      viewerState.annotationEditable = !pdf.isPureXfa && canEditAnnotations
      viewerState.pageEditAllowed = !permissions ||
        permissions.includes(pdfjsLib.PermissionFlag.ASSEMBLE)
      this.annotationUiManager = null
      const storage = pdf.annotationStorage
      storage.onSetModified = () => this.setAnnotationDirty(true)
      storage.onResetModified = () => {
        if (!this.annotationSaving) this.setAnnotationDirty(false)
      }
      this.setAnnotationDirty(false)

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
      if (id === this.generation && source.kind === 'url' && source.grantId) {
        await revokeDesktopFile(source.grantId)
      }
    } finally {
      if (id === this.generation) viewerState.loading = false
    }
    return id === this.generation
  }

  async close() {
    this.flushReadingPosition()
    this.generation++
    this.pendingRestore = null
    this.latestPosition = null
    this.annotationUiManager = null
    this.annotationSaving = false
    viewerState.annotationDirty = false
    viewerState.annotationSaving = false
    viewerState.annotationToolbarVisible = false
    viewerState.annotationMode = pdfjsLib.AnnotationEditorType.NONE
    viewerState.annotationEditable = true
    viewerState.pageEditAllowed = true
    viewerState.annotationCanUndo = false
    viewerState.annotationCanRedo = false
    viewerState.annotationHasSelection = false
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
    viewerState.currentGrantId = undefined
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
  setAnnotationMode(mode: number) {
    if (!this.viewer || !viewerState.pageCount || !viewerState.annotationEditable) return
    try {
      this.viewer.annotationEditorMode = { mode }
      viewerState.annotationMode = mode
    } catch (error) {
      console.error('Annotation mode change failed', error)
    }
  }
  undoAnnotation() { if (this.annotationUiManager?.undo) this.annotationUiManager.undo(); else this.eventBus.dispatch('editingaction', { source: this, name: 'undo' }); }
  redoAnnotation() { if (this.annotationUiManager?.redo) this.annotationUiManager.redo(); else this.eventBus.dispatch('editingaction', { source: this, name: 'redo' }); }
  deleteAnnotation() { if (this.annotationUiManager?.delete) this.annotationUiManager.delete(); else this.eventBus.dispatch('editingaction', { source: this, name: 'delete' }); }
  async confirmBeforeReplace() {
    if (!viewerState.annotationDirty && !viewerState.annotationSaving &&
        !pageOrganizerState.dirty && !pageOrganizerState.saving) return true
    return window.confirm('当前 PDF 有未保存的修改，确定要打开其他文件吗？\n\n未保存的修改将丢失。')
  }
  async saveAnnotations() {
    const pdf = viewerState.document
    const source = this.source
    if (!pdf || !source || !viewerState.annotationEditable || !viewerState.annotationDirty || this.annotationSaving) return false
    this.annotationSaving = true
    viewerState.annotationSaving = true
    this.setAnnotationDirty(true)
    const position = this.latestPosition
    let token: string | undefined
    try {
      let bytes: Uint8Array
      if (source.kind === 'url' && source.grantId && window.lw) {
        const grant = await window.lw.invoke<{ cancelled: boolean; token?: string; url?: string }>('pdf.annotationSaveGrant', { sourceGrantId: source.grantId })
        if (grant.cancelled) return false
        if (!grant.token || !grant.url) throw new Error('无法创建保存权限。')
        token = grant.token
        bytes = await pdf.saveDocument()
        const response = await fetch(grant.url, {
          method: 'PUT',
          headers: { 'Content-Type': 'application/pdf' },
          // WebView2 accepts Uint8Array request bodies; this assertion keeps
          // the runtime value intact and avoids a second full-PDF allocation.
          body: bytes as unknown as BodyInit,
        })
        if (!response.ok) {
          const detail = await response.json().catch(() => null) as { error?: string } | null
          throw new Error(detail?.error === 'output_commit_failed'
            ? '无法写入目标文件，请检查磁盘空间或文件权限。'
            : 'Native 保存失败。')
        }
        const result = await response.json() as { file?: NativeFile }
        if (!result.file?.id || !result.file.url || !result.file.name) throw new Error('Native 未返回保存后的 PDF。')
        await this.open(nativeFileToPdfSource(result.file), position, true)
        return true
      }
      bytes = await pdf.saveDocument()
      const blob = new Blob([bytes as unknown as BlobPart], { type: 'application/pdf' })
      const url = URL.createObjectURL(blob)
      const anchor = document.createElement('a')
      anchor.href = url
      anchor.download = source.name.replace(/\.pdf$/i, '') + '_批注.pdf'
      anchor.click()
      URL.revokeObjectURL(url)
      this.setAnnotationDirty(false)
      return true
    } catch (error) {
      console.error('Annotation save failed', error)
      viewerState.error = error instanceof Error ? error.message : '批注保存失败。'
      this.setAnnotationDirty(true)
      return false
    } finally {
      if (token) void window.lw?.invoke('pdf.annotationRevokeSaveGrant', { token }).catch(() => {})
      this.annotationSaving = false
      viewerState.annotationSaving = false
    }
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
    this.latestPosition = position
    this.pendingSave = { fingerprint: this.fingerprint, position }
    if (this.saveTimer !== null) window.clearTimeout(this.saveTimer)
    this.saveTimer = window.setTimeout(() => this.flushReadingPosition(), 500)
  }

  private setAnnotationDirty(dirty: boolean) {
    viewerState.annotationDirty = dirty
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

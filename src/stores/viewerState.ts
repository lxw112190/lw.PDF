import { reactive } from 'vue'
import type { PDFDocumentProxy } from 'pdfjs-dist'
import type { OutlineItem } from '../types/pdf'
export const viewerState = reactive({ documentName: '', pageNumber: 1, pageCount: 0, scale: 1, sidebarVisible: true, sidebarMode: 'thumbnail' as 'thumbnail' | 'outline', loading: false, error: null as string | null, searchVisible: false, searchQuery: '', searchCurrent: 0, searchTotal: 0, outline: [] as OutlineItem[], document: null as PDFDocumentProxy | null, currentGrantId: undefined as string | undefined, transforming: false, annotationToolbarVisible: false, annotationMode: 0, annotationEditable: true, pageEditAllowed: true, annotationDirty: false, annotationSaving: false, annotationCanUndo: false, annotationCanRedo: false, annotationHasSelection: false })

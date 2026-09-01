import { reactive } from 'vue'
import {
  createInitialPagePlan,
  isPlanDirty,
  PageOrganizerHistory,
  type PageEditItem,
} from '../services/pageOrganizer'

export const pageOrganizerState = reactive({
  active: false,
  pages: [] as PageEditItem[],
  selectedIds: [] as string[],
  anchorId: null as string | null,
  focusedId: null as string | null,
  dirty: false,
  saving: false,
  canUndo: false,
  canRedo: false,
  dragIds: [] as string[],
  dropTargetId: null as string | null,
  dropBefore: true,
  editable: true,
})

export const pageOrganizerHistory = new PageOrganizerHistory()

export function resetPageOrganizer(pageCount: number, focusedPage = 1, editable = true): void {
  pageOrganizerState.active = true
  pageOrganizerState.pages = createInitialPagePlan(pageCount)
  const focused = pageOrganizerState.pages[Math.max(0, Math.min(pageCount - 1, focusedPage - 1))]
  pageOrganizerState.selectedIds = focused ? [focused.id] : []
  pageOrganizerState.anchorId = focused?.id ?? null
  pageOrganizerState.focusedId = focused?.id ?? null
  pageOrganizerState.dirty = false
  pageOrganizerState.saving = false
  pageOrganizerState.dragIds = []
  pageOrganizerState.dropTargetId = null
  pageOrganizerState.dropBefore = true
  pageOrganizerState.editable = editable
  pageOrganizerHistory.clear()
  syncHistoryState()
}

export function updatePageOrganizerDirty(): void {
  pageOrganizerState.dirty = isPlanDirty(pageOrganizerState.pages)
}

export function syncHistoryState(): void {
  pageOrganizerState.canUndo = pageOrganizerHistory.canUndo
  pageOrganizerState.canRedo = pageOrganizerHistory.canRedo
}

export function closePageOrganizer(): void {
  pageOrganizerState.active = false
  pageOrganizerState.pages = []
  pageOrganizerState.selectedIds = []
  pageOrganizerState.anchorId = null
  pageOrganizerState.focusedId = null
  pageOrganizerState.dirty = false
  pageOrganizerState.saving = false
  pageOrganizerState.dragIds = []
  pageOrganizerState.dropTargetId = null
  pageOrganizerHistory.clear()
  syncHistoryState()
}

export type PageRotation = 0 | 90 | 180 | 270

export interface PageEditItem {
  id: string
  sourcePage: number
  rotation: PageRotation
}

export interface PagePlanSnapshot {
  pages: PageEditItem[]
}

export function createInitialPagePlan(pageCount: number): PageEditItem[] {
  return Array.from({ length: Math.max(0, Math.floor(pageCount)) }, (_, index) => ({
    id: `source-${index + 1}`,
    sourcePage: index + 1,
    rotation: 0 as PageRotation,
  }))
}

export function clonePagePlan(pages: PageEditItem[]): PageEditItem[] {
  return pages.map(item => ({ ...item }))
}

export function snapshotPagePlan(pages: PageEditItem[]): PagePlanSnapshot {
  return { pages: clonePagePlan(pages) }
}

export function normalizeRotation(value: number): PageRotation {
  return (((value % 360) + 360) % 360) as PageRotation
}

export function isPlanDirty(pages: PageEditItem[]): boolean {
  return pages.some((item, index) => item.sourcePage !== index + 1 || item.rotation !== 0)
}

export function reversePagePlan(pages: PageEditItem[]): PageEditItem[] {
  return [...pages].reverse()
}

export function rotatePagePlan(
  pages: PageEditItem[], selectedIds: string[], delta: 90 | -90 | 180,
): PageEditItem[] {
  const selected = new Set(selectedIds)
  return pages.map(item => selected.has(item.id)
    ? { ...item, rotation: normalizeRotation(item.rotation + delta) }
    : { ...item })
}

export function selectPageRange(
  pages: PageEditItem[], anchorId: string, targetId: string,
): string[] {
  const anchor = pages.findIndex(item => item.id === anchorId)
  const target = pages.findIndex(item => item.id === targetId)
  if (anchor < 0 || target < 0) return target >= 0 ? [pages[target].id] : []
  const start = Math.min(anchor, target)
  const end = Math.max(anchor, target)
  return pages.slice(start, end + 1).map(item => item.id)
}

export function movePageGroup(
  pages: PageEditItem[], ids: string[], targetId: string, before: boolean,
): PageEditItem[] {
  const moving = new Set(ids)
  const group = pages.filter(item => moving.has(item.id))
  if (!group.length || moving.has(targetId)) return clonePagePlan(pages)
  const remaining = pages.filter(item => !moving.has(item.id))
  const targetIndex = remaining.findIndex(item => item.id === targetId)
  if (targetIndex < 0) return clonePagePlan(pages)
  const insertAt = targetIndex + (before ? 0 : 1)
  remaining.splice(insertAt, 0, ...group)
  return remaining
}

export class PageOrganizerHistory {
  private undoStack: PagePlanSnapshot[] = []
  private redoStack: PagePlanSnapshot[] = []
  private readonly maxEntries: number

  constructor(maxEntries = 50) { this.maxEntries = maxEntries }

  push(current: PagePlanSnapshot): void {
    this.undoStack.push(snapshotPagePlan(current.pages))
    if (this.undoStack.length > Math.max(1, this.maxEntries)) this.undoStack.shift()
    this.redoStack = []
  }

  undo(current: PagePlanSnapshot): PagePlanSnapshot | null {
    const previous = this.undoStack.pop()
    if (!previous) return null
    this.redoStack.push(snapshotPagePlan(current.pages))
    return snapshotPagePlan(previous.pages)
  }

  redo(current: PagePlanSnapshot): PagePlanSnapshot | null {
    const next = this.redoStack.pop()
    if (!next) return null
    this.undoStack.push(snapshotPagePlan(current.pages))
    return snapshotPagePlan(next.pages)
  }

  clear(): void {
    this.undoStack = []
    this.redoStack = []
  }

  get canUndo(): boolean { return this.undoStack.length > 0 }
  get canRedo(): boolean { return this.redoStack.length > 0 }
}

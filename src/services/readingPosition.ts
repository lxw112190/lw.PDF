export type ReadingScale = number | 'auto' | 'page-actual' | 'page-fit' | 'page-width'

export interface ReadingPosition {
  pageNumber: number
  scale: ReadingScale
  left: number
  top: number
  updatedAt: number
}

interface StoredReadingPosition {
  fingerprint: string
  position: ReadingPosition
}

interface StorageLike {
  getItem(key: string): string | null
  setItem(key: string, value: string): void
}

const storageKey = 'lw.PDF.readingPositions.v1'
const namedScales = new Set<ReadingScale>(['auto', 'page-actual', 'page-fit', 'page-width'])

function finiteCoordinate(value: unknown): number | null {
  if (typeof value !== 'number' || !Number.isFinite(value)) return null
  return Math.max(-10_000_000, Math.min(10_000_000, value))
}

function readingScale(value: unknown): ReadingScale | null {
  if (typeof value === 'number' && Number.isFinite(value) && value >= 0.1 && value <= 10) {
    return value
  }
  return typeof value === 'string' && namedScales.has(value as ReadingScale)
    ? value as ReadingScale
    : null
}

export function normalizeReadingPosition(value: unknown): ReadingPosition | null {
  if (!value || typeof value !== 'object') return null
  const candidate = value as Partial<ReadingPosition>
  const scale = readingScale(candidate.scale)
  const left = finiteCoordinate(candidate.left)
  const top = finiteCoordinate(candidate.top)
  if (!Number.isInteger(candidate.pageNumber) || (candidate.pageNumber ?? 0) < 1 ||
      !scale || left === null || top === null || typeof candidate.updatedAt !== 'number' ||
      !Number.isFinite(candidate.updatedAt) || candidate.updatedAt <= 0) {
    return null
  }
  return {
    pageNumber: candidate.pageNumber!,
    scale,
    left,
    top,
    updatedAt: candidate.updatedAt,
  }
}

export class ReadingPositionStore {
  private readonly storage: StorageLike | null
  private readonly maximumDocuments: number

  constructor(storage?: StorageLike | null, maximumDocuments = 100) {
    this.storage = storage === undefined
      ? (typeof window !== 'undefined' ? window.localStorage : null)
      : storage
    this.maximumDocuments = maximumDocuments
  }

  get(fingerprint: string): ReadingPosition | null {
    if (!this.validFingerprint(fingerprint)) return null
    return this.read().find(item => item.fingerprint === fingerprint)?.position ?? null
  }

  save(fingerprint: string, position: ReadingPosition): void {
    const normalized = normalizeReadingPosition(position)
    if (!this.storage || !this.validFingerprint(fingerprint) || !normalized) return
    const items = this.read().filter(item => item.fingerprint !== fingerprint)
    items.unshift({ fingerprint, position: normalized })
    try {
      this.storage.setItem(storageKey, JSON.stringify(items.slice(0, Math.max(1, this.maximumDocuments))))
    } catch {
      // Reading still works when private mode or storage policy rejects persistence.
    }
  }

  private read(): StoredReadingPosition[] {
    if (!this.storage) return []
    try {
      const parsed: unknown = JSON.parse(this.storage.getItem(storageKey) ?? '[]')
      if (!Array.isArray(parsed)) return []
      const result: StoredReadingPosition[] = []
      for (const item of parsed) {
        if (!item || typeof item !== 'object') continue
        const fingerprint = (item as { fingerprint?: unknown }).fingerprint
        const position = normalizeReadingPosition((item as { position?: unknown }).position)
        if (typeof fingerprint !== 'string' || !this.validFingerprint(fingerprint) ||
            !position || result.some(value => value.fingerprint === fingerprint)) continue
        result.push({ fingerprint, position })
      }
      return result
    } catch {
      return []
    }
  }

  private validFingerprint(value: string): boolean {
    return value.length > 0 && value.length <= 256
  }
}

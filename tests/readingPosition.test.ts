import { describe, expect, it } from 'vitest'
import {
  ReadingPositionStore,
  normalizeReadingPosition,
  type ReadingPosition,
} from '../src/services/readingPosition'

function memoryStorage(initial: string | null = null) {
  let value = initial
  return {
    getItem: () => value,
    setItem: (_key: string, next: string) => { value = next },
    value: () => value,
  }
}

function position(pageNumber: number, updatedAt = pageNumber): ReadingPosition {
  return { pageNumber, scale: 'page-width', left: 12, top: 34, updatedAt }
}

describe('reading position persistence', () => {
  it('round-trips page, scale and in-page PDF coordinates by fingerprint', () => {
    const storage = memoryStorage()
    const store = new ReadingPositionStore(storage)
    store.save('fingerprint-a', {
      pageNumber: 18,
      scale: 1.25,
      left: 42.5,
      top: 713.25,
      updatedAt: 1234,
    })
    expect(store.get('fingerprint-a')).toEqual({
      pageNumber: 18,
      scale: 1.25,
      left: 42.5,
      top: 713.25,
      updatedAt: 1234,
    })
  })

  it('keeps only the configured number of most recently saved documents', () => {
    const storage = memoryStorage()
    const store = new ReadingPositionStore(storage, 2)
    store.save('a', position(1))
    store.save('b', position(2))
    store.save('c', position(3))
    expect(store.get('a')).toBeNull()
    expect(store.get('b')?.pageNumber).toBe(2)
    expect(store.get('c')?.pageNumber).toBe(3)
  })

  it('updates an existing fingerprint without creating duplicates', () => {
    const storage = memoryStorage()
    const store = new ReadingPositionStore(storage, 2)
    store.save('a', position(1))
    store.save('b', position(2))
    store.save('a', position(8, 8))
    expect(store.get('a')?.pageNumber).toBe(8)
    expect(JSON.parse(storage.value() ?? '[]')).toHaveLength(2)
  })

  it('ignores corrupt storage and invalid view locations safely', () => {
    const storage = memoryStorage('{broken')
    const store = new ReadingPositionStore(storage)
    expect(store.get('fingerprint')).toBeNull()
    expect(normalizeReadingPosition({
      pageNumber: 0,
      scale: 'unexpected',
      left: Number.NaN,
      top: 0,
      updatedAt: Date.now(),
    })).toBeNull()
    expect(() => store.save('fingerprint', position(2))).not.toThrow()
  })

  it('continues reading when browser persistence is blocked', () => {
    const storage = {
      getItem: () => { throw new Error('blocked') },
      setItem: () => { throw new Error('blocked') },
    }
    const store = new ReadingPositionStore(storage)
    expect(() => store.save('fingerprint', position(2))).not.toThrow()
    expect(store.get('fingerprint')).toBeNull()
  })
})

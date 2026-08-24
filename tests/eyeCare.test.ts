import { beforeEach, describe, expect, it } from 'vitest'
import {
  appearanceState,
  eyeCareStorageKey,
  initializeEyeCareMode,
  readEyeCarePreference,
  setEyeCareMode,
  type EyeCareRoot,
  type EyeCareStorage,
} from '../src/services/eyeCare'

function createHarness(initial?: string) {
  const values = new Map<string, string>()
  if (initial !== undefined) values.set(eyeCareStorageKey, initial)
  const attributes = new Map<string, string>()
  const storage: EyeCareStorage = {
    getItem: key => values.get(key) ?? null,
    setItem: (key, value) => { values.set(key, value) },
    removeItem: key => { values.delete(key) },
  }
  const root: EyeCareRoot = {
    setAttribute: (name, value) => { attributes.set(name, value) },
    removeAttribute: name => { attributes.delete(name) },
  }
  return { attributes, root, storage, values }
}

describe('eye-care preference', () => {
  beforeEach(() => { appearanceState.eyeCareEnabled = false })

  it('restores the saved mode before the app is mounted', () => {
    const harness = createHarness('true')
    expect(initializeEyeCareMode(harness.storage, harness.root)).toBe(true)
    expect(appearanceState.eyeCareEnabled).toBe(true)
    expect(harness.attributes.get('data-eye-care')).toBe('true')
  })

  it('persists enabling and removes the preference when disabled', () => {
    const harness = createHarness()
    setEyeCareMode(true, harness.storage, harness.root)
    expect(harness.values.get(eyeCareStorageKey)).toBe('true')
    setEyeCareMode(false, harness.storage, harness.root)
    expect(harness.values.has(eyeCareStorageKey)).toBe(false)
    expect(harness.attributes.has('data-eye-care')).toBe(false)
  })

  it('falls back safely when browser storage is unavailable', () => {
    const storage: EyeCareStorage = {
      getItem: () => { throw new Error('blocked') },
      setItem: () => { throw new Error('blocked') },
      removeItem: () => { throw new Error('blocked') },
    }
    const harness = createHarness()
    expect(readEyeCarePreference(storage)).toBe(false)
    expect(() => setEyeCareMode(true, storage, harness.root)).not.toThrow()
    expect(harness.attributes.get('data-eye-care')).toBe('true')
  })
})

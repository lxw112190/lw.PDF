import { reactive } from 'vue'

export const eyeCareStorageKey = 'lw.PDF.eyeCare'

export interface EyeCareStorage {
  getItem(key: string): string | null
  setItem(key: string, value: string): void
  removeItem(key: string): void
}

export interface EyeCareRoot {
  setAttribute(name: string, value: string): void
  removeAttribute(name: string): void
}

export const appearanceState = reactive({ eyeCareEnabled: false })

export function readEyeCarePreference(storage: EyeCareStorage): boolean {
  try {
    return storage.getItem(eyeCareStorageKey) === 'true'
  } catch {
    return false
  }
}

export function setEyeCareMode(
  enabled: boolean,
  storage: EyeCareStorage = window.localStorage,
  root: EyeCareRoot = document.documentElement,
) {
  appearanceState.eyeCareEnabled = enabled
  if (enabled) root.setAttribute('data-eye-care', 'true')
  else root.removeAttribute('data-eye-care')
  try {
    if (enabled) storage.setItem(eyeCareStorageKey, 'true')
    else storage.removeItem(eyeCareStorageKey)
  } catch {
    // The visual mode still works when storage is unavailable.
  }
}

export function initializeEyeCareMode(
  storage: EyeCareStorage = window.localStorage,
  root: EyeCareRoot = document.documentElement,
) {
  const enabled = readEyeCarePreference(storage)
  setEyeCareMode(enabled, storage, root)
  return enabled
}

export function toggleEyeCareMode() {
  setEyeCareMode(!appearanceState.eyeCareEnabled)
}


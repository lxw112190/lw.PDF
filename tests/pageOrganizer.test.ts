import { describe, expect, it } from 'vitest'
import {
  createInitialPagePlan,
  isDropBeforeHorizontalMidpoint,
  isPlanDirty,
  movePageGroup,
  normalizeRotation,
  PageOrganizerHistory,
  reversePagePlan,
  rotatePagePlan,
  selectPageRange,
  snapshotPagePlan,
} from '../src/services/pageOrganizer'

const pages = (count: number) => createInitialPagePlan(count)
const numbers = (value: ReturnType<typeof pages>) => value.map(item => item.sourcePage)

describe('page organizer plan', () => {
  it('initializes a clean plan', () => {
    const plan = pages(5)
    expect(numbers(plan)).toEqual([1, 2, 3, 4, 5])
    expect(isPlanDirty(plan)).toBe(false)
  })

  it('reverses pages and detects dirty state', () => {
    const plan = reversePagePlan(pages(3))
    expect(numbers(plan)).toEqual([3, 2, 1])
    expect(isPlanDirty(plan)).toBe(true)
  })

  it('supports snapshot undo and redo', () => {
    const history = new PageOrganizerHistory()
    const initial = pages(3)
    const reversed = reversePagePlan(initial)
    history.push(snapshotPagePlan(initial))
    const restored = history.undo(snapshotPagePlan(reversed))!
    expect(numbers(restored.pages)).toEqual([1, 2, 3])
    const redone = history.redo(restored)!
    expect(numbers(redone.pages)).toEqual([3, 2, 1])
  })

  it('rotates only selected pages with normalization', () => {
    let plan = rotatePagePlan(pages(5), ['source-2', 'source-4', 'source-5'], 90)
    expect(plan.filter(item => item.rotation !== 0).map(item => item.sourcePage)).toEqual([2, 4, 5])
    plan = rotatePagePlan(plan, ['source-2'], 180)
    expect(plan.find(item => item.sourcePage === 2)?.rotation).toBe(270)
    expect(normalizeRotation(360)).toBe(0)
  })

  it('moves a single page and preserves a selected group order', () => {
    const plan = pages(5)
    expect(numbers(movePageGroup(plan, ['source-5'], 'source-2', true))).toEqual([1, 5, 2, 3, 4])
    expect(numbers(movePageGroup(plan, ['source-2', 'source-3'], 'source-5', true))).toEqual([1, 4, 2, 3, 5])
  })

  it('selects a range in either direction', () => {
    const plan = pages(8)
    expect(selectPageRange(plan, 'source-3', 'source-7')).toEqual(
      ['source-3', 'source-4', 'source-5', 'source-6', 'source-7'])
    expect(selectPageRange(plan, 'source-7', 'source-3')).toEqual(
      ['source-3', 'source-4', 'source-5', 'source-6', 'source-7'])
  })

  it('uses the horizontal card midpoint for before and after drops', () => {
    expect(isDropBeforeHorizontalMidpoint(149, 100, 100)).toBe(true)
    expect(isDropBeforeHorizontalMidpoint(150, 100, 100)).toBe(false)
    expect(isDropBeforeHorizontalMidpoint(199, 100, 100)).toBe(false)
  })
})

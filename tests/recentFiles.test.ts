import { describe, expect, it } from 'vitest'
import { formatRecentTime } from '../src/services/recentFiles'

describe('recent file display', () => {
  const now = new Date('2026-08-25T12:00:00Z').getTime()

  it('uses compact relative times for recent opens', () => {
    expect(formatRecentTime(now - 20_000, now)).toBe('刚刚')
    expect(formatRecentTime(now - 5 * 60_000, now)).toBe('5 分钟前')
    expect(formatRecentTime(now - 3 * 3_600_000, now)).toBe('3 小时前')
    expect(formatRecentTime(now - 2 * 86_400_000, now)).toBe('2 天前')
  })

  it('falls back to a calendar date for older files', () => {
    expect(formatRecentTime(now - 10 * 86_400_000, now)).toContain('2026')
  })
})

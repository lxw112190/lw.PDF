import { describe, expect, it } from 'vitest'
import { createFindCommand } from '../src/services/search'

describe('PDF search commands', () => {
  it('starts a fresh search when the query changes', () => {
    expect(createFindCommand('PDF')).toMatchObject({ type: '', query: 'PDF', findPrevious: false })
  })

  it('uses again for next and previous navigation', () => {
    expect(createFindCommand('PDF', 'again')).toMatchObject({ type: 'again', findPrevious: false })
    expect(createFindCommand('PDF', 'again', true)).toMatchObject({ type: 'again', findPrevious: true })
  })
})

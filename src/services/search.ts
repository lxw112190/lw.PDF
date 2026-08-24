export type FindCommandType = '' | 'again'

export interface FindCommand {
  type: FindCommandType
  query: string
  findPrevious: boolean
  phraseSearch: true
  highlightAll: true
}

export function createFindCommand(
  query: string,
  type: FindCommandType = '',
  findPrevious = false,
): FindCommand {
  return { type, query, findPrevious, phraseSearch: true, highlightAll: true }
}

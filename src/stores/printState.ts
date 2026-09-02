import { reactive } from 'vue'

export type PrintPhase = 'idle' | 'preparing' | 'dialog'

export const printState = reactive({
  active: false,
  current: 0,
  total: 0,
  phase: 'idle' as PrintPhase,
})

export function resetPrintState() {
  printState.active = false
  printState.current = 0
  printState.total = 0
  printState.phase = 'idle'
}

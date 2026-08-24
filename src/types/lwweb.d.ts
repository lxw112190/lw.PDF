interface LwNativeApi { platform: string; invoke<T = unknown>(method: string, params?: Record<string, unknown>): Promise<T>; on(event: string, callback: (data: unknown) => void): void; off(event: string, callback: (data: unknown) => void): void }
declare global { interface Window { lw?: LwNativeApi } }
export {}

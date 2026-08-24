export interface FileAssociationStatus {
  registered: boolean
  current: boolean
  defaultApplication: boolean
  executablePath: string
  registeredExecutablePath: string | null
}

function invoke<T>(method: string): Promise<T> {
  if (!window.lw) return Promise.reject(new Error('请在 lw.PDF 桌面应用中使用此功能。'))
  return window.lw.invoke<T>(method)
}

export const desktop = {
  association: {
    status: () => invoke<FileAssociationStatus>('association.status'),
    register: () => invoke<void>('association.register'),
    unregister: () => invoke<void>('association.unregister'),
    openDefaultApps: () => invoke<void>('association.openDefaultApps'),
  },
}

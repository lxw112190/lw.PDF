import type { PdfSource } from '../types/pdf'
export interface NativeFile { id: string; name: string; size: number; mime: string; url: string }
interface OpenFileResult { files: NativeFile[] }
let currentGrantId: string | undefined
export const isDesktop = typeof window !== 'undefined' && !!window.lw
export function nativeFileToPdfSource(file: NativeFile): PdfSource { return { kind: 'url', name: file.name, url: file.url, grantId: file.id, size: file.size } }
export async function openDesktopPdf(): Promise<PdfSource | null> { if (!window.lw) return null; const result = await window.lw.invoke<OpenFileResult>('dialog.openFile', { multiple: false, filters: [{ name: 'PDF 文档', extensions: ['pdf'] }] }); const file = result.files?.[0]; return file ? nativeFileToPdfSource(file) : null }
export async function openBrowserPdf(file: File): Promise<PdfSource> { return { kind: 'data', name: file.name, data: new Uint8Array(await file.arrayBuffer()) } }
export async function revokeDesktopFile(id = currentGrantId) { if (id && window.lw) { try { await window.lw.invoke('file.revoke', { id }) } catch {} } if (id === currentGrantId) currentGrantId = undefined }
export function setCurrentGrant(id?: string) { currentGrantId = id }

export type PdfSource = { kind: 'url'; name: string; url: string; grantId?: string; size?: number } | { kind: 'data'; name: string; data: Uint8Array }
export interface OutlineItem { title: string; dest?: string | unknown[] | null; items?: OutlineItem[] }

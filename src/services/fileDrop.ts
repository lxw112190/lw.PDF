export interface DroppedFileDescriptor {
  name: string
  type?: string
}

export function isPdfFile(file: DroppedFileDescriptor): boolean {
  return /\.pdf$/i.test(file.name.trim())
}

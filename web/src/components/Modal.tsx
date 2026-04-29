import type { ComponentChildren } from 'preact';

interface ModalProps {
  open: boolean;
  title: string;
  onClose: () => void;
  children: ComponentChildren;
  footer?: ComponentChildren;
}

export function Modal({ open, title, onClose, children, footer }: ModalProps) {
  if (!open) return null;
  return (
    <div class="fixed inset-0 z-50 flex items-center justify-center p-4" role="dialog" aria-modal="true">
      <div class="absolute inset-0 bg-black/60 backdrop-blur-sm" onClick={onClose} />
      <div class="relative glass-elev p-5 max-w-md w-full">
        <div class="flex items-center justify-between mb-4">
          <h2 class="text-lg font-semibold">{title}</h2>
          <button onClick={onClose} class="opacity-60 hover:opacity-100 text-xl leading-none">×</button>
        </div>
        <div class="space-y-3">{children}</div>
        {footer && <div class="mt-5 flex justify-end gap-2">{footer}</div>}
      </div>
    </div>
  );
}

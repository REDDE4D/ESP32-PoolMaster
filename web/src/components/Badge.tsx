import type { ComponentChildren } from 'preact';

type Variant = 'ok' | 'info' | 'warn' | 'alarm' | 'muted';

const VARIANT: Record<Variant, string> = {
  ok:    'bg-aqua-ok/15 text-emerald-300 border-emerald-500/30',
  info:  'bg-aqua-info/15 text-cyan-300 border-cyan-500/30',
  warn:  'bg-aqua-warn/15 text-amber-300 border-amber-500/30',
  alarm: 'bg-aqua-alarm/15 text-rose-300 border-rose-500/35',
  muted: 'bg-white/5 text-slate-300 border-white/10',
};

interface BadgeProps {
  variant?: Variant;
  dot?: boolean;
  children: ComponentChildren;
}

export function Badge({ variant = 'muted', dot, children }: BadgeProps) {
  return (
    <span class={`inline-flex items-center gap-1.5 px-2.5 py-0.5 rounded-full text-[0.72rem] font-semibold border ${VARIANT[variant]}`}>
      {dot && <span class="w-1.5 h-1.5 rounded-full bg-current" />}
      {children}
    </span>
  );
}

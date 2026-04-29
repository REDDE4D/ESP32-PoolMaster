import type { ComponentChildren } from 'preact';
import { useSignal } from '@preact/signals';
import { wsStatus } from '../lib/ws';
import { useComputed } from '@preact/signals';

interface NavItem {
  to: string;
  label: string;
  icon: string;  // emoji for now; lucide-preact later if desired
}

const NAV: NavItem[] = [
  { to: '/',           label: 'Home',     icon: '🏠' },
  { to: '/control',    label: 'Control',  icon: '🎛' },
  { to: '/tune',       label: 'Tune',     icon: '🧪' },
  { to: '/insights',   label: 'Insights', icon: '📊' },
  { to: '/settings',   label: 'Settings', icon: '⚙' },
];

function statusBadge(status: string) {
  if (status === 'connected')    return { class: 'text-emerald-300 bg-emerald-500/15', text: 'Live' };
  if (status === 'connecting')   return { class: 'text-amber-300 bg-amber-500/15',   text: 'Connecting' };
  if (status === 'reconnecting') return { class: 'text-amber-300 bg-amber-500/15',   text: 'Reconnecting' };
  return { class: 'text-rose-300 bg-rose-500/15', text: 'Offline' };
}

function currentPath(): string {
  return window.location.pathname || '/';
}

function isActive(path: string, to: string): boolean {
  if (to === '/') return path === '/';
  return path === to || path.startsWith(`${to}/`);
}

interface NavShellProps {
  children: ComponentChildren;
}

export function NavShell({ children }: NavShellProps) {
  const path = useSignal(currentPath());
  const badge = useComputed(() => statusBadge(wsStatus.value));

  // Listen for navigation events (preact-iso handles the pushstate).
  if (typeof window !== 'undefined') {
    window.addEventListener('popstate', () => { path.value = currentPath(); });
    // preact-iso emits a custom event on route change:
    window.addEventListener('preact-iso-route', () => { path.value = currentPath(); });
  }

  const navigate = (to: string) => (e: Event) => {
    e.preventDefault();
    history.pushState({}, '', to);
    path.value = to;
    window.dispatchEvent(new Event('preact-iso-route'));
  };

  return (
    <div class="min-h-screen flex">
      {/* Desktop sidebar */}
      <aside class="hidden md:flex flex-col w-56 p-4 border-r border-aqua-border glass-elev">
        <div class="text-lg font-bold tracking-tight mb-6">🏊 PoolMaster</div>
        <nav class="flex-1 space-y-1">
          {NAV.map(n => (
            <a
              href={n.to}
              onClick={navigate(n.to)}
              class={`flex items-center gap-3 px-3 py-2 rounded-lg text-sm ${
                isActive(path.value, n.to)
                  ? 'bg-aqua-primary/15 text-cyan-200 border border-aqua-border-elev'
                  : 'hover:bg-white/5 text-aqua-text/80'
              }`}
            >
              <span class="text-lg">{n.icon}</span>
              <span>{n.label}</span>
            </a>
          ))}
        </nav>
        <div class="mt-4 pt-4 border-t border-aqua-border">
          <span class={`inline-flex items-center gap-2 px-2.5 py-1 rounded-full text-xs font-semibold ${badge.value.class}`}>
            <span class="w-1.5 h-1.5 rounded-full bg-current" />
            {badge.value.text}
          </span>
        </div>
      </aside>

      {/* Main column */}
      <div class="flex-1 flex flex-col min-w-0">
        {/* Mobile top bar */}
        <header class="md:hidden flex items-center justify-between p-3 safe-top border-b border-aqua-border">
          <span class="font-bold">🏊 PoolMaster</span>
          <span class={`inline-flex items-center gap-1.5 px-2.5 py-0.5 rounded-full text-xs font-semibold ${badge.value.class}`}>
            <span class="w-1.5 h-1.5 rounded-full bg-current" />
            {badge.value.text}
          </span>
        </header>

        <main class="flex-1 overflow-y-auto p-4 pb-24 md:pb-4">{children}</main>

        {/* Mobile bottom tab bar */}
        <nav class="md:hidden fixed bottom-0 inset-x-0 z-40 glass-elev border-t border-aqua-border safe-bottom flex justify-around py-2">
          {NAV.map(n => (
            <a
              href={n.to}
              onClick={navigate(n.to)}
              class={`flex flex-col items-center py-1 px-3 text-xs ${
                isActive(path.value, n.to) ? 'text-cyan-200' : 'text-aqua-text/60'
              }`}
            >
              <span class="text-lg leading-none">{n.icon}</span>
              <span class="mt-0.5">{n.label}</span>
            </a>
          ))}
        </nav>
      </div>
    </div>
  );
}

// Sub-navigation strip used at the top of each section's landing page so
// the user can reach screens that aren't on NavShell's 5-item top nav
// (e.g. Logs, Diagnostics, Setpoints, PID, Schedule, Tanks, Firmware).
//
// Path matching is exact, so the strip highlights the page the user is on.

interface Tab { to: string; label: string }

interface SectionTabsProps {
  current: string;
  tabs: Tab[];
}

function navigate(to: string) {
  return (e: Event) => {
    e.preventDefault();
    history.pushState({}, '', to);
    window.dispatchEvent(new Event('preact-iso-route'));
  };
}

export function SectionTabs({ current, tabs }: SectionTabsProps) {
  return (
    <div class="flex flex-wrap gap-1 mb-2">
      {tabs.map(t => {
        const active = t.to === current;
        return (
          <a
            key={t.to}
            href={t.to}
            onClick={navigate(t.to)}
            class={`text-xs px-3 py-1 rounded-md border ${
              active
                ? 'bg-aqua-primary/25 border-aqua-primary/50 text-cyan-100'
                : 'bg-white/5 border-aqua-border text-aqua-text/70 hover:text-aqua-text'
            }`}
          >
            {t.label}
          </a>
        );
      })}
    </div>
  );
}

// Convenience: per-section tab definitions so each screen only needs to
// pass `current={location.pathname}` and the right SECTION constant.
export const TABS_CONTROL = [
  { to: '/control',           label: 'Manual'    },
  { to: '/control/setpoints', label: 'Setpoints' },
];

export const TABS_TUNE = [
  { to: '/tune',          label: 'Calibration' },
  { to: '/tune/pid',      label: 'PID'         },
  { to: '/tune/schedule', label: 'Schedule'    },
  { to: '/tune/tanks',    label: 'Tanks'       },
];

export const TABS_INSIGHTS = [
  { to: '/insights',             label: 'History'     },
  { to: '/insights/logs',        label: 'Logs'        },
  { to: '/insights/diagnostics', label: 'Diagnostics' },
];

export const TABS_SETTINGS = [
  { to: '/settings',          label: 'Network' },
  { to: '/settings/drivers',  label: 'Drivers' },
  { to: '/settings/firmware', label: 'Firmware' },
];

interface ToggleProps {
  on: boolean;
  onChange: (next: boolean) => void;
  label?: string;
  disabled?: boolean;
}

export function Toggle({ on, onChange, label, disabled }: ToggleProps) {
  return (
    <button
      type="button"
      role="switch"
      aria-checked={on}
      disabled={disabled}
      onClick={() => !disabled && onChange(!on)}
      class={`inline-flex items-center gap-3 ${disabled ? 'opacity-50 cursor-not-allowed' : 'cursor-pointer'}`}
    >
      <span class={`relative w-[42px] h-6 rounded-full border transition-colors ${
        on ? 'bg-aqua-primary/35 border-aqua-primary/50' : 'bg-slate-900/60 border-aqua-border'
      }`}>
        <span class={`absolute top-0.5 w-[18px] h-[18px] rounded-full transition-all ${
          on ? 'left-[22px] bg-aqua-primary-hover' : 'left-0.5 bg-slate-300'
        }`} />
      </span>
      {label && <span class="text-sm">{label}</span>}
    </button>
  );
}

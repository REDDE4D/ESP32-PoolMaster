import { useEffect, useRef, useState } from 'preact/hooks';

interface TimePickerProps {
  /** Minutes-of-day, 0..1439. */
  value: number;
  onChange: (minutes: number) => void;
  disabled?: boolean;
}

function pad(n: number): string {
  return n < 10 ? `0${n}` : String(n);
}

export function TimePicker({ value, onChange, disabled }: TimePickerProps) {
  const [text, setText] = useState(`${pad(Math.floor(value / 60))}:${pad(value % 60)}`);
  const lastEmitted = useRef(value);

  useEffect(() => {
    if (value !== lastEmitted.current) {
      setText(`${pad(Math.floor(value / 60))}:${pad(value % 60)}`);
      lastEmitted.current = value;
    }
  }, [value]);

  const commit = (raw: string) => {
    const m = /^(\d{1,2}):(\d{2})$/.exec(raw.trim());
    if (!m) {
      setText(`${pad(Math.floor(value / 60))}:${pad(value % 60)}`);
      return;
    }
    const h = Math.min(23, Math.max(0, parseInt(m[1]!, 10)));
    const min = Math.min(59, Math.max(0, parseInt(m[2]!, 10)));
    const total = h * 60 + min;
    setText(`${pad(h)}:${pad(min)}`);
    if (total !== lastEmitted.current) {
      lastEmitted.current = total;
      onChange(total);
    }
  };

  return (
    <input
      type="text"
      inputMode="numeric"
      pattern="[0-9]{1,2}:[0-9]{2}"
      class="bg-slate-900/40 border border-aqua-border rounded-md px-2 py-1 w-20 text-center font-mono text-sm focus:outline-none focus:ring-1 focus:ring-aqua-primary disabled:opacity-40"
      value={text}
      disabled={disabled}
      onInput={e => setText((e.target as HTMLInputElement).value)}
      onBlur={e => commit((e.target as HTMLInputElement).value)}
      onKeyDown={e => { if (e.key === 'Enter') (e.target as HTMLInputElement).blur(); }}
    />
  );
}

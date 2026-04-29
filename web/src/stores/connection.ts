import { signal } from '@preact/signals';

export interface WelcomeInfo {
  device: string;
  firmware: string;
  server_time: number;
}

export const welcome = signal<WelcomeInfo | null>(null);

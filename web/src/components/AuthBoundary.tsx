import { useCallback } from 'preact/hooks';
import { forceAuthPrompt } from '../lib/api';

/**
 * Wraps an async admin-gated action. If the action fails with auth_required
 * (HTTP 401 or `error: "auth_required"` in body), we trigger the browser's
 * Basic-auth dialog via a GET to /api/whoami and retry once.
 */
export function useAuthedAction<T>(action: () => Promise<{ ok: boolean; error?: string }>) {
  return useCallback(async () => {
    const first = await action();
    if (first.ok) return first;
    if (first.error?.includes('auth')) {
      const authed = await forceAuthPrompt();
      if (authed) return action();
    }
    return first;
  }, [action]);
}

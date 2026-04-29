// Basic fetch helpers. All routes use relative URLs — Vite dev-proxy
// forwards /api/* to the device; in production the SPA is served from
// the device itself so relative URLs just work.

export interface ApiResult<T = unknown> {
  ok: boolean;
  status: number;
  data?: T;
  error?: string;
}

export async function apiGet<T = unknown>(path: string): Promise<ApiResult<T>> {
  try {
    const res = await fetch(path, { credentials: 'same-origin' });
    const body = res.headers.get('content-type')?.includes('application/json')
      ? await res.json()
      : await res.text();
    if (!res.ok) return { ok: false, status: res.status, error: typeof body === 'string' ? body : JSON.stringify(body) };
    return { ok: true, status: res.status, data: body as T };
  } catch (err: unknown) {
    const msg = err instanceof Error ? err.message : String(err);
    return { ok: false, status: 0, error: msg };
  }
}

export async function apiPostForm<T = unknown>(path: string, fields: Record<string, string>): Promise<ApiResult<T>> {
  const body = new URLSearchParams(fields);
  try {
    const res = await fetch(path, {
      method: 'POST',
      credentials: 'same-origin',
      body,
    });
    const text = await res.text();
    let data: unknown = text;
    try { data = JSON.parse(text); } catch { /* keep as text */ }
    if (!res.ok) return { ok: false, status: res.status, error: typeof data === 'string' ? data : JSON.stringify(data) };
    return { ok: true, status: res.status, data: data as T };
  } catch (err: unknown) {
    const msg = err instanceof Error ? err.message : String(err);
    return { ok: false, status: 0, error: msg };
  }
}

export async function apiPostJson<T = unknown>(path: string, body: unknown): Promise<ApiResult<T>> {
  try {
    const res = await fetch(path, {
      method: 'POST',
      credentials: 'same-origin',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify(body),
    });
    const text = await res.text();
    let data: unknown = text;
    try { data = JSON.parse(text); } catch { /* keep as text */ }
    if (!res.ok) return { ok: false, status: res.status, error: typeof data === 'string' ? data : JSON.stringify(data) };
    return { ok: true, status: res.status, data: data as T };
  } catch (err: unknown) {
    const msg = err instanceof Error ? err.message : String(err);
    return { ok: false, status: 0, error: msg };
  }
}

// Trigger a browser Basic-auth challenge on demand. Used by the UI before
// issuing a command when the server reports auth_required.
export async function forceAuthPrompt(): Promise<boolean> {
  const res = await apiGet('/api/whoami');
  return res.ok;
}

export async function apiDelete(path: string, form?: Record<string, string>): Promise<ApiResult<void>> {
  const body = form ? new URLSearchParams(form).toString() : undefined;
  try {
    const res = await fetch(path, {
      method: 'DELETE',
      credentials: 'same-origin',
      headers: form ? { 'Content-Type': 'application/x-www-form-urlencoded' } : {},
      body,
    });
    if (!res.ok) {
      const text = await res.text();
      return { ok: false, status: res.status, error: text };
    }
    return { ok: true, status: res.status };
  } catch (err: unknown) {
    const msg = err instanceof Error ? err.message : String(err);
    return { ok: false, status: 0, error: msg };
  }
}

// Send a command as legacy JSON (e.g. {"FiltPump":1}) via POST /api/cmd.
export async function apiSendCommand(json: string): Promise<ApiResult<{ ok: boolean; queued?: boolean; error?: string }>> {
  try {
    const res = await fetch('/api/cmd', {
      method: 'POST',
      credentials: 'same-origin',
      headers: { 'Content-Type': 'application/json' },
      body: json,
    });
    const data = (await res.json()) as { ok: boolean; queued?: boolean; error?: string };
    if (!res.ok) return { ok: false, status: res.status, data, error: data.error };
    return { ok: true, status: res.status, data };
  } catch (err: unknown) {
    const msg = err instanceof Error ? err.message : String(err);
    return { ok: false, status: 0, error: msg };
  }
}

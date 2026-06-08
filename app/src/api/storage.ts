//
// Tiny persistent KV. Uses Capacitor Preferences on device (which is backed by
// the OS keystore-adjacent store) and falls back to localStorage on the web.
//
import { Preferences } from '@capacitor/preferences';

export async function getItem(key: string): Promise<string | null> {
  try {
    const { value } = await Preferences.get({ key });
    return value;
  } catch {
    return localStorage.getItem(key);
  }
}

export async function setItem(key: string, value: string): Promise<void> {
  try {
    await Preferences.set({ key, value });
  } catch {
    localStorage.setItem(key, value);
  }
}

export async function removeItem(key: string): Promise<void> {
  try {
    await Preferences.remove({ key });
  } catch {
    localStorage.removeItem(key);
  }
}

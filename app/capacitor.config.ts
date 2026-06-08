import type { CapacitorConfig } from '@capacitor/cli';

// One web bundle (dist/) → iOS app, Android app, and the installable PWA.
const config: CapacitorConfig = {
  appId: 'com.polymath.app',
  appName: 'Polymath',
  webDir: 'dist',
  backgroundColor: '#0f1115',
  ios: {
    contentInset: 'always',
  },
  android: {
    // Cleartext only matters for LAN-direct http://polymath.local; remote is
    // always wss/https via the relay.
    allowMixedContent: true,
  },
  plugins: {
    PushNotifications: {
      presentationOptions: ['badge', 'sound', 'alert'],
    },
  },
};

export default config;

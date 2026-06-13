import { useEffect } from 'react';
import { HashRouter, Navigate, Route, Routes } from 'react-router-dom';
import { Layout } from './components/Layout';
import { Toasts } from './components/Toasts';
import { Loading } from './components/ui';
import { useApp } from './state/store';
import { isPaired } from './api/auth';
import { api } from './api/client';
import { socket } from './api/socket';
import { isRemote } from './api/transport';
import type { NoticeEvent, ServerStatus, TokenEvent } from './api/contract';

import { Pair } from './screens/Pair';
import { ChatScreen } from './screens/Chat';
import { CamerasScreen } from './screens/Cameras';
import { TasksScreen } from './screens/Tasks';
import { TimelineScreen } from './screens/Timeline';
import { MoreScreen } from './screens/More';
import { ShoppingScreen } from './screens/Shopping';
import { RemindersScreen } from './screens/Reminders';
import { MemoryScreen } from './screens/Memory';
import { PersonalitiesScreen } from './screens/Personalities';
import { SettingsScreen } from './screens/Settings';
import { DevicesScreen } from './screens/Devices';
import { FabricDevicesScreen } from './screens/FabricDevices';
import { InstrumentsScreen } from './screens/Instruments';
import { LabSessionScreen } from './screens/LabSession';
import { CameraClipsScreen } from './screens/CameraClips';

export function App() {
  const paired = useApp((s) => s.paired);
  const setPaired = useApp((s) => s.setPaired);

  useEffect(() => {
    isPaired().then(setPaired);
  }, [setPaired]);

  if (paired === null) return <Loading label="Connecting…" />;
  if (!paired) return <Pair onPaired={() => setPaired(true)} />;
  return <PairedApp />;
}

function PairedApp() {
  const setStatus = useApp((s) => s.setStatus);
  const setOnline = useApp((s) => s.setOnline);
  const setRemote = useApp((s) => s.setRemote);
  const pushToast = useApp((s) => s.pushToast);

  // Global live wiring: connection state, notices → toasts, reminders, status,
  // and device-fabric events (routed into store by socket.ts automatically).
  useEffect(() => {
    socket.start();
    socket.subscribe(['notice', 'reminder', 'status', 'privacy',
      'instrument_reading', 'device_presence', 'lab_step']);

    const offState = socket.onState(setOnline);
    const offEvent = socket.on((e) => {
      switch (e.type) {
        case 'notice': {
          const n = e.data as NoticeEvent;
          pushToast(n.level === 'error' ? 'bad' : 'info', n.message);
          break;
        }
        case 'reminder': {
          const r = e.data as { text: string };
          pushToast('info', `⏰ ${r.text}`);
          break;
        }
        case 'status':
          setStatus(e.data as ServerStatus);
          break;
      }
    });

    api
      .status()
      .then((s) => {
        setStatus(s);
        setRemote(isRemote());
      })
      .catch(() => {});

    return () => {
      offState();
      offEvent();
      socket.stop();
    };
  }, [setStatus, setOnline, setRemote, pushToast]);

  return (
    <>
      <HashRouter>
        <Routes>
          <Route element={<Layout />}>
            {/* Chat is the default/home tab */}
            <Route index element={<Navigate to="/chat" replace />} />
            <Route path="/chat" element={<ChatScreen />} />
            <Route path="/cameras" element={<CamerasScreen />} />
            <Route path="/tasks" element={<TasksScreen />} />
            <Route path="/timeline" element={<TimelineScreen />} />
            <Route path="/more" element={<MoreScreen />} />
            <Route path="/shopping" element={<ShoppingScreen />} />
            <Route path="/reminders" element={<RemindersScreen />} />
            <Route path="/memory" element={<MemoryScreen />} />
            <Route path="/personalities" element={<PersonalitiesScreen />} />
            <Route path="/settings" element={<SettingsScreen />} />
            <Route path="/devices" element={<DevicesScreen />} />
            {/* Device fabric (v2) */}
            <Route path="/fabric" element={<FabricDevicesScreen />} />
            <Route path="/instruments" element={<InstrumentsScreen />} />
            <Route path="/lab" element={<LabSessionScreen />} />
            <Route path="/clips/:deviceId" element={<CameraClipsScreen />} />
            <Route path="*" element={<Navigate to="/chat" replace />} />
          </Route>
        </Routes>
      </HashRouter>
      <Toasts />
    </>
  );
}

// re-exported so screens can import the streaming token type conveniently
export type { TokenEvent };

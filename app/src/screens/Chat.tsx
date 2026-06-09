//
// Chat with streaming replies. We POST the turn, then stitch the assistant's
// answer together from `token` WS events correlated by request_id.
//
import { useEffect, useRef, useState } from 'react';
import { Icon } from '../components/icons';
import { EmptyState } from '../components/ui';
import { api } from '../api/client';
import { socket } from '../api/socket';
import type { ChatMessageDTO, TokenEvent } from '../api/contract';
import { useApp } from '../state/store';

interface Msg {
  id: string;
  role: 'user' | 'assistant';
  content: string;
  done: boolean;
}

export function ChatScreen() {
  const [messages, setMessages] = useState<Msg[]>([]);
  const [draft, setDraft] = useState('');
  const [sending, setSending] = useState(false);
  const pushToast = useApp((s) => s.pushToast);
  const bottomRef = useRef<HTMLDivElement>(null);

  // history
  useEffect(() => {
    api
      .chatHistory(50)
      .then((rows: ChatMessageDTO[]) =>
        setMessages(
          rows
            .filter((r) => r.role === 'user' || r.role === 'assistant')
            .map((r, i) => ({
              id: r.request_id ?? `h${i}`,
              role: r.role as 'user' | 'assistant',
              content: r.content,
              done: true,
            })),
        ),
      )
      .catch(() => {});
  }, []);

  // token stream
  useEffect(() => {
    socket.subscribe(['token']);
    const off = socket.on((e) => {
      if (e.type !== 'token') return;
      const t = e.data as TokenEvent;
      setMessages((prev) => {
        const idx = prev.findIndex((m) => m.id === t.request_id);
        if (idx === -1) {
          return [
            ...prev,
            { id: t.request_id, role: 'assistant', content: t.text, done: t.done },
          ];
        }
        const next = [...prev];
        next[idx] = {
          ...next[idx],
          content: next[idx].content + t.text,
          done: t.done,
        };
        return next;
      });
    });
    return () => {
      off();
      socket.unsubscribe(['token']);
    };
  }, []);

  useEffect(() => {
    bottomRef.current?.scrollIntoView({ behavior: 'smooth' });
  }, [messages]);

  async function send() {
    const text = draft.trim();
    if (!text || sending) return;
    setDraft('');
    setSending(true);
    setMessages((m) => [
      ...m,
      { id: `u${Date.now()}`, role: 'user', content: text, done: true },
    ]);
    try {
      const { request_id } = await api.sendChat({ text });
      // Pre-create the assistant bubble so tokens land even if they race.
      setMessages((m) =>
        m.some((x) => x.id === request_id)
          ? m
          : [...m, { id: request_id, role: 'assistant', content: '', done: false }],
      );
    } catch (e) {
      pushToast('bad', `Send failed: ${(e as Error).message}`);
    } finally {
      setSending(false);
    }
  }

  return (
    <div style={{ display: 'flex', flexDirection: 'column', minHeight: '100%' }}>
      <div
        className="app-content"
        style={{ flex: 1, display: 'flex', flexDirection: 'column', gap: 10 }}
      >
        {messages.length === 0 && (
          <EmptyState
            icon={<Icon name="chat" size={34} />}
            title="Say hello"
            hint="Ask a question, set a reminder, or queue a deep-work task."
          />
        )}
        {messages.map((m) => (
          <div key={m.id} className={`bubble ${m.role}`}>
            {m.content || (m.role === 'assistant' && !m.done ? '…' : '')}
          </div>
        ))}
        <div ref={bottomRef} />
      </div>

      <div
        style={{
          position: 'sticky',
          bottom: 0,
          display: 'flex',
          gap: 8,
          padding: 12,
          paddingBottom: 'calc(12px + var(--safe-bottom))',
          background: 'var(--bg)',
          borderTop: '1px solid var(--line)',
        }}
      >
        <input
          className="input"
          value={draft}
          onChange={(e) => setDraft(e.target.value)}
          onKeyDown={(e) => e.key === 'Enter' && send()}
          placeholder="Message Hearth…"
        />
        <button
          className="btn"
          style={{ padding: '0 14px' }}
          onClick={send}
          disabled={sending || !draft.trim()}
          aria-label="Send"
        >
          <Icon name="send" size={20} />
        </button>
      </div>
    </div>
  );
}

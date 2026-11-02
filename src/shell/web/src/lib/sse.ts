import type { ServerEvent } from './types';

export type StreamStatus = 'connecting' | 'online' | 'reconnecting' | 'closed';

const EVENT_TYPES = [
  'client.hello',
  'client.joined',
  'client.left',
  'stream.reset',
  'file.indexed',
  'file.changed',
  'file.reindexed',
  'file.indexing_failed',
  'indexing.progress',
  'search.progress',
  'search.completed',
  'event.extraction.progress',
  'event.extraction.completed',
  'marks.changed',
  'engine.error',
  'view.changed',
];

export class EventStream {
  private source: EventSource | null = null;
  private closed = false;

  onEvent: (event: ServerEvent) => void = () => {};
  onStatus: (status: StreamStatus) => void = () => {};

  constructor(
    private clientId: string,
    private token = '',
  ) {}

  open(): void {
    this.close();
    this.closed = false;

    const params = new URLSearchParams({ clientId: this.clientId });
    if (this.token) {
      params.set('token', this.token);
    }

    const source = new EventSource(`/api/events?${params.toString()}`);
    this.source = source;
    this.onStatus('connecting');

    source.onopen = () => {
      if (!this.closed) {
        this.onStatus('online');
      }
    };

    source.onerror = () => {
      if (!this.closed) {
        this.onStatus('reconnecting');
      }
    };

    for (const type of EVENT_TYPES) {
      source.addEventListener(type, (raw) => {
        try {
          const event = JSON.parse((raw as MessageEvent).data) as ServerEvent;
          this.onEvent(event);
        } catch {
          // An event that is not JSON is a server bug; keep the stream alive.
        }
      });
    }
  }

  close(): void {
    this.closed = true;
    if (this.source) {
      this.source.close();
      this.source = null;
    }
    this.onStatus('closed');
  }
}

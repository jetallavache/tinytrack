# tinytsdk

JavaScript/TypeScript SDK for the [TinyTrack](https://github.com/jetallavache/tinytrack) monitoring system.

## Packages

| Entry point | Contents |
|-------------|----------|
| `tinytsdk` | Core WebSocket client + protocol parser (no framework deps) |
| `tinytsdk/react` | React components + `TinyTrackProvider` context |

## Installation

```bash
npm install tinytsdk
```

## Vanilla JS / TypeScript

```ts
import { TinyTrackClient, RING_L1 } from 'tinytsdk';

const client = new TinyTrackClient('ws://<hostname>:25015');

client.on('metrics', m => {
  console.log(`CPU: ${m.cpu / 100}%  MEM: ${m.mem / 100}%`);
});

client.on('open', () => {
  // request history from L1 ring (last 60 samples)
  client.getHistory(RING_L1, 60);
  // set push interval to 5 seconds
  client.setInterval(5000);
});

client.connect();
```

### CDN (browser)

```html
<script type="module">
  import { TinyTrackClient } from 'https://cdn.jsdelivr.net/npm/tinytsdk/dist/tinytsdk.min.js';
  const client = new TinyTrackClient('ws://<hostname>:25015');
  client.on('metrics', m => console.log(m));
  client.connect();
</script>
```

## React

```tsx
import { TinyTrackProvider, useTinyTrack, MetricsPanel } from 'tinytsdk/react';

function App() {
  return (
    <TinyTrackProvider url="ws://<hostname>:25015">
      <Dashboard />
    </TinyTrackProvider>
  );
}

function Dashboard() {
  const { metrics, connected } = useTinyTrack();
  return (
    <>
      <p>Status: {connected ? 'connected' : 'disconnected'}</p>
      <MetricsPanel />
    </>
  );
}
```

## API

### `TinyTrackClient`

```ts
const client = new TinyTrackClient(url, options?);
```

| Option | Type | Default | Description |
|--------|------|---------|-------------|
| `reconnect` | boolean | `true` | Auto-reconnect on close |
| `reconnectDelay` | number | `2000` | Delay between reconnects (ms) |
| `maxRetries` | number | `0` | Max retries (0 = unlimited) |
| `path` | string | `'/v1/stream'` | WebSocket path |

**Methods:**

| Method | Description |
|--------|-------------|
| `connect()` | Open WebSocket connection |
| `disconnect()` | Close connection |
| `getSnapshot()` | Request current metrics snapshot |
| `getStats()` | Request ring buffer statistics |
| `setInterval(ms)` | Change push interval |
| `getHistory(level, maxCount?, fromTs?, toTs?)` | Request historical data |
| `subscribe(level, intervalMs?)` | Subscribe to a ring level |
| `on(event, fn)` / `off(event, fn)` | Add/remove event listener |

**Events:** `open`, `close`, `error`, `metrics`, `config`, `ack`, `stats`, `history`

### Metrics fields (`TtMetrics`)

| Field | Type | Description |
|-------|------|-------------|
| `timestamp` | number | ms since epoch |
| `cpu` | number | CPU usage × 100 (e.g. 5000 = 50%) |
| `mem` | number | Memory usage × 100 |
| `netRx` / `netTx` | number | Network bytes/sec |
| `load1` / `load5` / `load15` | number | Load average × 100 |
| `nrRunning` / `nrTotal` | number | Process counts |
| `duUsage` | number | Disk usage × 100 |
| `duTotal` / `duFree` | number | Disk bytes |

### Ring levels

| Constant | Value | Resolution | Retention |
|----------|-------|------------|-----------|
| `RING_L1` | 1 | 1 second | 1 hour |
| `RING_L2` | 2 | 1 minute | 24 hours |
| `RING_L3` | 3 | 1 hour | 7 days |

## Development

```bash
npm test          # run tests
npm run build     # build dist/
```

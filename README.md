
<img src="docs/assets/logo.svg" width="32" height="32" align="left" style="float:left; margin-top:-2px; padding-right:8px" />

# tinytrack.dev

TinyTrack is a lightweight Linux system metrics daemon with real-time WebSocket streaming.
It collects CPU, memory, network, disk, and load average — and streams them live to any
browser, dashboard, or custom app. The only server-side dependencies are libc and libssl.

Runs on any Linux VPS. Consumes less than 1% CPU and under 10 MB of RAM.

[![Build Status](https://github.com/jetallavache/tinytrack/actions/workflows/sdk-publish.yml/badge.svg)](https://github.com/jetallavache/tinytrack/actions/workflows/sdk-publish.yml)
[![Build Status](https://github.com/jetallavache/tinytrack/actions/workflows/server.yml/badge.svg)](https://github.com/jetallavache/tinytrack/actions/workflows/server.yml)
[![Build Status](https://github.com/jetallavache/tinytrack/actions/workflows/storybook.yml/badge.svg)](https://github.com/jetallavache/tinytrack/actions/workflows/storybook.yml)
[![License: MIT](https://img.shields.io/badge/license-MIT-27AE60.svg)](LICENSE)
[![Docker](https://img.shields.io/badge/docker-hub-2496ED.svg?logo=docker&logoColor=white)](https://hub.docker.com/r/jetallavache/tinytrack)
[![Platform: Linux](https://img.shields.io/badge/platform-Linux-E67E22.svg)](https://kernel.org/)
<!-- [![npm version](https://img.shields.io/npm/v/tinytsdk.svg)](https://www.npmjs.com/package/tinytsdk)
[![npm version lite](https://img.shields.io/npm/v/tinytsdk-lite.svg)](https://www.npmjs.com/package/tinytsdk-lite) -->

[![npm](https://nodei.co/npm/tinytsdk.png)](https://www.npmjs.com/package/tinytsdk)
[![npm](https://nodei.co/npm/tinytsdk-lite.png)](https://www.npmjs.com/package/tinytsdk-lite)

## Deploy the server

Pick the method that fits your setup.

### Option A — one-line install on a host

```bash
curl -fsSL https://raw.githubusercontent.com/jetallavache/tinytrack/main/install.sh | bash
```

That's it. The script builds from source, installs binaries, and starts `tinytd` + `tinytrack`
as systemd services. Supports Debian/Ubuntu, Fedora/RHEL, and Arch.

### Option B — Docker

```bash
curl -fsSL https://raw.githubusercontent.com/jetallavache/tinytrack/main/install.sh | TINYTRACK_DOCKER=1 bash
```

Or pull and run directly:

```bash
docker run -d --name tinytrack \
  -p 25015:25015 \
  -v /proc:/host/proc:ro \
  -v /:/host/rootfs:ro \
  -v /dev/shm:/dev/shm \
  -e TT_PROC_ROOT=/host/proc \
  -e TT_ROOTFS_PATH=/host/rootfs \
  jetallavache/tinytrack:latest
```

### Option C — Docker Compose

```yaml
services:
  tinytrack:
    image: jetallavache/tinytrack:latest
    ports:
      - "25015:25015"
    volumes:
      - /proc:/host/proc:ro
      - /:/host/rootfs:ro
      - /dev/shm:/dev/shm
    environment:
      TT_PROC_ROOT: /host/proc
      TT_ROOTFS_PATH: /host/rootfs
    restart: unless-stopped
```

### Option D — Kubernetes

```yaml
apiVersion: apps/v1
kind: DaemonSet
metadata:
  name: tinytrack
spec:
  selector:
    matchLabels:
      app: tinytrack
  template:
    metadata:
      labels:
        app: tinytrack
    spec:
      containers:
        - name: tinytrack
          image: jetallavache/tinytrack:latest
          ports:
            - containerPort: 25015
          env:
            - name: TT_PROC_ROOT
              value: /host/proc
            - name: TT_ROOTFS_PATH
              value: /host/rootfs
          volumeMounts:
            - name: proc
              mountPath: /host/proc
              readOnly: true
            - name: rootfs
              mountPath: /host/rootfs
              readOnly: true
      volumes:
        - name: proc
          hostPath:
            path: /proc
        - name: rootfs
          hostPath:
            path: /
```

Once running, the server is available at `ws://your-host:25015/websocket`.

---

## Add metrics to your website

### npm (React / TypeScript)

Install the SDK:

```bash
npm install tinytsdk
```

React component:

```jsx
import { TinyTrackProvider, MetricsBar } from 'tinytsdk/react';

export default function App() {
  return (
    <TinyTrackProvider url="ws://your-host:25015">
      <MetricsBar />
    </TinyTrackProvider>
  );
}
```

### CDN (Vanilla JS, no build step)

```html
<script src="https://cdn.jsdelivr.net/npm/tinytsdk/dist/tinytsdk.min.js"></script>
<script>
  const client = new TinyTrack.TinyTrackClient('ws://your-host:25015');
  client.on('metrics', (m) => {
    document.getElementById('cpu').textContent = (m.cpu / 100).toFixed(1) + '%';
  });
  client.connect();
</script>

<span id="cpu">—</span>
```

### TypeScript (ESM)

```ts
import { TinyTrackClient } from 'tinytsdk';

const client = new TinyTrackClient('ws://your-host:25015');
client.on('metrics', (m) => {
  console.log(`CPU: ${m.cpu / 100}%  RAM: ${m.mem / 100}%`);
});
client.connect();

---

## CLI

```bash
tiny-cli status           # daemon status
tiny-cli metrics          # current snapshot
tiny-cli history l1       # last hour  (1 s resolution)
tiny-cli history l2       # last 24 h  (1 min resolution)
tiny-cli history l3       # last 30 days (1 hr resolution)
tiny-cli dashboard        # live ncurses dashboard
```

---

## Documentation

| | Русский | English |
| :--- | :--- | :--- |
| Overview | [docs/ru/overview.md](docs/ru/overview.md) | [docs/en/overview.md](docs/en/overview.md) |
| Install | [docs/ru/install.md](docs/ru/install.md) | [docs/en/install.md](docs/en/install.md) |
| Docker | [docs/ru/docker.md](docs/ru/docker.md) | [docs/en/docker.md](docs/en/docker.md) |
| Configuration | [docs/ru/configuration.md](docs/ru/configuration.md) | [docs/en/configuration.md](docs/en/configuration.md) |
| Architecture | [docs/ru/architecture.md](docs/ru/architecture.md) | [docs/en/architecture.md](docs/en/architecture.md) |
| Troubleshooting | [docs/ru/troubleshooting.md](docs/ru/troubleshooting.md) | [docs/en/troubleshooting.md](docs/en/troubleshooting.md) |
| SDK | [docs/ru/sdk.md](docs/ru/sdk.md) | [docs/en/sdk.md](docs/en/sdk.md) |

## Contributing

Pull requests are welcome. Please keep each PR focused on a single issue and include a clear description of the change.

## License

MIT License — see [LICENSE](LICENSE)

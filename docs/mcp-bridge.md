# MCP Bridge

A standalone CLI tool that aggregates multiple MCP servers — stdio or HTTP/SSE — and re-exposes them either behind a single HTTP endpoint **or** as one stdio server. Built on top of LLMQore.

## What it does

MCP servers in the ecosystem come in two flavours: stdio (child processes) and HTTP/SSE (network endpoints). Clients are just as split — some speak only stdio (e.g. Claude Desktop), some only HTTP. The bridge glues any mix together:

- **Aggregation** — connect several upstreams, expose their tools as a flat list.
- **Protocol translation** — any combination of client/upstream transports works:
  - stdio upstream → HTTP client
  - SSE / Streamable-HTTP upstream → HTTP client
  - stdio upstream → stdio client
  - SSE / Streamable-HTTP upstream → stdio client
- **Live re-sync** — when an upstream signals `notifications/tools/list_changed`, the bridge refreshes its tool list automatically.
- **Auto-reconnect** — if an upstream drops, its tools are removed and the bridge retries with exponential backoff (1s → 30s); tools are re-registered once it comes back.

```
┌─────────────────┐   HTTP / stdio    ┌───────────────┐  stdio / HTTP / SSE   ┌──────────────┐
│   MCP client    │ ◄───────────────► │               │ ◄───────────────────► │ MCP server A │
│ (Claude Desktop │                   │  mcp-bridge   │                       └──────────────┘
│  Qt Creator,    │                   │               │  stdio / HTTP / SSE   ┌──────────────┐
│  web hosts ...) │                   │               │ ◄───────────────────► │ MCP server B │
└─────────────────┘                   └───────────────┘                       └──────────────┘
```

## Installation

### Prebuilt binaries

Download from [GitHub Releases](https://github.com/Palm1r/llmqore/releases):

- `mcp-bridge-linux-x86_64.tar.gz`
- `mcp-bridge-macos-universal.tar.gz` (x86_64 + arm64)
- `mcp-bridge-windows-x86_64.zip`

Each archive contains the `mcp-bridge` binary together with the Qt runtime it needs (DLLs on Windows, frameworks on macOS, `.so` files on Linux). Extract the archive and run `mcp-bridge` from inside — no separate Qt installation required.

### Build from source

The bridge is built as part of the llmqore CMake project:

```bash
cmake -B build -DLLMQORE_BUILD_MCP_BRIDGE=ON
cmake --build build --target mcp-bridge
```

The binary is produced at `build/mcp-bridge/mcp-bridge`.

## Usage

```
mcp-bridge [options] [config-file]

Options:
  --port <port>      HTTP port (overrides config)
  --host <address>   Bind address (default 127.0.0.1)
  --stdio            Serve MCP over stdio instead of HTTP
  -h, --help         Show help
  -v, --version      Show version
```

If no config file is given, the bridge looks for `mcp-bridge.json` in the current directory.

In `--stdio` mode the `port` / `host` options and top-level config fields are ignored; see [Connecting clients → Stdio mode](#stdio-mode) below.

## Configuration

Two different files are involved — don't mix them up:

1. **Bridge config** (`mcp-bridge.json` or whatever path you pass to `mcp-bridge`) — lists upstream MCP servers the bridge will aggregate.
2. **Client config** (e.g. `claude_desktop_config.json`) — lists MCP servers the *client* connects to. When using the bridge in stdio mode, this is where you register `mcp-bridge` itself.

Every JSON snippet below is labelled with which file it belongs to.

The bridge config uses the same `mcpServers` schema as Claude Desktop and other MCP hosts, plus top-level `port` and `host` for the HTTP endpoint:

```json
// mcp-bridge.json
{
  "port": 8808,
  "host": "127.0.0.1",
  "mcpServers": {
    "filesystem": {
      "command": "npx",
      "args": ["-y", "@modelcontextprotocol/server-filesystem", "/tmp"]
    },
    "qtcreator": {
      "type": "sse",
      "url": "http://127.0.0.1:3001/sse"
    }
  }
}
```

### Fields

**Top-level**

| Field | Type | Default | Description |
|---|---|---|---|
| `port` | int | `8808` | HTTP port to listen on |
| `host` | string | `127.0.0.1` | Bind address |
| `mcpServers` | object | required | Map of upstream server name → entry |

**Per-server entry**

Each entry describes one upstream MCP server. The shape of the entry depends on *how the bridge talks to that upstream*: as a child process (stdio) or over the network (HTTP/SSE). Pick one transport per entry — fields from the other transport are ignored.

Common fields for every entry:

| Field | Type | Default | Description |
|---|---|---|---|
| `type` | string | `"stdio"` | Upstream transport: `"stdio"`, `"sse"`, or `"http"` |
| `enable` | bool | `true` | Set to `false` to skip this entry without removing it |

**stdio entries** (`type: "stdio"`, the default) — bridge launches the upstream as a child process and talks over stdin/stdout:

| Field | Type | Description |
|---|---|---|
| `command` | string | Executable to launch (required) |
| `args` | array | Command-line arguments |
| `env` | object | Environment variables (merged with host env) |

Minimal template:

```json
"my-stdio-server": {
  "command": "npx",
  "args": ["-y", "@modelcontextprotocol/server-filesystem", "/tmp"]
}
```

`type` is omitted — it defaults to `"stdio"`. `url` / `httpSpec` / `headers` don't apply here.

**HTTP/SSE entries** (`type: "sse"` or `"http"`) — upstream is already running on the network; bridge connects to its URL. `"sse"` and `"http"` are treated identically; the actual wire revision is chosen by `httpSpec`, not by `type`:

| Field | Type | Description |
|---|---|---|
| `url` | string | Upstream endpoint URL (required) |
| `headers` | object | Additional HTTP headers sent with every request |
| `httpSpec` | string | MCP HTTP spec revision the upstream speaks. One of `"2024-11-05"`, `"2025-03-26"`, `"2025-06-18"`, `"2025-11-25"`, or `"latest"`. Empty / omitted = latest. |

Minimal template:

```json
"my-http-server": {
  "type": "http",
  "url": "http://127.0.0.1:29180/mcp",
  "httpSpec": "2025-11-25"   // Streamable HTTP (single /mcp endpoint)
}
```

`type` is required (otherwise the entry is treated as stdio and `command` will be missing). `command` / `args` / `env` don't apply here.

**Quick rule of thumb**

| Upstream is… | Required fields | Notes |
|---|---|---|
| a command you want the bridge to launch | `command` (+ `args`, `env`) | Don't set `type` — stdio is the default. `httpSpec` doesn't apply. |
| a server already running on a URL | `type` + `url` | `httpSpec` optional (defaults to latest); set it if the upstream speaks an older revision. |

---

The `type` field selects the transport family (HTTP vs stdio). `httpSpec` is the single knob that picks the wire-protocol revision inside the HTTP family:

- `"2024-11-05"` — legacy SSE transport (separate `/sse` + `POST /messages` endpoints). Conventionally written as `type: "sse"` for readability.
- `"2025-03-26"`, `"2025-06-18"`, `"2025-11-25"` (and `"latest"`) — Streamable HTTP transport (single `/mcp` endpoint). Conventionally written as `type: "http"`.

`"sse"` and `"http"` are interchangeable in code — use whichever names the upstream's actual shape best. If you don't set `httpSpec`, the bridge speaks the latest known revision. Match it to whatever the upstream MCP server expects — mismatched revisions show up as immediate `Transport closed` after the initial HTTP request.

## Common configurations

### Claude Desktop → stdio upstreams

Aggregate several stdio MCP servers behind one stdio endpoint that Claude Desktop talks to.

**Bridge config** — e.g. `bridge.json`, lists the upstreams:

```json
// bridge.json  (bridge config)
{
  "mcpServers": {
    "filesystem": {
      "command": "npx",
      "args": ["-y", "@modelcontextprotocol/server-filesystem", "/home/user"]
    },
    "git": {
      "command": "uvx",
      "args": ["mcp-server-git"]
    }
  }
}
```

**Client config** — `claude_desktop_config.json`, tells Claude Desktop to launch the bridge (no need to run `mcp-bridge` manually — the client starts it):

```json
// claude_desktop_config.json  (client config)
{
  "mcpServers": {
    "bridge": {
      "command": "/path/to/mcp-bridge",
      "args": ["--stdio", "/path/to/bridge.json"]
    }
  }
}
```

### Claude Desktop → HTTP upstream (e.g. MCP server embedded in a desktop app)

Claude Desktop only speaks stdio, but your MCP server lives inside a Qt/Electron/whatever app and exposes Streamable HTTP. Bridge translates.

**Bridge config:**

```json
// bridge.json  (bridge config)
{
  "mcpServers": {
    "myapp": {
      "type": "http",
      "url": "http://127.0.0.1:29180/mcp",
      "httpSpec": "2025-11-25"   // Streamable HTTP (single /mcp endpoint)
    }
  }
}
```

**Client config** — same `claude_desktop_config.json` snippet as in the previous section. The host app must be running for the bridge to reach the upstream.

### HTTP host → mixed upstreams

Run the bridge as an HTTP endpoint and put any combination of stdio and HTTP upstreams behind it.

**Bridge config:**

```json
// bridge.json  (bridge config)
{
  "port": 8808,
  "mcpServers": {
    "filesystem": {
      "command": "npx",
      "args": ["-y", "@modelcontextprotocol/server-filesystem", "/tmp"]
    },
    "qtcreator": {
      "type": "sse",
      "url": "http://127.0.0.1:3001/sse",
      "httpSpec": "2024-11-05"   // legacy split: GET /sse + POST /messages
    },
    "myapp": {
      "type": "http",
      "url": "http://127.0.0.1:29180/mcp",
      "httpSpec": "2025-11-25"   // Streamable HTTP (single /mcp endpoint)
    }
  }
}
```

Run the bridge yourself: `mcp-bridge bridge.json`. Clients then connect to `http://127.0.0.1:8808/mcp` (Streamable HTTP) — no client-side config file needed here, just point the client at that URL.

## Connecting clients

### HTTP mode (default)

The bridge serves MCP 2025 Streamable HTTP transport at `/mcp`. Example URL:

```
http://127.0.0.1:8808/mcp
```

Any MCP client that supports the HTTP transport can connect.

### Stdio mode

Launched with `--stdio`, the bridge speaks MCP over stdin/stdout. Plug it into a stdio-only host (e.g. Claude Desktop) by pointing the host at the bridge binary — this goes into the **client config** (e.g. `claude_desktop_config.json`), not the bridge config:

```json
// claude_desktop_config.json  (client config)
{
  "mcpServers": {
    "bridge": {
      "command": "/path/to/mcp-bridge",
      "args": ["--stdio", "/path/to/mcp-bridge.json"]
    }
  }
}
```

Tools from all upstream servers appear as a flat list in either mode. Tool names are taken as-is from upstream — **if two upstream servers expose a tool with the same name, the last registration wins**; either rename at source or run them on separate bridge instances.

## Behavior notes

- **Parallel init** — upstreams connect concurrently. The serving endpoint (HTTP or stdio) starts once *all* upstreams have either initialized or failed.
- **Failure tolerance** — if an upstream fails to initialize, its tools are skipped and the bridge continues with the rest. Check logs for `[name] init failed: ...`.
- **Auto-reconnect** — on `McpClient::disconnected` the upstream's tools are de-registered and `connectAndInitialize` + `tools/list` are retried with exponential backoff (1s → 30s). On success the tools are registered again.
- **Graceful shutdown** — on `Ctrl+C` (SIGINT) the bridge stops the serving transport, cancels pending reconnects, and sends MCP `shutdown` to each upstream (which terminates stdio child processes).
- **Tool changes** — each upstream's `notifications/tools/list_changed` triggers a re-sync: old tools are removed, the fresh list is fetched and re-registered.
- **Logs on stderr** — in stdio mode stdout is reserved for MCP frames; all bridge logs (`llmqore.mcp`, qInfo/qWarning) go to stderr.

## Current limitations

- **Only tools are proxied.** Resources, prompts, sampling and elicitation are not forwarded yet.
- **No tool-name collision handling.** Last registration wins.
- **No authentication.** The HTTP endpoint is open — bind to `127.0.0.1` (default) or put the bridge behind a reverse proxy with auth.

## See also

- [Integration](integration.md) — using llmqore from your own CMake project
- [MCP Protocol Coverage](mcp/mcp_protocol_coverage.md) — what llmqore's MCP implementation supports

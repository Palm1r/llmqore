# MCP Bridge

A standalone CLI tool that aggregates multiple MCP servers — stdio or HTTP/SSE — and re-exposes them either behind a single HTTP endpoint **or** as one stdio server. Built on top of LLMCore.

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

Download from [GitHub Releases](https://github.com/Palm1r/llmcore/releases):

- `mcp-bridge-linux-x86_64.tar.gz`
- `mcp-bridge-macos-universal.tar.gz` (x86_64 + arm64)
- `mcp-bridge-windows-x86_64.zip`

Each archive contains the `mcp-bridge` binary together with the Qt runtime it needs (DLLs on Windows, frameworks on macOS, `.so` files on Linux). Extract the archive and run `mcp-bridge` from inside — no separate Qt installation required.

### Build from source

The bridge is built as part of the llmcore CMake project:

```bash
cmake -B build -DLLMCORE_BUILD_MCP_BRIDGE=ON
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

The config file uses the same `mcpServers` schema as Claude Desktop and other MCP hosts, plus top-level `port` and `host` for the HTTP endpoint:

```json
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

| Field | Type | Default | Description |
|---|---|---|---|
| `type` | string | `"stdio"` | Upstream transport: `"stdio"`, `"sse"`, `"http"` / `"streamable-http"` |
| `enable` | bool | `true` | Set to `false` to skip this entry without removing it |

**stdio entries** (`type: "stdio"`, the default)

| Field | Type | Description |
|---|---|---|
| `command` | string | Executable to launch (required) |
| `args` | array | Command-line arguments |
| `env` | object | Environment variables (merged with host env) |

**HTTP/SSE entries** (`type: "sse"` / `"http"` / `"streamable-http"`)

| Field | Type | Description |
|---|---|---|
| `url` | string | Upstream endpoint URL (required) |
| `headers` | object | Additional HTTP headers sent with every request |

`"sse"` speaks the MCP 2024-11-05 SSE transport (separate `/sse` + `POST` endpoints).
`"http"` / `"streamable-http"` use the MCP 2025-03-26 Streamable HTTP transport.

## Connecting clients

### HTTP mode (default)

The bridge serves MCP 2025 Streamable HTTP transport at `/mcp`. Example URL:

```
http://127.0.0.1:8808/mcp
```

Any MCP client that supports the HTTP transport can connect.

### Stdio mode

Launched with `--stdio`, the bridge speaks MCP over stdin/stdout. Plug it into a stdio-only host (e.g. Claude Desktop) by pointing the host at the bridge binary:

```json
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
- **Logs on stderr** — in stdio mode stdout is reserved for MCP frames; all bridge logs (`llmcore.mcp`, qInfo/qWarning) go to stderr.

## Current limitations

- **Only tools are proxied.** Resources, prompts, sampling and elicitation are not forwarded yet.
- **No tool-name collision handling.** Last registration wins.
- **No authentication.** The HTTP endpoint is open — bind to `127.0.0.1` (default) or put the bridge behind a reverse proxy with auth.

## See also

- [Integration](integration.md) — using llmcore from your own CMake project
- [MCP Protocol Coverage](mcp/mcp_protocol_coverage.md) — what llmcore's MCP implementation supports

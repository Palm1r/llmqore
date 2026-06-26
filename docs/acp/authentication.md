# ACP agent authentication

ACP does **not** carry an API key in the protocol. The host never sends provider
credentials over the wire — each agent authenticates itself with its own credentials.
The host's only job is to (a) optionally run the ACP `authenticate` step, and (b) make
sure the agent process can see whatever credential it needs (usually via environment
variables).

## How it surfaces

- On `initialize` the agent returns `authMethods`. If it is **non-empty**, the host
  should call `AcpClient::authenticate(methodId)` before `session/new`.
- If the agent has **no usable credential**, calls fail with a JSON-RPC error
  `-32000 Authentication required` — typically at `session/prompt` (the first call that
  actually reaches the provider), even though `initialize` and `session/new` succeeded.

```
llmqore.rpc: --> request id=3 method=session/prompt
llmqore.rpc: <-- response id=3 error=Authentication required
```

## Claude Code adapter (`@agentclientprotocol/claude-agent-acp`)

This adapter wraps the Claude Agent SDK. Note: it advertises **`authMethods: []`** (empty),
so the ACP `authenticate` step does not apply — it authenticates purely from its
environment / stored credentials. The SDK looks for, in order:

- `CLAUDE_CODE_OAUTH_TOKEN` — a long-lived **subscription** token (no paid API key needed)
- `ANTHROPIC_API_KEY` — a paid API key from console.anthropic.com
- `ANTHROPIC_AUTH_TOKEN`
- otherwise the stored Claude Code login (macOS **Keychain** item `Claude Code-credentials`)

**The Keychain path is the gotcha.** When you run a GUI host (Finder / Qt Creator), the
spawned `node` process often cannot read the Keychain item that the `claude` CLI created,
so it falls through to "Authentication required" — even though `claude` works fine in your
terminal. The fix is to hand the agent an explicit token via the environment.

### Subscription (recommended — no API key)

```bash
claude setup-token        # logs in via your subscription, prints a long-lived token
```

Then make the agent process see `CLAUDE_CODE_OAUTH_TOKEN`:

| Where | How |
|---|---|
| **agents.json** (per-agent) | add an `env` block (keep the file private / out of VCS) |
| **Host process env** | e.g. Qt Creator → Projects → Run → Environment → add the var; the host forwards its environment to the agent |
| **Terminal** | `export CLAUDE_CODE_OAUTH_TOKEN=…` then launch the host from that shell |

agents.json form:

```json
{ "agents": [
  { "id": "claude", "name": "Claude Code (ACP)", "command": "npx",
    "args": ["-y", "@agentclientprotocol/claude-agent-acp"],
    "env": { "CLAUDE_CODE_OAUTH_TOKEN": "sk-ant-oat-…" } }
]}
```

The host forwards both its own process environment **and** the agent's `env` block (the
latter wins) into the launched process — see `AcpAgentConfig::toLaunchConfig()`.

### Gotcha: GUI launchers strip the shell environment

A GUI-launched app does **not** inherit your interactive shell's exports. Putting
`export CLAUDE_CODE_OAUTH_TOKEN=…` only in `~/.zshrc` will **not** reach the agent. Set it
where the host can actually see it: the run-configuration environment, the agent's
`env` block, or a `~/.zshenv` (which is always sourced) — not `~/.zshrc`.

## Other agents

Auth is agent-specific. Gemini CLI (`gemini --experimental-acp`) and Codex use their own
login / keys; consult each agent's docs and provide the relevant env vars the same way
(host process env or the agent's `env` block in `agents.json`).

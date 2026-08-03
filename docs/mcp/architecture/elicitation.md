# Elicitation architecture (`elicitation/create`)

Server → client direction for collecting structured information from end user. Same host-supplied-provider pattern as sampling. No UI — host owns the widget toolkit.

## Client side — BaseElicitationProvider

- Subclass and implement the elicitation handler, returning the result as a future.
- Or hand `CallbackElicitationProvider` a `std::function<ElicitResult(const ElicitRequestParams &)>` and skip the subclass. It answers synchronously through `readyFuture`, so it fits a host that can decide on the spot; a host that has to wait for a dialog to close wants the subclass and its own promise. A callback that is null answers `Cancel` rather than hanging the server.
- Register the provider on McpClient -- this installs the handler and declares `elicitation` capability.
- Without provider → `MethodNotFound` (-32601).
- Reply actions: `ElicitAction::Accept` / `Decline` / `Cancel`. `content` only on accept (serialiser enforces).
- Refuse with `Rpc::RemoteError(ErrorCode::InvalidParams, reason)` from future when no UI available. User "no" → `Decline`.

## Server side

- McpServer short-circuits with Rpc::ProtocolError when not initialized or client lacks `elicitation` capability.
- Sends a normal session request. Default timeout: 300 s (vs 120 s for sampling) -- human typing into form.

## Flow over HTTP

Identical to sampling: buffered when no matching client socket, piggybacked onto next client POST.

## URL mode (2025-11-25)

`ElicitRequestParams::mode = "url"` + `url` replaces `requestedSchema` for flows where server wants client to open a URL (e.g. OAuth consent). Host providers check `mode` first → browser-opening strategy. Empty `mode` = default "form" behaviour (compatible with 2025-06-18).

## Wire types and tests

`ElicitRequestParams`, `ElicitResult`, `ElicitationCapability`, `ElicitAction::{Accept,Decline,Cancel}` in `McpTypes.hpp`. Round-trip: `tst_McpTypes.cpp`. E2E: three `McpLoopbackTest.Elicitation*` cases.

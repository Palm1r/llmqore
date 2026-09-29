# Networking layer

Sits between provider clients and `QNetworkAccessManager`. Three goals:

1. Two shapes: buffered `HttpResponse` (one-shot) and streaming `HttpStreamHandle` (SSE).
2. Transport errors (`HttpTransportError`) vs HTTP status codes (`HttpResponse`) kept separate.
3. The two shapes are an interface (`HttpTransport`), not a class, so anything above it can be driven without a socket.

LLM-agnostic -- knows nothing about JSON, SSE events, MCP. Also backs the MCP HTTP client transports.

Authentication and request headers are not part of this layer: a fully-formed `QNetworkRequest` arrives here. `BaseClient` builds it from its own `AuthScheme` and header map (see [BaseClient contract](clients/base-client.md)); the MCP HTTP transports carry their own header map. The transport just sends what it is handed.

```mermaid
flowchart TD
    subgraph User["Caller"]
        C1["provider BaseClient subclass<br/>(or an MCP HTTP transport, ...)"]
    end

    subgraph Api["HttpTransport interface"]
        H1["Buffered request<br/>returns HttpResponse future"]
        H2["Streaming request<br/>returns HttpStreamHandle"]
    end

    subgraph Stream["Streaming reply"]
        S1["HttpStreamHandle<br/><small>HttpStream over QNetworkReply</small>"]
        S2["headersReceived"]
        S3["chunkReceived(bytes)"]
        S4["finished"]
        S5["errorOccurred(HttpTransportError)"]
        S1 --> S2
        S1 --> S3
        S1 --> S4
        S1 --> S5
    end

    subgraph Framers["Framing helpers"]
        F1["SSEParser<br/>(SSE events)"]
        F2["Rpc::LineFramer<br/>(JSON lines)"]
    end

    subgraph Qt["Qt underlayer"]
        Q1["QNetworkAccessManager"]
        Q2["QNetworkReply"]
    end

    C1 -->|one-shot| H1
    C1 -->|streaming| H2
    H1 --> Q1
    H2 --> Q1
    Q1 --> Q2
    Q2 --> S1
    C1 -.consumes.-> S3
    C1 -.pushes bytes into.-> F1
    C1 -.pushes bytes into.-> F2
```

---

## HttpTransport

The abstract seam every request passes through. It declares exactly what the layer above needs: a **buffered** send returning a future of `HttpResponse`, a **streaming** `openStream` returning an `HttpStreamHandle`, and the transfer timeout. Nothing else -- proxies, network managers, and reply objects belong to implementations.

`BaseClient` takes an `HttpTransport *` as an optional constructor argument (every provider client forwards it), and `McpStreamableHttpTransport` / `McpSseHttpTransport` take one on the same terms. A null transport means "create a private `HttpClient`"; a supplied transport stays owned by the caller. That is the only injection point -- there is no setter, so the transport cannot change under an in-flight request.

Tests use it to drive provider clients and MCP-over-HTTP end to end without a socket: `tests/FakeHttpTransport.hpp` records the outgoing `QNetworkRequest` and body, and hands back a stream the test writes arbitrary bytes, statuses, and terminal events into.

---

## HttpClient

The production `HttpTransport`. Wraps one `QNetworkAccessManager`. Must be used from the owning thread.

The **buffered** mode returns a future that resolves to an `HttpResponse` containing the status code, headers, and body. Any HTTP status (including 4xx/5xx) produces a valid response; only transport-level failures (DNS, timeout, SSL, abort, connection refused) propagate as exceptions. This mode is used for model listing, MCP HTTP transports, and non-streamed endpoints. The **streaming** mode returns a live `HttpStream` (caller takes ownership) used for all streamed LLM requests and HTTP MCP client transport.

Additional configuration includes proxy settings (forwarded to the underlying network manager) and a transfer timeout (default 120 seconds, can be disabled). The transport's timeout applies to requests that carry none of their own: a request with a non-zero `QNetworkRequest::transferTimeout()` keeps it, the same rule `QNetworkAccessManager` follows.

---

## HttpStreamHandle / HttpStream

`HttpStreamHandle` is the streaming reply contract: status code and raw headers, an abort, and a fixed signal sequence -- headers-received (after which status and headers are readable), zero or more chunk-received events carrying raw bytes, and exactly one terminal event, either a clean finish or a transport error. After an abort, neither terminal event fires.

`HttpStream` is the `QNetworkReply`-backed implementation, and additionally exposes content type, single-header lookup, and completion state.

---

## SSEParser

Incremental, spec-compliant (WHATWG HTML section 9.2) Server-Sent Events parser. Accepts byte chunks and returns completed events, each carrying a type (defaulting to "message"), data (multi-line joined), and an optional ID. Supports flushing at end-of-stream, clearing between independent streams, and formatting events for the inverse direction (used by `McpHttpServerTransport`). A configurable buffer size limit (default 16 MiB) protects against memory exhaustion.

Used by all providers except Ollama.

---

## Rpc::LineFramer

Newline framing for every JSON-lines protocol in the library: Ollama's REST stream, JSON-RPC over stdio (stdout and stderr), and the stdio MCP server transport. Accepts byte chunks and returns complete lines as `QByteArray`, holding any incomplete trailing bytes across calls, stripping CRLF and skipping blank lines. Intentionally separate from `SSEParser` by design. A configurable buffer size limit (default 16 MiB) drops a runaway line that never terminates.

Framing happens on bytes, never on decoded text. Both framers buffer `QByteArray` and hand raw bytes to the JSON parser, so a multi-byte UTF-8 sequence split across two network reads survives; decoding a partial chunk first would replace the split character with U+FFFD and lose it permanently.

---

## Error taxonomy

### Transport errors

`HttpTransportError` represents a transport-level failure that prevents an HTTP response from being received. It derives from `QException` and carries a human-readable message plus the underlying network error code. Transport errors cover DNS failures, timeouts, SSL errors, aborted connections, and connection refused. The dividing line between transport errors and HTTP responses is based on the network error code category -- low-level network failures are transport errors, while anything that produced an HTTP status code is delivered as an `HttpResponse`.

### HTTP error responses

Non-2xx HTTP responses are represented as normal `HttpResponse` values carrying the status code, headers, and body. Provider clients inspect these and run them through a provider-specific error parser to extract a meaningful error message. The default parser produces a message containing the HTTP status code and a truncated body snippet. Providers override this to handle their specific error envelope formats (Anthropic, OpenAI, Google, Ollama each have different shapes).

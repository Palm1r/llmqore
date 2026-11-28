# Working on LLMQore

LLMQore is a reusable Qt/C++ library, not an application or an agent runtime.
Keep its public interfaces usable from host applications and its implementation
independent of any particular UI, IDE, or LLM integration.

## Before changing code

- Read the relevant public headers in `include/LLMQore/`, implementation in
  `source/`, and existing tests in `tests/`. Follow established conventions
  rather than introducing a second abstraction for the same responsibility.
- Start with [the architecture index](docs/architecture.md) for subsystem
  documentation. Read [the threading contract](docs/threading.md) before
  changing asynchronous APIs, ownership, signals, or transport lifetimes.
- Keep changes scoped to the requested behavior. Do not perform unrelated
  refactors, formatting, dependency upgrades, or version bumps.

## Architecture and compatibility

- Preserve the separation between provider clients, conversation/content,
  networking, RPC, MCP, ACP, and the tool registry. Provider-specific wire
  formats belong in the corresponding provider implementation.
- Use the existing `HttpTransport` and `Rpc::Transport` interfaces at transport
  boundaries; keep protocol logic out of the HTTP layer. MCP and ACP share
  JSON-RPC infrastructure rather than duplicating it.
- Preserve the QObject ownership and thread-affinity contract. Use value types
  across asynchronous boundaries; do not retain references into mutable
  library-owned state across event-loop iterations. Consider cancellation,
  reentrancy, destruction, and exactly-once completion.
- Use the existing future compatibility helpers for Qt 5 and Qt 6. The library
  supports C++20; the Qt 5 unit-test target uses C++17. Do not silently rely on
  Qt 6-only APIs or change the supported versions.
- Treat `include/LLMQore/` as public API. Avoid unrequested source or binary
  compatibility breaks; document intentional changes and update affected
  examples, architecture notes, or migration documentation as appropriate.
- Follow the repository's `.clang-format` and nearby naming and formatting.
  Reuse existing CMake targets and dependencies.

## Build and test

Use an out-of-source build with a Qt installation available to CMake:

```sh
cmake -S . -B build -G Ninja \
  -DCMAKE_BUILD_TYPE=Debug \
  -DLLMQORE_BUILD_TESTS=ON \
  -DLLMQORE_BUILD_EXAMPLES=OFF \
  -DLLMQORE_BUILD_MCP_BRIDGE=OFF
cmake --build build --target LLMQoreUnitTests
ctest --test-dir build --output-on-failure
```

Set `CMAKE_PREFIX_PATH` to the selected Qt installation if necessary.
Use separate build directories and `-DQT_VERSION_MAJOR=5` or
`-DQT_VERSION_MAJOR=6` when testing both versions. Enable the examples or MCP
bridge and build them too when a change affects those targets.

- Add a regression test for a bug fix and behavior tests for new functionality.
  Prefer the existing GoogleTest fixtures, fake transports and local loopback
  servers; unit tests must not require live providers, credentials or Internet access.
- Run the affected tests first, then the full unit suite for code changes.
  For lifecycle or intermittent failures, repeat the relevant test and preserve
  the evidence. Do not remove assertions or suppress warnings just to pass CI.
- Check [the CI workflow](.github/workflows/build_and_test.yml) for the actual
  platform and Qt-version matrix. Report what was run locally, what passed in
  CI, and what could not be verified. Documentation-only changes need not
  rebuild the library unless they alter runnable examples.

## Git and pull requests

- Work on a dedicated branch; never commit directly to `main`. Preserve
  unrelated local changes and do not rewrite shared history.
- Use focused commits with clear messages (for example, `fix: ...`,
  `feat: ...`, `refactor: ...`). Describe the change, not the tool used to
  produce it.
- Review the diff before pushing. Open a PR against `main`, explain the
  motivation and trade-offs, include test results and any known limitations,
  and leave merging to the maintainer.
- Keep credentials, local machine settings and generated build artifacts out
  of the repository.

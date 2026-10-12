# Integration

## FetchContent

```cmake
include(FetchContent)
FetchContent_Declare(
    LLMQore
    GIT_REPOSITORY https://github.com/palm1r/llmqore.git
    GIT_TAG v0.9.0
)
FetchContent_MakeAvailable(LLMQore)

target_link_libraries(YourApp PRIVATE LLMQore::LLMQore)
```

Upgrading from 0.8? See [Migrating to 0.9](migration-0.9.md).

## Installed

```bash
cmake -B build -DLLMQORE_INSTALL=ON
cmake --build build
cmake --install build --prefix /usr/local
```

```cmake
find_package(LLMQore REQUIRED)
target_link_libraries(YourApp PRIVATE LLMQore::LLMQore)
```

## Building from source

```bash
cmake -B build -DCMAKE_PREFIX_PATH=/path/to/Qt/6.x.x/gcc_64
cmake --build build
```

Tests and examples:

```bash
cmake -B build -DLLMQORE_BUILD_TESTS=ON -DLLMQORE_BUILD_EXAMPLES=ON
cmake --build build
ctest --test-dir build
```

The GUI example needs Qt Quick and is Qt 6 only. Under Qt 5.15 configure with
`-DLLMQORE_BUILD_EXAMPLES=OFF`; the library, the unit tests and `mcp-bridge` build either
way.

## Options

| Option | Default | Effect |
|---|---|---|
| `LLMQORE_INSTALL` | on when top-level | generate install targets |
| `LLMQORE_BUILD_TESTS` | on when top-level | build `LLMQoreUnitTests` |
| `LLMQORE_BUILD_EXAMPLES` | on when top-level | build `example-chat`, `example-mcp-server`, `example-mcp-http-probe` |
| `LLMQORE_BUILD_MCP_BRIDGE` | on when top-level | build the `mcp-bridge` CLI |
| `LLMQORE_BUILD_INTEGRATION_TESTS` | off | build tests that need live credentials |
| `BUILD_SHARED_LIBS` | CMake default | static build defines `LLMQORE_STATIC_LIB` for consumers |

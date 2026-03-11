# Integration

## FetchContent

```cmake
include(FetchContent)
FetchContent_Declare(
    LLMCore
    GIT_REPOSITORY https://github.com/palm1r/llmcore.git
    GIT_TAG v0.1.0
)
FetchContent_MakeAvailable(LLMCore)

target_link_libraries(YourApp PRIVATE LLMCore::LLMCore)
```

## Installed

```bash
cmake -B build -DLLMCORE_INSTALL=ON
cmake --build build
cmake --install build --prefix /usr/local
```

```cmake
find_package(LLMCore REQUIRED)
target_link_libraries(YourApp PRIVATE LLMCore::LLMCore)
```

## Building from source

```bash
cmake -B build -DCMAKE_PREFIX_PATH=/path/to/Qt/6.x.x/gcc_64
cmake --build build
```

Tests and examples:

```bash
cmake -B build -DLLMCORE_BUILD_TESTS=ON -DLLMCORE_BUILD_EXAMPLES=ON
cmake --build build
ctest --test-dir build
```
# slipstream

## Build

```sh
cmake --preset debug
cmake --build --preset debug
./build/debug/slipstream
```

Requires CMake 3.25+ and a C++23 compiler (GCC 14+, Clang 17+).

The standard is set per target in `CMakeLists.txt`, so it applies to this
project's compile commands only. To build with a different compiler without
changing anything system-wide, create a `CMakeUserPresets.json` (gitignored):

```json
{
    "version": 6,
    "configurePresets": [
        {
            "name": "local",
            "inherits": "debug",
            "cacheVariables": {
                "CMAKE_CXX_COMPILER": "/usr/bin/g++-14"
            }
        }
    ]
}
```

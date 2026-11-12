# Building on Nix

This document provides users with instructions on how to compile and test `mod_tile` with [Nix](https://nixos.org).

It is an additional option next to the CMake build: `nix/package.nix` builds the sources of this repository
against the dependencies from `nixpkgs`, including the `ctest` suite. `default.nix` and `shell.nix` are thin
wrappers around it.

```shell
# Build and test
nix-build ./default.nix

# Development shell with all build and test dependencies
nix-shell ./shell.nix
cmake -B build -S . -DENABLE_TESTS=ON
cmake --build build
ctest --test-dir build
```

The `nixpkgs` to build against can be passed as an argument:

```shell
nix-build ./default.nix --arg pkgs 'import <nixpkgs> { }'
```

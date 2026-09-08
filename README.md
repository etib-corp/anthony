# ANTHony

ANTHony

## What It Provides

-

## Key Concepts


## Quickstart

ANTHony is a library; it is consumed by an application (such as XIDER) that
provides a platform implementation. A minimal setup selects one backend and one
platform:

```sh
cmake -S . -B build
cmake --build build
```

## Building

### Dependencies

- CMake 3.10+
- A C++20 compiler
- Doxygen + Graphviz (only for `-DBUILD_DOCS=ON`)

### Configure and build

Select exactly one backend and one platform:

```sh
cmake -S . -B build \
  -DBUILD_TESTING=ON
cmake --build build
ctest --test-dir build --output-on-failure
```

Optional options:

- `-DBUILD_TESTING=ON` — build and run the test suite.
- `-DBUILD_DOCS=ON` — build Doxygen documentation.
- `-DBUILD_BENCHMARKS=ON` — build the benchmark harness.

## Consuming as a dependency

Once installed, downstream projects can use `find_package(anthony)`:

```sh
cmake --install build --prefix /path/to/prefix
```

```cmake
find_package(anthony REQUIRED)
target_link_libraries(my_app PRIVATE anthony::anthony)
```

## Documentation

- [How ANTHony Works](docs/HOW_ANTHONY_WORKS.md)
- [Architecture](docs/ARCHITECTURE.md)
- [Getting Started](docs/GETTING_STARTED.md)
- [Versioning & Support](docs/VERSIONING.md)
- [Technical Choices](docs/TECHNICAL_CHOICES.md)
- [Code Conventions](docs/CODE_CONVENTIONS.md)
- [Commit Conventions](docs/COMMIT_CONVENTIONS.md)

## Contributing

We welcome contributions from the community! If you're interested in
contributing to ANTHony, please check out our
[Contributing Guidelines](CONTRIBUTING.md) for more information on how to get
involved.

## License

ANTHony is released under the [MIT License](LICENSE). See
[CODE_OF_CONDUCT.md](CODE_OF_CONDUCT.md) for our community standards and
[SECURITY.md](SECURITY.md) for reporting vulnerabilities.

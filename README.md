# Paren

Forked and bootstrapped from the original [paren](https://bitbucket.org/ktg/paren/src/master/).

## Requirements

- LLVM (at least v18.1.4)
- C++20 complient compiler (much of this was only tested with clang)
- [black](https://github.com/psf/black) (for formatting)
- [lit](https://pypi.org/project/lit/) (for testing)
- `python3`

## Building

This can be built with a clang+llvm toolchain that provides:

- clang
- llvm-config (and core llvm libs)
- FileCheck (for testing)
- clang-format (optional)

The llvm release page for [v18.1.4](https://github.com/llvm/llvm-project/releases/tag/llvmorg-18.1.4) provides a handful of prebuilt toolchains.

Assuming you have the toolchain from above, you can build the project with just the `Toolchain.cmake` file:

```sh
$ mkdir build
$ cd build
$ cmake -GNinja -DCLANG_PREFIX=/path/to/clang/toolchain/bin/ -DCMAKE_TOOLCHAIN_FILE=../Toolchain.cmake ..
$ ninja
```

## Testing

Install the required python packages:

```sh
$ pip install -r requirements.txt
```

Then in the build directory:

```sh
$ ninja check
```

## Formatting

Install the required python packages:

```sh
$ pip install -r requirements.txt
```

Then in the build directory:

```sh
$ ninja format
```

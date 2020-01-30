# u6a

[![license](https://img.shields.io/badge/licence-GPLv3-blue.svg)](LICENSE)

Implementation of Unlambda, an esoteric programming language.

## Description

The u6a project provides a C implementation of the [Unlambda](http://www.madore.org/~david/programs/unlambda/) programming language, where Unlambda source code can be compiled into bytecode and executed in a virtual machine.

Ideas behind this implementation can be found [here](https://).

## Requirements

* A POSIX-compliant operating system
* GNU Autotools
* GNU Make
* A C compiler (with C99 support)

## Build

```bash
# (If not already) Install the required build tools.
sudo apt install build-essential automake

# Generate configure files.
autoreconf --install

# Run configure script with desired options.
./configure --prefix=$HOME

# Compile sources and generate executables.
make

# (Optional) Run tests.
make check

# (Optional) Install executables and man pages.
make install
```

## Usage

First, compile an Unlambda source file into bytecode.

```bash
u6ac -o foo.unl.bc foo.unl
```

Then, execute the bytecode file with the interpreter.

```bash
u6a foo.unl.bc
```

See [**u6ac**(1)](man/u6ac.1) and [**u6a**(1)](man/u6a.1) man pages for details.

## TODOs

* `u6adb` - An interactive debugger for Unlambda
* More compile-time optimizations

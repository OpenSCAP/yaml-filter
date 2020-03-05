# Developer's Guide

## Building on Linux

If you want to build the `libyaml-path` library and `yamlp` filtering utility follow the these instructions:


### 1. Get the source code

```sh
$ git clone https://github.com/OpenSCAP/yaml-filter.git
$ cd yaml-filter
```


### 2. *Get the build dependencies*

To build the library you will also need to install the build dependencies.

The project relies on CMake (`cmake`) build system and C99 compiler (for example `gcc`).

The only mandatroy dependency of `libyaml-path` is the YAML document parser/emitter library `libyaml`. You can also use `lcov` for coverage reports, but it is optional.

```sh
# Ubuntu
$ sudo apt-get install -y libyaml-0-2
```

```sh
# Fedora
$ sudo dnf install -y libyaml lcov
```

When you have all the build dependencies installed you can build the library.


### 3. *Build the project*

Run the following commands to build the library and filtering utility:

```sh
$ mkdir -p build
$ cd build/
$ cmake ..
$ make
```


### 3. *Run the tests*

Now you can execute the following command to run library self-checks:

```sh
$ ctest
```


### 4. *Install*

Run the installation procedure by executing the following command:

```sh
$ make install
```

You can also configure CMake to install everything into the $HOME/.local directory:

```sh
$ cd build
$ rm -rf *
$ cmake -DCMAKE_INSTALL_PREFIX=$HOME/.local ..
$ make
$ make install
```


### 5. *Generate code coverage report*

You can use `lcov` for code coverage report generation. It is integrated into the project's build configuration:

```sh
$ cd build
$ cmake -DENABLE_COVERAGE=yes ..
$ make && ctest -V
$ make gcov
```
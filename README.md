# RuiAnalysis

## Configurations

### Install Packages

```sh
brew install cmake # cmake
brew install ninja # ninja
brew install llvm # llvm
```

### Set Environment Variables

```sh
# Headers and libraries
export CPPFLAGS="-I/opt/homebrew/opt/llvm/include"
export LDFLAGS="-L/opt/homebrew/opt/llvm/lib"

# Mac SDKs
export SDKROOT=$(xcrun --show-sdk-path)
```

### IDE

[Clion](https://www.jetbrains.com/clion/)

**CMake Configurations**

```
Clang_DIR = /opt/homebrew/Cellar/llvm/21.1.2/lib/cmake/clang
LLVM_DIR = /opt/homebrew/Cellar/llvm/21.1.2/lib/cmake/llvm
```

![Clion_CMake](./figures/Clion_CMake.png)

**Toolchains Configurations**

```
C Compiler = /opt/homebrew/Cellar/llvm/21.1.2/bin/clang
C++ Compiler = /opt/homebrew/Cellar/llvm/21.1.2/bin/clang++
```

![Clion_Toolchains](./figures/Clion_Toolchains.png)

## Instructions

Build the project and copy compile_commands.json from cmake-build-debug to root.

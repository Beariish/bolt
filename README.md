# ⚡ Bolt
A *lightweight*, **lightning-fast**, type-safe embeddable language for real-time applications. 

```ts
import print, error, Error from core
import abs, epsilon from math

// The return type of safe_divide is inferred to be `Error | number`
fn safe_divide(a: number, b: number) {
    if abs(b) < epsilon {
        return error("Cannot divide by zero!")
    }

    return a / b
}

match let result = safe_divide(10, 5) {
    is Error {
        // The type of result is narrowed in this branch!
        print("Failed to divide:", result.what)
    }

    is number {
        print("The answer is", result)
    }
}
```

## Features
* [Lightning-fast performance](https://github.com/Beariish/bolt/blob/main/doc/Bolt%20Performance.md), outperforming other languages in its class
* Compact implementation, leaving a minimal impact on build size while remaining consise enough to browse. 
* Blazingly quick compilation, plow through code at over 500kloc/thread/second. That's 50'000 lines in the blink of an eye.
* Ease of embedding, only a handful of lines to get going
* Rich type system to catch errors before code is ran, with plenty of support for extending it from native code
* Embed-first design, prioritizing inter-language performance and agility  

## Links
* **[Bolt programming guide](https://github.com/Beariish/bolt/blob/main/doc/Bolt%20Programming%20Guide.md)**
* **[Bolt standard library reference](https://github.com/Beariish/bolt/tree/main/doc/Bolt%20Standard%20Library)**
* **[Bolt embedding and API reference](https://github.com/Beariish/bolt/tree/main/doc/Bolt%20Embedding%20Guide.md)**
* **[Bolt performance](https://github.com/Beariish/bolt/blob/main/doc/Bolt%20Performance.md)**
* **[Notable Bolt users](https://github.com/Beariish/bolt/blob/main/doc/Bolt%20Users.md)**

## Dependencies 
Bolt only depends on the C standard library as well as `libm` on Unix-based ssytems.
Some standard library modules include things like file and system IO, but these can be disabled easily.
By default, Bolt sets up an environment that uses `malloc`/`realloc`/`free`, but this is also easy to configure.

## Building
Bolt currently only builds on x64. 32-bit architectures are explicitly not supported, arm and riscv are untested.
Running `cmake` in the root directory of the project will generate a static library for the language, as well as the CLI tool.
For more information and options regarding embedding Bolt in your application, see `bt_config.h`.
See below for the status of Bolt on each relevant compiler. 

## Compiler Status
Please note that Bolt is **not** yet stable, expect to encounter compiler bugs and crashes. If you do, opening an issue with replicable Bolt code would be much appreciated 😊

[![Build Status](https://github.com/Beariish/bolt/actions/workflows/cmake-multi-platform.yml/badge.svg)](https://github.com/Beariish/bolt/actions/workflows/cmake-multi-platform.yml)
| Compiler | Status | Reason |
| -------- | ------ | ------ |
| MSVC     | ✅     | no issues |
| GCC      | ✅🟨  | all functional, some warnings |
| Clang    | ✅🟨  | all functional, some warnings |

## Contributing
Bolt is a very opinionated project, and any contributions should take the vision into account.

Bugfixes are likely to be accepted as long as they're within reason and don't change any expected behaviour. Adding tests in case of regression is very much appreciated as well. A clean run of `/tests/all` is expected of course.

Optimizations may also be accepted for minor versions under similar criteria. A before/after run of `/benchmarks/all` is expected to evaluate the impact and make sure nothing else regresses. If the specific optimization isn't captured in any existing benchmark, adding one is required.

Feature additions will need a lot of consideration, Bolt is very intentionally minimal in its' design and featureset. I highly suggest you submit some kind of proposal or plan before starting any significant work on a feature to review. Use cases, performance, and implementation cost will all be expected to be justified.

## License
Bolt is licensed under MIT. See LICENSE for more information.

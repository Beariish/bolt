# ⚡ Bolt
A *lightweight*, **lightning-fast**, type-safe embeddable language for real-time applications. 

```rust
import print from core

fn speak(animal: string) {
	if animal == "cat" { return "meow" }
	else if animal == "dog" { return "woof" }
	else if animal == "mouse" { return "squeak" }
	
	return "nothing!"
}

let const animals = [ "cat", "dog", "mouse", "monkey" ]
for const animal in animals.each() {
	print(animal, "says", speak(animal))
}
```

## Features
* Lightning-fast performance, outperforming other languages in its class
* Compact implementation, entire library <10kloc and compiles to <200kb
* Ease of embedding, only a handful of lines to get going
* Rich type system to catch errors before code is ran, with plenty of support for extending it from native code
* Embed-first design, prioritizing inter-language performance and agility  

## Links
* **[Bolt programming guide](https://github.com/Beariish/bolt/blob/main/doc/Bolt%20Programming%20Guide.md)**
* **Bolt standard library reference**
* **Bolt embedding and API reference**
* **Bolt performance**
* **Notable Bolt users**

## Building
Bolt currently only builds on x64. 32-bit architectures are explicitly not supported, arm and riscv are untested.
Running `cmake` in the root directory of the project will generate a static library for the language, as well as the CLI tool.
For more information and options regarding embedding Bolt in your application, see `bt_config.h`.
See below for the status of Bolt on each relevant compiler. 

## Compiler Status
[![Build Status](https://github.com/Beariish/bolt/actions/workflows/cmake-multi-platform.yml/badge.svg)](https://github.com/Beariish/bolt/actions/workflows/cmake-multi-platform.yml)
| Compiler | Status | Reason |
| -------- | ------ | ------ |
| MSVC     | ✅     | no issues |
| GCC      | ✅🟨  | all functional, but some warnings |
| Clang    | ⚙️     | compiles, but runtime is fishy. would appreciate help. |

## Contributing
Bug reports and fixes are of course welcome, and changes/improvements to the embedding side and C api will generally be considered. Performance improvements are likely to be accepted as long as they're repeatable in a case defined in the `benchmarks/` directory, and don't come at some other significant cost.

Actual language features and extensions will require a lot more consideration, discussion, and revision before any real work should be started. There is a farily clear vision and roadmap for the language, and I explicitly want to avoid bloating it with features that don't further its' goals. 

Standard library improvements and extensions are accepted as long as they don't bring in an external dependency, and aren't too niche/overlapping not to make sense.

## Roadmap
* 0.1.x - Bugfixing and stabilization, debugability *[we are here]*
* 1. Focus on improving code quality, reducing duplication and some loving comments
* * - 
* 0.2.x - Compile to bytecode and bytecode bundles
* 0.3.x - Generics and arrow functions
* 0.4.x - Fibres and coroutines
* 0.5.x and beyond - ? 

## License
Bolt is licensed under MIT. See LICENSE for more information.
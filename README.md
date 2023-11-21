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
* **Bolt programming guide**
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
| Compiler | Status | Reason |
| -------- | ------ | ------ |
| MSVC     | ✅     | no issues |
| GCC      | ✅🟨  | all functional, but some warnings |
| Clang    | ⚙️     | compiles, but runtime is fishy. would appreciate help. |

## Contributing
Some notes about how to contribute to bolt, what is and is not likely to be accepted, etc

## Roadmap
* 0.1.x - Bugfixing and stabilization *[we are here]*
* 0.2.x - Compile to bytecode and bytecode bundles
* 0.3.x - Generics and arrow functions
* 0.4.x - Fibres and coroutines
* 0.5.x and beyond - ? 

## License
Bolt is licensed under MIT. See LICENSE for more information.
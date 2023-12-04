# Bolt performance

Bolt is fast. It's a large part of the driving factor behind the language, and performance informs every decision made in its development. The benchmarks below are certainly artificial, but they're highly repeatable and each highlight a different aspect of language peformance.

All tests are performed on the same machine, running an AMD Ryzen 9 3900X and 32GB of DDR4 3200MTs in dual channel mode.
The implementations for all of these can be found in `benchmarks/` for Bolt, and `benchmarks/_foreign` for everything else.

Only interpreted languages are compared against here, even when the runtime supports JIT compilation. JITted languages are in a class of their own, and come with a lot of implications. (Warm-up time, unavailible in sandboxed environments etc). `Luau -O2` is only included in benchmarks where it makes a measurable difference.

Bolt is built with MSVC using the exact configuration in the CMake file provided in the repo. Every other language is built to recommended build instructions, or using a provided pre-compiled binary, depending on which is faster.

---

This benchmark highlights the cost of invoking a closure in each language, implementing the simplest iterator for an empty loop body. This is the lowest-overhead case I can think of to do this, in Bolt it compiles to literally just calling the closure, and then conditionally exiting the loop.
<p align="center">
    <img src="https://github.com/Beariish/bolt/blob/main/doc/_images/Closure%20iterator%20performance.png"></img>
</p>

---

This benchmark computes a mandelbrot set. It doesn't render it out to an image or anything, though a rolling sum of pixel values is collected to ensure correctness. This is very arithmetic-heavy, with a decent amount of function invocations as well (especially to sqrt). Function inlining would potentially provide a huge boost here.
<p align="center">
    <img src="https://github.com/Beariish/bolt/blob/main/doc/_images/Mandelbrot.png"></img>
</p>

---

The next few benchmarks are somewhat memory-bound, which is why Bolt gets two entries. The performance of Bolt with the system allocator varies a lot depending on OS and toolchain. Building with mimalloc (which takes only 3 lines of code to configure Bolt for) eliminates this variance and lets us focus purely on language performance. Bolt's internal allocator is still very, very simple, and implementing a more complicated middle layer would help alleviate the issues with making lots of system allocations. This is what the other fast languages do in this benchmark. 
<p align="center">
    <img src="https://github.com/Beariish/bolt/blob/main/doc/_images/Vec2%20create%20create%20add.png"></img>
</p>

---

This benchmark only makes a single allocation per iteration, and instead measures both method invocation and object setup. 
<p align="center">
    <img src="https://github.com/Beariish/bolt/blob/main/doc/_images/Vec2%20add.png"></img>
</p>

---

This calls the same method repeatedly on a pre-allocated object, purely benchmarking the invocation performance.
<p align="center">
    <img src="https://github.com/Beariish/bolt/blob/main/doc/_images/Vec2%20distance.png"></img>
</p>

# Why is Bolt fast?
### Nanboxing
Bolt makes use of [nanboxing](https://github.com/zuiderkwast/nanbox) in order to keep value representations nice and compact. This means that every stack-stored value in bolt is 8 bytes wide. Unlike some implementations though, Bolt (quite aptly) uses qNaN to mean "this is not a number", leaving actual double values completely unmasked. Coupled with the type system, this makes arithmetic very fast. 

There are only a few primitive types that can be stored directly in the nanbox (number, null, boolean, enum), with the rest being relegated to an "object" type that makes a second hop to a heap-allocated GC header. It may be interesting to explore separating strings into their own primitive type, or inlining the tags for arrays and tables as well (there's a whole unused bit there!), but it's unclear how much value this would provide.

### Register based VM
Bolt's VM uses register-style stack addressing when executing its bytecode, which is generally the approach preferred by other high-performance embedded environments as well. The main reason for this is quite simple: you can encode more information in a single instruction. From a high level, this means your dispatch loop will spend less time decoding instructions and reading the next, and more time performing actual useful work. 

Consider the following Bolt function:
```rust
fn add(a: number, b: number) { return a + b }
```

In a stack-based VM, this may compile down to something like:
```rust
PUSH 0 // [a]
PUSH 1 // [b, a]
ADD    // [a+b]
RETURN
```

While in current bolt, it actually becomes:
```rust
ADD    2, 0, 1 // result = a + b
RETURN 2       // return result
```

Not only is this two instructions shorter, but the stack-based approach involves two extra copies (pushing a and b to the stack), but the `ADD` and `RETURN`` instructions both encode 0 arguments, either leaving gaps in the instruction stream if constant-sized, or requiring variable-read dispatch.

### Typesafety and accelerated bytecode
Bolt's type system is a large factor behind it's impressive performance. Despite the compiler still being in it's early stages, having basic checking in place lets it emit smarter sequences of instructions, or perform work at compile-time that would otherwise need to be repeated.

The clearest example of this is that many instructions in Bolt make use of the "acceleration bit", which when set tells the VM to take a fast path for that instruction, often forgoing doing any runtime type checking or hashtable lookups. Coupled with unmasked nanbox doubles, this means things like arithmetic often compile down to a single native instruction.

### Imports
Bolt has a more formal import/export system than many dynamic languages. This is in part due to the type system, needing them to be resolved during compilation in order to facilitate checking, but it also bakes into the compilation model of Bolt functions. Every function keeps a reference to the module it was defined in, which in turn holds references to all the imports in that module. This means functions do not need to dynamically capture their imports, avoiding closure invocations, and are able to linearly address them in the import array instead of making some kind of environment lookup.

### Setupless calling convention
Once again thanks to the type system in Bolt, the calling convention can be made a lot simpler than in other dynamic languages. You cannot invoke a function with an incorrect number of arguments, the compiler knows ahead of time whether the function returns any values, and where to store them. This makes the setup for every function call very light, and the cleanup afterwards literally nonexistent. 

### Inline allocations
Bolt makes use of inline table allocations whenever the shape of the table is known at compiletime, making for a single allocation instead of needing to make two jumps in memory to fetch the contents. This not only makes lookup faster, but the allocation/freeing of tables as well. In most circumstances, for hard-typed table creation (via `=>` operator), the type contains a table template as well, with all keys pre-hashed and put into the correct slots, so creating the table becomes a single alloc + memcpy. 

### String interning
Bolt deduplicates strings through interning, performing a hash on the character data if the strings length is beneath a certain threshold (`32`, currently, derived through testing), and searching for it in a global string deduplication table before allocating a new object. Allocations are costly, and for some non-trivial tasks (see `examples/json.bolt`) it provides a very significant speedup.
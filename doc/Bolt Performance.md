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

The next few benchmarks are somewhat memory-bound, which is why Bolt gets two entries. The performance of Bolt with the system allocator varies a lot depending on OS and toolchain. Building with mimalloc (which takes only 3 lines of code to configure bolt for) eliminates this variance and lets us focus purely on language performance. Bolt's internal allocator is still very, very simple, and implementing a more complicated middle layer would help alleviate the issues with making lots of system allocations. This is what the other fast languages do in this benchmark. 
<p align="center">
    <img src="https://github.com/Beariish/bolt/blob/main/doc/_images/Vec2%20create%20create%20add.png"></img>
</p>

---

This benchmark only makes a single allocation per iteration, as opposed to 3, and instead relies more on method invocation and object setup. 
<p align="center">
    <img src="https://github.com/Beariish/bolt/blob/main/doc/_images/Vec2%20add.png"></img>
</p>

---

This calls the same method repeatedly on a pre-allocated object, purely benchmarking the performance of object lookups.
<p align="center">
    <img src="https://github.com/Beariish/bolt/blob/main/doc/_images/Vec2%20distance.png"></img>
</p>
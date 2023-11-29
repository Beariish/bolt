# Core
The `core` module contains a set of miscellaneous functions useful to just about any Bolt script. It's meant to be trivially includable without needing to consider what it leaks.

---

```rust
core.print(params: ..any)
```
Iterates over all parameters, converts them to strings, and concatenates them with spaces before printing to stdout, ending with a newline.

---

```rust
core.write(params: ..any)
```
The same thing as `core.print`, except without the trailing newline.

---

```rust
core.sameline()
```
Simply outputs a carraige return to let the program overwrite the same line in the console.

---

```rust
core.throw(reason: string)
```
Throws a runtime error, which immediately suspends the execution of the current Bolt thread, and invokes the native error callback with the reason provided.

---

```rust
core.to_string(obj: any): string
```
Converts the object passed to a string, invoking the `@format` metamethod on table parameters if present.

---

```rust
core.to_number(str: string): number?
```
Attempts to parse `str` into a number, ignoring any trailing content. Returns `null` if no conversion was possible.

---

```rust
core.time(): number
```
Returns a timepoint in microseconds. This is not guarenteed to be relative to any specific time, and instead is meant to compare to snapshots against each other.

---

```rust
type Error = unsealed {
    what: string
}

core.error(string: what): Error
```
`Error` is the standard error type for Bolt, and the `core.error` function exists to conveniently make one.
Functions that can fail (and where returning null is insufficient for context) often return `T | Error`.

---

```rust
core.protect(to_exec: fn(..T): R, args?: ..T): R | Error
```
Wraps a given function in a protected call, internally creating a new thread to execute it on. This captures runtime errors, such as those given by `core.throw()`, and safely returns them to the caller instead. If `to_exec` takes no arguments, `?args` can be left off.

---

```rust
core.assert(result: T | Error, reason?: string): T
```
Asserts that `result` is not of type `Error`, throwing a runtime error with the optional context `reason` if it is. Otherwise, safely return the underlying value of `result` to the caller.
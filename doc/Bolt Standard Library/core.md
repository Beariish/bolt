# Core
The `core` module contains a set of miscellaneous functions useful to just about any Bolt script.

## Types
```ts
type Error = unsealed {
    what: string
}
```

## Functions

```ts
// Iterates over all parameters, converts them to strings, and concatenates 
// them with spaces and then prints to stdout, ending with a newline.
core.print(params: ..any)

// The same thing as `core.print`, except without the trailing newline.
core.write(params: ..any)

// Simply outputs a carriage return to let the program overwrite the same line in the console.
core.sameline()

// Throws a runtime error, which immediately suspends the execution of the current Bolt thread, 
// and invokes the native error callback with the reason provided.
core.throw(reason: string)

// Converts the object passed to a string, invoking the `@format` metamethod on table parameters if present.
core.to_string(obj: any): string

// Attempts to parse `str` into a number, ignoring any trailing content. Returns `null` if no conversion was possible.
core.to_number(str: string): number?

// Returns a timepoint in microseconds. This is not guarenteed to be relative to any specific time, 
// and instead is meant to compare snapshots against each other.
core.time(): number

// Creates an `Error` table with the given reason.
// Functions that can fail (and where returning null is insufficient for context) often return `T | Error`.
core.error(string: what): Error

// Wraps a given function in a protected call, internally creating a new thread to execute it on. 
// This captures runtime errors, such as those given by `core.throw()`, and safely returns them to the caller instead. 
// If `to_exec` takes no arguments, `args?` can be left off.
core.protect(to_exec: fn(..T): R, args?: ..T): R | Error

// Asserts that `result` is not of type `Error`, throwing a runtime error with the optional context `reason` if it is.
// Otherwise, safely return the underlying value of `result` to the caller.
core.assert(result: T | Error, reason?: string): T
```
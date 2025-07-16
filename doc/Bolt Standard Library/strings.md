# Arrays
Module containing utility functions for working with strings. All functions in this module are availible both as imports and exposed directly on the `string` prototype. That means that these are equal:
```ts
import strings

let str = "hello!"

strings.length(str) // 6
str.length() // 6
```

## Functions
```ts
// Returns the length of the string in characters
strings.length(str: string): number

// Creates a substring from `str`, starting from `start` for `len` characters
/** Example:
    let full = "Hello there!"
    let sub = full.substring(2, 3)
    print(sub) // llo
*/
strings.sustring(str: string, start: number, len: number): string

// Creates a substring from `str`, starting from `start` and going to the end of the original string
strings.remainder(str: string, start: number): string

// Concatenates all provided strings into one, preallocating a buffer for performance
strings.concat(first: string, rest: ..string): string

// Formats a string given typed parameters.
// Template string follows a printf-like syntax:
// * %d, %i: Format number as integer
// * %f: Format number with decimals
// * %s, %v: Format value as string, converting if needed
/** Example
    let str = "%d items".format(10)
    print(str) // "10 items"
*/
strings.format(template: string, args: ..any): string

// Searches for a substring in `str`, returning the index of the first character on match.
// If there is no match, -1 is returned
strings.find(haystack: string, needle: string): number

// Replaces all instances of `from` in `str` with `to`.
// Returns a new string.
strings.replace(str: string, from: string, to: string): string

// Returns a new copy of `str`, but reversed
strings.reverse(str: string): string

// Returns the byte value of the character at `idx` in `str`
strings.byte_at(str: string, idx: number): number

// Creates a single-character string from the byte value `val`
strings.from_byte(val: number): string

// Returns whether `haystack` begins with substring `needle`
strings.starts_with(haystack: string, needle: string): bool

// Returns whether `haystack` ends with substring `needle`
strings.ends_with(haystack: string, needle: string): bool

// Returns whether `needle` exists inside `haystack`, starting from `offset`
strings.compare_at(haystack: string, needle: string, offset: number): bool
```
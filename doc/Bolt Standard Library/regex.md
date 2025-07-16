# Regex
Embeds the functionality of [picomatch](https://github.com/Beariish/picomatch) with bolt type-safety. All functions in this module are availible both as imports and exposed directly on the `Regex` prototype. That means that these are equal:
```ts
import regex

let reg = regex.compile("^[a-z]+$")!

regex.groups(reg) // 1
reg.groups() // 1
```

## Types
```ts
// Represents a compiled, ready-to-execute regex
type Regex = <opaque userdata>
```

## Functions
```ts
// Attempts to compile a regex from the supplied pattern
/** Example:
    match let result = regex.compile("^[a-z]+$") {
        is Error {
            print(result.what)
        }
    }
*/
regex.compile(pattern: string): Regex | Error

// Returns the size of the compiled regex, in bytes
regex.size(reg: Regex): number

// Returns the number of capture groups in the compiled regex
regex.groups(reg: Regex): number

// Matches `reg` against `pattern`, returning a list of captures. The first capture is always the entire matched string
regex.eval(reg: Regex, pattern: string): [string]?

// Returns an iterator closure that matches `reg` against `pattern`, 
// continuing after the end of the last match until none are left.
regex.all(reg: Regex, pattern: string): fn: [string]?
```
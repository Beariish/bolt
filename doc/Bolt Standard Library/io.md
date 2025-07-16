# IO

This module is basically a wrapper around the file manipulation functions in the C standard library. `File` represents an opaque file handle.

## Types
```ts
type File = <opaque userdata>
```

## Functions
```ts
// Attempts to open a file handle at `path`, returning an error if it fails. `mode` is identical to `fopen` in C.
/** Example:
    let file = core.assert(io.open("test.txt", "r"), "File not found!")
*/
io.open(path: string, mode: string): File | Error

// Closes a file handle, erroring if it has already been closed.
io.close(file: File): Error?

// Attempts to find the total size of the file pointed to by `file`, erroring on failure.
io.get_size(file: File): number | Error

// Attempts to seek the handle `file` to `position` in bytes, starting from 0
io.seek_set(file: File, position: number): Error?

// Attempts to perform a relative seek from the current position in `file`.
io.seek_relative(file: File, offset: number): Error?

// Attempts to seek `file` to its end.
io.seek_end(file: File): Error?

// Returns the current position of position of the `file` stream, or an error.
io.tell(file: File): number | Error

// Attempts to read up to `length` bytes from `file`, returning the result as a string, or erroring on failure.
io.read(file: File, length: number): string | Error

// Writes `content` to `file` at its current position, returning an error if one occurs
io.write(file: File, content: string): Error?

// Returns whether the stream in `file` is at the end of the file.
io.is_eof(file: File): bool

// Attempt to delete the file at `path`, returning an error if one occurs.
io.delete(path: string): Error?
```

# IO

This module is basically a wrapper around the file manipulation functions in the C standard library. `File` represents an opaque userdata handle.

---

```rust
io.open(path: string, mode: string): File | Error
```

Attempts to open a file handle at `path`, returning an error if it fails. `mode` is identical to `fopen` in C.

Example:
```rust
let file = core.assert(io.open("test.txt", "r"), "File not found!")
```

---

```rust
io.close(file: File): Error?
```
Closes a file handle, erroring if it has already been closed.

---

```rust
io.get_size(file: File): number | Error
```
Attempts to find the total size of the file pointed to by `file`, erroring on failure.

---

```rust
io.seek_set(file: File, position: number): Error?
```
Attempts to seek the handle `file` to `position` in bytes, starting from 0

---

```rust
io.seek_relative(file: File, offset: number): Error?
```
Attempts to perform a relative seek from the current position in `file`.

---

```rust
io.seek_end(file: File): Error?
```
Attempts to seek `file` to its end.

---

```rust
io.tell(file: File): number | Error
```
Returns the current position of position of the `file` stream, or an error.

---

```rust
io.read(file: File, length: number): string | Error
```
Attempts to read up to `length` bytes from `file`, returning the result as a string, or erroring on failure.

---

```rust
io.write(file: File, content: string): Error?
```
Writes `content` to `file` at its current position, returning an error if one occurs

---

```rust
io.is_eof(file: File): bool
```
Returns whether the stream in `file` is at the end of the file.

---

```rust
io.delete(path: string): Error?
```

Attempt to delete the file at `path`, returning an error if one occurs.
# Math
The math module is a thin wrapper around `math.h`.

## Constants
These are all exactly what you'd expect.
```rust
math.pi
math.tau
math.e

math.huge
math.infinity
math.nan

math.ln2
math.ln10
math.log2e
math.log10e

math.sqrt2
math.sqrthalf

math.epsilon
```

## Functions

```rust
math.min(args: ..number): number
```
Returns the smallest out of all the numbers provided.

---

```rust
math.max(args: ..number): number
```
Returns the largest out of all the numbers provided.

---

```rust
math.ispow2(n: number): bool
```
Returns whether `n` is a power of 2.

---

Explaining the follow functions in detail seems needless, so I'm just gonna list them.
```rust
math.sqrt(number): number
math.abs(number): number
math.round(number): number
math.ceil(number): number
math.floor(number): number
math.trunc(number): number
math.sign(number): number

math.sin(number): number
math.cos(number): number
math.tan(number): number

math.asin(number): number
math.acos(number): number
math.atan(number): number

math.sinh(number): number
math.cosh(number): number
math.tanh(number): number

math.asinh(number): number
math.acosh(number): number
math.atanh(number): number

math.log(number): number
math.log10(number): number
math.log2(number): number
math.exp(number): number

math.deg(number): number
math.rad(number): number

math.pow(number, number): number
math.mod(number, number): number
math.imod(number, number): number
math.atan2(number, number): number
```
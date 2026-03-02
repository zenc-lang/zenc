# Standard Library: Random (`std/random.zc`)

The `random` module provides an idiomatic, object-oriented pseudo-random number generator (PRNG). It wraps POSIX `<stdlib.h>` functions (`rand`, `srand`) and automatically seeds using the system clock from `<time.h>`.

## Usage

```zc
import "std/random.zc"

fn main() {
    // Automatically seeds the generator with the current time
    let rng = Random::new();

    // Generate random integers
    let raw_int = rng.next_int();
    let bounded = rng.next_int_range(1, 100); // 1 to 100 inclusive
    
    // Generate other types
    let fraction = rng.next_double(); // 0.0 to 1.0 (exclusive)
    let coin_flip = rng.next_bool();
    
    println "Rolled: {bounded}";
}
```

## Structure

```zc
struct Random {
    seed: U32;
}
```

## Methods

### Initialization

| Method | Signature | Description |
| :--- | :--- | :--- |
| **new** | `Random::new() -> Random` | Creates a new random generator initialized with the current system time. |
| **from_seed** | `Random::from_seed(seed: U32) -> Random` | Creates a new random generator using a specific seed for deterministic sequences. |

### Generation

| Method | Signature | Description |
| :--- | :--- | :--- |
| **next_int** | `next_int(self) -> int` | Returns a pseudo-random integer in the raw range `[0, RAND_MAX]`. |
| **next_int_range** | `next_int_range(self, min: int, max: int) -> int` | Returns a pseudo-random integer in the range `[min, max]` inclusive. Panics if `min > max`. |
| **next_double** | `next_double(self) -> double` | Returns a pseudo-random floating-point number in the range `[0.0, 1.0)`. |
| **next_bool** | `next_bool(self) -> bool` | Returns a pseudo-random boolean (`true` or `false`). |

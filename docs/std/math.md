# Math (`std/math.zc`)

The `Math` module provides a core set of standard mathematical constants and functions natively wrapped around the POSIX `<math.h>` standard library to function smoothly within Zen C's Object-Oriented context. 

## Usage

```zc
import "std/core.zc"
import "std/math.zc"

fn main() {
    let radius = 5.0;
    let area = Math::PI() * Math::pow(radius, 2.0);
    println "Area of circle: {area}";
}
```

## Constants
All constants are functions returning a `double`.
- `Math::PI()`: Archimedes' constant.
- `Math::E()`: Euler's number.

## Functions

### Absolute Value
- `fn abs(x: double) -> double`: Returns the absolute value of `x`.

### Trigonometry
- `fn sin(x: double) -> double`
- `fn cos(x: double) -> double`
- `fn tan(x: double) -> double`
- `fn asin(x: double) -> double`
- `fn acos(x: double) -> double`
- `fn atan(x: double) -> double`
- `fn atan2(y: double, x: double) -> double`

### Exponentials & Logarithms
- `fn sqrt(x: double) -> double`
- `fn pow(base: double, exp: double) -> double`
- `fn exp(x: double) -> double`
- `fn log(x: double) -> double`
- `fn log10(x: double) -> double`

### Rounding & Remainder
- `fn ceil(x: double) -> double`: Rounds up to the nearest integer.
- `fn floor(x: double) -> double`: Rounds down to the nearest integer.
- `fn round(x: double) -> double`: Rounds to the closest integer.
- `fn mod(x: double, y: double) -> double`: Computes the floating-point remainder of `x / y`.

### Min / Max
- `fn max(a: double, b: double) -> double`
- `fn min(a: double, b: double) -> double`

# arith Library Options

Basic arithmetic operators (+, -, *, /)

Options controlling the behaviour of the basic arithmetic operator library.

## Options

### Arithmetic

Runtime behaviour of arithmetic operations.

#### `--div-zero`

Behaviour when evaluating x / 0.

nan      — return IEEE 754 NaN. inf      — return +Inf or -Inf depending on sign of numerator. error    — return a parse/evaluation error code. saturate — return INT_MAX / INT_MIN (integer mode) or ±Inf (real mode). 

#### `--epsilon`

Tolerance below which results are clamped to zero.

#### `--rounding`

Rounding rule applied at the end of every binary operation.


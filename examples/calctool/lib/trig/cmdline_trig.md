# trig Library Options

Trigonometric ops (sin, cos, tan, asin, acos, atan2, ...)

Options controlling trigonometric function behaviour.

## Options

### Angles

Angle unit and precision settings.

#### `--units`

Unit used to interpret arguments to trig functions.

**Example:** `calctool --trig.units degrees eval 'sin(90)'`

#### `--normalize`

Reduce input angle modulo 2π before evaluation.

#### `--method`

Numerical method for high-precision evaluation.

taylor  — Taylor series; good general purpose. cordic  — Iterative; better for fixed-point or SIMD. lookup  — Table-based; fastest but less precise. 


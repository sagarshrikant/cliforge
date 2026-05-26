# ops_linear Plugin Options

Linear-algebra operator plugin (dot, norm, mat-vec)

Plugin providing dot-product, vector-norm, and matrix-vector multiply operators to calctool.  Loaded at runtime via --plugin. 

**Version:** 0.1.0

## Options

### Computation

Numerical backend and precision settings.

#### `--precision`

Decimal digits of precision for intermediate accumulation.

#### `--backend`

Computation backend: naive (portable C) or blas (requires BLAS at runtime).

### Cache

Optional result-cache settings.

#### `--cache-size`

Size of the LRU result cache (0 = disabled).

#### `--cache-ttl`

Cache entry lifetime (0 = never evict).


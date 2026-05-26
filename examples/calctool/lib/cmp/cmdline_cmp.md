# cmp Library Options

Comparison operators (<, >, ==, !=, <=, >=)

Options controlling floating-point comparison behaviour.

## Options

### Comparison

Floating-point comparison parameters.

#### `--epsilon`

Floating-point comparison tolerance.

#### `--nan-equal`

Treat NaN as equal to itself for the == operator.

IEEE 754 defines NaN != NaN. Set this to true for applications that need reflexive equality on all float values. 


# stats Library Options

Statistics ops (mean, stddev, variance, median, ...)

Options controlling statistical function behaviour.

## Options

### Sampling

Variance and standard deviation formula selection.

#### `--mode`

Use Bessel-corrected (n-1) divisor or plain n for variance/stddev.

sample     — divide by (n-1); unbiased estimator for a sample drawn from a population. population — divide by n; use when computing over the entire population. 

#### `--max-points`

Maximum number of data points accepted by stats functions.

#### `--epsilon`

Tolerance for incremental-mean convergence detection.


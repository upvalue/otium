# Ported r7rs-benchmarks

This directory keeps Otium ports of selected programs from ecraven/r7rs-benchmarks. These are ports, not claims of R7RS compatibility.

Initialize the pinned upstream checkout after cloning:

```sh
git submodule update --init benchmarks/r7rs/vendor
```

Build Otium, then run one or more ports:

```sh
python benchmarks/r7rs/run.py build/otium fib ack
```

With Guile installed, run the unmodified upstream program on the same host as a reference:

```sh
python benchmarks/r7rs/run.py build/otium fib --reference guile
```

The runner writes one row per sample to `results.csv` and a short summary to `STATUS.md`. Otium rows use `variant=port`; Guile rows use `variant=upstream`. Do not mix these results with historical numbers collected on other machines.

`manifest.json` grades each translation. A is mechanical. B changes representation without changing the algorithm. C is useful as an Otium benchmark but not a direct comparison. D is unsupported.

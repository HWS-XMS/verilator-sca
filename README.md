# verilator-sca

Verilator-based Side Channel Analysis (SCA) leakage simulator.

A small C++ library (`sca`) you link into a Verilated simulation. While the
design runs, it samples internal signals every clock, converts each sample to a
power estimate via a leakage model (Hamming distance/weight, or raw value), and
streams the resulting traces to HDF5 for side-channel analysis (DPA/CPA/TVLA).

## Requirements

- CMake ≥ 3.20, a C++17 compiler
- [Verilator](https://verilator.org)
- [yaml-cpp](https://github.com/jbeder/yaml-cpp)
- HDF5 (C library)

## Build

```sh
cmake -B build
cmake --build build
```

This produces the static library `libsca.a`.

## Usage

1. Verilate your design with `--public-flat-rw` (so the library can read
   internal signals) and link your simulation against `sca`.

2. Drive capture from your SystemVerilog testbench via DPI-C:

   ```systemverilog
   import "DPI-C" function void sca_init(string config_path);
   import "DPI-C" function void sca_set_metadata(string key, string value);
   import "DPI-C" function void sca_start_capture();
   import "DPI-C" function void sca_sample();
   import "DPI-C" function void sca_end_capture();
   import "DPI-C" function void sca_save(string output_path);
   ```

   Call `sca_init` once, then per trace: `sca_start_capture`, `sca_sample()`
   on every clock edge, `sca_end_capture`; finally `sca_save`. Use
   `sca_set_metadata` to attach values such as `plaintext`, `key`, `ciphertext`.

3. Describe what to capture in a YAML config:

   ```yaml
   leakage_model: hamming_distance   # or hamming_weight | identity, or a list
   signals:
     - path: "TOP.tb.dut.state_reg"
       weight: 1.0
   # or auto-discover every signal under a scope:
   #   signals: auto
   #   scope:   "TOP.tb.dut"
   experiment: "my_experiment"
   series:     "run0"
   ```

## Output

HDF5 in the SCAM trace-database layout: `experiment/series/{samples,
timestamps, ...}`. The metadata keys `plaintext`, `ciphertext`, and `key` are
stored as `stimuli`, `responses`, and `keys`.

## License

MIT — see [LICENSE](LICENSE).

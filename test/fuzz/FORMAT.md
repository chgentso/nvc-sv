# hdlfuzz case directory format (format 1)

Interface between the generator (`hdlfuzz.gen`, `emit_vhdl`, `casefile`)
and the runner/compare/reducer side.  Everything in a case directory is
generated from the seed by own code; nothing is copied from a test suite.

## Producing cases

```
cd test/fuzz
python3 -m hdlfuzz gen --cases 7 --out DIR            # seed 7 -> DIR/
python3 -m hdlfuzz gen --cases 0..99 --campaign 42 --out DIR
                                                     # DIR/c000000 ... c000099
python3 -m hdlfuzz emit DIR/case.json --out DIR2       # re-emit from the IR
python3 -m hdlfuzz check DIR/case.json                 # ir.check()
```

From Python: `gen.generate(seed, tier="V1", profile=None)` returns a
`ir.FuzzCase`; `casefile.write_case(case, dir)` checks it and writes the
directory; `casefile.load_case(dir)` reads it back; `casefile.render(case)`
returns `{file name: text}` without touching the disk (for a reducer cache).
Seeds: with `--campaign C`, case `k` has seed `prng.case_seed(C, k)`;
without it the seed is `k`.  Same seed, tier and profile give the same
bytes in every file.

## Files

| File | Content |
|---|---|
| `case.json` | `{"format": 1, "run": {...}, "ir": {...}}` (sorted keys, indent 1) |
| `dut.vhd` | all design entities in analysis order (children first) |
| `tb.vhd` | testbench entity `fz_tb` |

Analyse the `run.files` in order with VHDL-2008, elaborate `run.top`, run:

```
nvc --std=2008 --work=work:W/work -a dut.vhd tb.vhd -e [opts] fz_tb -r --stop-time=<stop_time_ns>ns
ghdl -a --std=08 --workdir=W dut.vhd tb.vhd
ghdl --elab-run --std=08 --workdir=W fz_tb --ieee-asserts=disable --stop-time=<stop_time_ns>ns
```

Only `std_logic_1164` and `numeric_std` are used (no `-fsynopsys`).

### `run` block

| Key | Meaning |
|---|---|
| `lang`, `std` | `"vhdl"`, `"2008"` |
| `files` | analysis order |
| `top` | testbench entity, always `fz_tb` |
| `dut_entity`, `dut_instance` | top design entity (`m0`) and its label in the testbench (`dut`), so DUT signals are `fz_tb.dut.*` in a VCD |
| `period_ns`, `cycles` | clock period and number of clock cycles |
| `end_time_ns` | time of the final dump and of `FZ END` (cycles*period + period/4) |
| `stop_time_ns` | guard for `--stop-time` ((cycles+2)*period); the testbench calls `std.env.finish` before it |
| `end_marker` | `"FZ END"`: the run is complete only if this report appears |
| `outputs` | top output port names, in sampler/final order |
| `monitors` | `"<entity>.<signal>"` of every monitor process |
| `observe` | `{"vcd", "monitors", "sampler", "final"}`; `vcd: false` means the case should be run without a wave dump (observers must not change results) |

The rest of `case.json` is `ir` (see `hdlfuzz/ir.py`; `ir.from_json` /
`ir.to_json` round-trip it exactly).  `ir.seed`, `ir.tier`, `ir.profile`
and `ir.features` (sorted tags such as `item:ccond`, `timing:delta`,
`wide`, `sw:mul`) are for the campaign records.

## Timing (period P, default 20 ns)

- `clk` starts at '0'; rising edge of cycle `c` (0-based) at `c*P + P/2`,
  falling edge at `(c+1)*P`.
- Reset (when present, port `rst`, active level in `ir.tb.reset.level`):
  active from time 0, released at `cycles_reset * P` (a falling edge).
  FF processes use a synchronous reset in tier V1.
- Inputs, per input `ir.tb.inputs[i].timing`, value `c` of the table:
  - `fall`: applied at `c*P` (time 0 and falling edges); the signal is
    'U' until the first assignment (time 0, delta 1);
  - `rise`: applied at `c*P + P/2`, in the same delta as the clock edge,
    so FFs clocked by that edge already see value `c`;
  - `delta`: as `rise`, then through `delta` (1-3) zero-delay signal
    stages in the testbench (`fz_<port>_d<k>`), so the DUT sees it `delta`
    deltas after the edge.
  `rise`/`delta` inputs start with value 0 of the table.
- Sampler: at `c*P + 3P/4` for every cycle.
- Final dump and end marker at `end_time_ns`, then `std.env.finish`.

## Observer report lines

All observer messages start with `FZ ` and are `report ... severity note`
(nvc prints `** Note: <time>+<delta>: FZ ...`, GHDL prints
`tb.vhd:L:C:@<time>:(report note): FZ ...`).  Values are 9-valued strings:
vectors through `to_string` (MSB first, e.g. `01UX`), scalars through
`std_logic'image` (with the apostrophes, e.g. `'1'`).

| Message | Source | When |
|---|---|---|
| `FZ M <entity>.<signal> <value>` | monitor process `monN` inside the design entity (`process (sig) report`) | at time 0 (initialisation run) and on every event of the signal, deltas included |
| `FZ S <cycle> <o0>=<v> <o1>=<v> ...` | `fz_sample` in the testbench | once per cycle at `c*P + 3P/4` |
| `FZ F <output> <value>` | `fz_clock` | once per output at `end_time_ns` |
| `FZ END` | `fz_clock` | last line, at `end_time_ns` |

Each design entity is instantiated once in tier V1, so `<entity>.<signal>`
names one signal.  IEEE metavalue warnings (`numeric_std` arithmetic on
'U'/'X') are expected in cases without reset; they are not `FZ` lines.

## Naming (all generated)

Entities `m0` (top) .. `mN`; ports `clk`, `rst`, inputs `iK`, outputs
`oK`; internal signals `sK`; labels: processes `pK`, concurrent
assignments `aK`, instances `uK`, monitors `monK`.  Testbench helpers start
with `fz_` (never used by the design).

## Island statistics

Run the default elaboration with `--dump-islands` (or `NVC_DUMP_ISLANDS=1`)
and read `VHDL_ISLAND_ELAB: ... fused_procs=F/T`.  Monitor processes
(`monK`, no signal writes) and the testbench processes are never fused by
design; count the fused share over the DUT processes without monitors
(`VHDL_ISLAND_REJECT` lines name the rejected process keys).

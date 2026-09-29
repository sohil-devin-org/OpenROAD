# Physics-Driven Placement (thm)

The `thm` module couples the global placer with the physical effects that
decide real silicon behavior: power, temperature, supply-voltage (IR) drop,
timing and reliability.  It provides a thermal engine (finite-volume
conduction on a regular grid, steady state and transient, single or two-die
stacks), temperature-dependent leakage with an electrothermal fixed-point
loop, IR-drop coupling through PDNSim, temperature/voltage delay derates
applied through OpenSTA, heat maps for the GUI, and an iteration history that
feeds reports, comparisons and animations.

Physics-driven placement is opt-in: `global_placement -physics_driven`.
Without the flag the placer behaves exactly as before.

## Commands

```{note}
Parameters in square brackets `[-param param]` are optional.  Parameters
without square brackets `-param2 param2` are required.
```

### Set Thermal Configuration

Sets the package/stack model, grid, activity and loop parameters.  All
physical constants have documented defaults (see `ThermalConfig.h`); nothing
is invented at run time.

```tcl
set_thermal_config
    [-ambient temp_c]
    [-top_resistance k_per_w]
    [-bottom_resistance k_per_w]
    [-grid {nx ny}]
    [-two_die] [-single_die]
    [-bond_conductivity w_mk]
    [-thinned_die_thickness m]
    [-second_die_power watts]
    [-activity_scale scale]
    [-activity_file file]
    [-phase {name scale duration_s}]
    [-clear_phases]
    [-loop_max_iterations n]
    [-loop_peak_tolerance temp_c]
    [-runaway_temperature temp_c]
    [-nominal_vdd volts]
    [-include_ir_drop] [-no_ir_drop]
    [-vsrc file]
    [-power_net net]
    [-leakage_fits file]
    [-derate_fits file]
    [-config_file file]
    [-set {key value}]
    [-report]
```

#### Options

| Switch Name | Description |
| ----- | ----- |
| `-ambient` | Ambient temperature in C (default 25). |
| `-top_resistance` | Lumped junction-to-ambient resistance through the heat-sink side in K/W (default 0.1). |
| `-bottom_resistance` | Lumped resistance through the package/board side in K/W (default 50). |
| `-grid` | Lateral thermal grid size (default 64 x 64). |
| `-two_die` | Model a two-die stack (thinned second die bonded on top). |
| `-activity_scale` | Multiplier on the switching + internal power of every instance (default 1). |
| `-activity_file` | Per-instance activity scales, see [Activity file](#activity-file). |
| `-phase` | Add an activity phase for transient analysis: name, activity multiplier, sustained duration in seconds and an optional activity file. |
| `-nominal_vdd` | Nominal supply voltage in V (default 1.8, sky130).  Set it to the liberty operating voltage of the corner (1.1 V for Nangate45); the IR-drop coupling warns when they differ. |
| `-include_ir_drop`, `-no_ir_drop` | Run (default) or skip the PDNSim IR-drop analysis, see [IR drop](#ir-drop). |
| `-vsrc` | Voltage-source location file for the IR-drop analysis, in the `analyze_power_grid -vsrc` format (`x_um, y_um, size_um, voltage` per line). |
| `-power_net` | Power net to analyze for IR drop (default: the first routed `POWER` net of the block). |
| `-loop_max_iterations` | Electrothermal fixed-point iteration limit (default 10). |
| `-runaway_temperature` | Peak temperature above which the loop stops and reports thermal runaway (default 150 C). |
| `-leakage_fits`, `-derate_fits` | JSON files with per-cell leakage(T) and delay(T, V) fits characterized from multi-corner liberty libraries. |
| `-config_file` | `key = value` file with any of the keys accepted by `-set`. |
| `-report` | Print the effective configuration. |

#### Activity file

`-activity_file` (and the optional fourth element of `-phase`) names a plain
text file with one `instance_name scale` pair per line:

```
# hierarchical instance name   scale
core/alu/_1234_   3.0
core/idle_ctrl/_77_   0.1
```

Blank lines and text after `#` are ignored.  The scale multiplies the
switching + internal power that OpenSTA reports for the listed instance (on
top of `-activity_scale`); leakage is not affected and unlisted instances
keep their propagated activity.  Instances that are not in the design are
reported once with a warning.  Power always comes from OpenSTA
(`Sta::power` per instance, whose sum is checked against the `report_power`
design total); the file only rescales its activity.

#### IR drop

With `-include_ir_drop` (the default) `analyze_thermal` pushes the
temperature-aware per-instance power into PDNSim (`PDNSim::setInstPower`),
runs `analyze_power_grid` on `-power_net` and reads the IR drop of the lowest
routed layer of the grid (`getIRDropForLayer`) back.  Every instance gets
`vdd_v = nominal_vdd - drop` of its tile, the tile map is available as the
`ir_drop` heat map (`write_thermal_map -map ir_drop`) and the worst drop is
reported by `report_physics`.  Requirements, as for `analyze_power_grid`:

- a routed power grid on the net (`pdngen` or the DEF special nets), and
- voltage sources: `-vsrc file`, or PDNSim's own sources (power bumps /
  bterms, `set_pdnsim_source_settings`, or the generated full-grid sources
  when the block has neither).

The net voltage is taken from PDNSim (voltage-source file, SDC or liberty
PVT).  When no grid, net or source is available the analysis is skipped with
one `THM` warning, `vdd_v` stays at `-nominal_vdd` and the worst IR drop is
0.  The power-grid resistance is temperature independent in this version:
PDNSim has no public hook to scale its resistors, so `em.metal_tcr_per_k` is
kept in the configuration but not applied.

#### Electromigration

`report_physics` prints the relative Black's-equation lifetime
`MTTF ∝ J^-n exp(Ea / kT)` of the worst tile versus the reference
temperature (`em_reference_temperature`, default 105 C), with `n` and `Ea`
from `ThermalConfig::em` (defaults 2.0 and 0.9 eV, see `ThermalConfig.h`).
The current-density proxy comes from the IR-drop analysis:
J ∝ tile current = tile power / VDD (local, IR-dropped), normalized by the
mean tile current of the powered tiles.  Without an IR-drop analysis the
current term is 1.0 and only the Arrhenius temperature term varies.  The same
per-tile values feed the `em` heat map.

### Analyze Thermal

Runs the coupled analysis at the current placement: OpenSTA power
extraction, thermal solve, temperature-dependent leakage update (iterated
until the peak temperature and leakage converge, or runaway is detected),
optional PDNSim IR drop, and nominal versus derated timing.

```tcl
analyze_thermal
    [-transient]
    [-phases phase_names]
    [-die die]
    [-label label]
    [-corner corner]
    [-no_ir_drop]
    [-no_timing]
```

### Report Physics

```tcl
report_physics [-json file]
```

Reports peak/average temperature per die, gradient, power, leakage, worst IR
drop, nominal and derated WNS/TNS, convergence status, EM lifetime factor and
the hottest instances.

### Set Physics Derating

```tcl
set_physics_derating -enable | -disable
```

Applies (or removes) the per-instance temperature/voltage delay derates so
that every subsequent timing report, the resizer and CTS see physics-aware
timing.  The derates are OpenSTA instance cell-delay derates on both clock
and data paths (early and late), multiplied onto the factors that were in
effect before (`set_timing_derate`, cell/global derates).  Only instances
whose derate differs from 1.0 by more than 1e-6 are derated, and re-applying
unchanged derates is a no-op.  `-disable` restores the pre-physics factors
of every derated instance so that the nominal timing is recovered exactly;
user derates are kept.

### Write Thermal Map

```tcl
write_thermal_map -file file [-map temperature|leakage|derate|ir_drop|delta|em] [-die die]
```

### Physics History and Animation

```tcl
write_physics_history file
load_physics_reference [-clear] [file]
write_physics_animation -file file [-type cooldown|transient|electrothermal|compare|stack]
    [-format gif|mp4] [-scale {min_c max_c}] [-fps fps] [-die die]
```

The history records every checkpoint (maps, metrics and cell positions) and
can be reloaded as a reference for comparison mode (`delta` map, `compare`
animation).  GIF output has no external dependency; MP4 uses `ffmpeg` when it
is installed.

### Global Placement

```tcl
global_placement -physics_driven [-physics_weight w] [-physics_checkpoints n] ...
```

See the `gpl` README for the full option list.

## Example scripts

```
./test/analyze_thermal_gcd.tcl
./test/analyze_thermal_ir_gcd.tcl
./test/physics_derating_gcd.tcl
./test/thermal_map_gcd.tcl
```

## Regression tests

There are a set of regression tests in `./test`.  For more information, refer
to this [section](../../README.md#regression-tests).

## Limitations

- Leakage(T) and delay(T, V) fits require multi-temperature liberty
  characterizations of the same process/voltage corner; without them leakage
  stays at the STA value and derates are 1.0 (a warning is issued).
- IR drop requires a routed power grid (`pdngen`) and voltage sources
  (`-vsrc` or PDNSim's own); the grid resistance is not corrected for
  temperature (no public PDNSim hook).
- Instance derates are applied to the SDC of the analysis corner; changing
  `set_timing_derate` while physics derating is enabled requires
  `set_physics_derating -disable` / `-enable` to pick the new factors up.
- Activity files of transient phases (`-phase {name scale duration file}`)
  are stored but only the global `-activity_file` is applied during
  `analyze_thermal`.

## License

BSD 3-Clause License. See [LICENSE](../../LICENSE) file.

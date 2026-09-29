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
| `-phase` | Add an activity phase for transient analysis: name, activity multiplier and sustained duration in seconds. |
| `-loop_max_iterations` | Electrothermal fixed-point iteration limit (default 10). |
| `-runaway_temperature` | Peak temperature above which the loop stops and reports thermal runaway (default 150 C). |
| `-leakage_fits`, `-derate_fits` | JSON files with per-cell leakage(T) and delay(T, V) fits characterized from multi-corner liberty libraries. |
| `-config_file` | `key = value` file with any of the keys accepted by `-set`. |
| `-report` | Print the effective configuration. |

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
timing.

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
./test/thermal_map_gcd.tcl
```

## Regression tests

There are a set of regression tests in `./test`.  For more information, refer
to this [section](../../README.md#regression-tests).

## Limitations

- Leakage(T) and delay(T, V) fits require multi-temperature liberty
  characterizations of the same process/voltage corner; without them leakage
  stays at the STA value and derates are 1.0 (a warning is issued).
- IR drop requires a routed power grid (`pdngen`) and voltage sources.

## License

BSD 3-Clause License. See [LICENSE](../../LICENSE) file.

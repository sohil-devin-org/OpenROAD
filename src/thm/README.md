# Physics-Driven Placement (thm)

The `thm` module couples the global placer with the physical effects that
decide real silicon behavior: power, temperature, supply-voltage (IR) drop,
timing and reliability.  It provides a thermal engine (conduction on a
regular grid, steady state and transient, single or two-die stacks),
temperature-dependent leakage with an electrothermal fixed-point loop,
IR-drop coupling through PDNSim, temperature/voltage delay derates applied
through OpenSTA, heat maps for the GUI, and an iteration history that feeds
reports, comparisons and animations.

Physics-driven placement is opt-in: `global_placement -physics_driven`.
Without the flag the placer behaves exactly as before
(see [Default-behavior invariance](#default-behavior-invariance)).

## Commands

```{note}
- Parameters in square brackets `[-param param]` are optional.
- Parameters without square brackets `-param2 param2` are required.
```

### Set Thermal Configuration

Sets the package/stack model, grid, activity and loop parameters.  Every
physical constant has a documented, cited default (see
[Coefficients and defaults](#coefficients-and-defaults)); nothing is invented
at run time.  The command may be called several times; each call only changes
the options given.

```tcl
set_thermal_config
    [-ambient temp_c]
    [-top_resistance k_per_w]
    [-bottom_resistance k_per_w]
    [-grid {nx ny}]
    [-two_die]
    [-single_die]
    [-bond_conductivity w_mk]
    [-thinned_die_thickness m]
    [-second_die_power watts]
    [-activity_scale scale]
    [-activity_file file]
    [-phase {name scale duration_s ?activity_file?}]
    [-clear_phases]
    [-loop_max_iterations n]
    [-loop_peak_tolerance temp_c]
    [-runaway_temperature temp_c]
    [-nominal_vdd volts]
    [-include_ir_drop]
    [-no_ir_drop]
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
| `-top_resistance` | Lumped junction-to-ambient thermal resistance through the heat-sink side of the stack in K/W (default 0.1). |
| `-bottom_resistance` | Lumped resistance through the package substrate / board side in K/W (default 50). |
| `-grid` | Lateral thermal grid size `{nx ny}`, at least 2 x 2 (default 64 x 64). |
| `-two_die` | Model a two-die stack: a thinned second die hybrid-bonded on top of the design die. |
| `-single_die` | Model a single die (default). |
| `-bond_conductivity` | Effective conductivity of the bonding layer between the dies in W/mK (default 2.0). |
| `-thinned_die_thickness` | Silicon thickness of the thinned top die in meters (default 50e-6). |
| `-second_die_power` | Uniform power of the second die in W (default 0). |
| `-activity_scale` | Multiplier on the OpenSTA propagated switching activity used for dynamic power (default 1.0). |
| `-activity_file` | VCD or SAIF activity file read through OpenSTA instead of the scaled default activity. |
| `-phase` | Add a sustained activity phase for transient analysis: name, activity multiplier, duration in seconds and an optional activity file. Repeat the command to add several phases. |
| `-clear_phases` | Remove all activity phases. |
| `-loop_max_iterations` | Electrothermal (temperature-leakage) fixed-point iteration limit (default 10). |
| `-loop_peak_tolerance` | Peak-temperature change in C below which the electrothermal loop is converged (default 0.05). |
| `-runaway_temperature` | Peak temperature in C above which the loop stops and reports thermal runaway (default 150). |
| `-nominal_vdd` | Nominal supply voltage in V used for derates when no IR-drop result is available (default 1.8). |
| `-include_ir_drop` | Run PDNSim IR-drop analysis inside `analyze_thermal` (default). |
| `-no_ir_drop` | Skip IR-drop analysis; every instance sees the nominal supply. |
| `-leakage_fits` | JSON file with per-cell leakage(T) fits characterized from multi-temperature liberty libraries. |
| `-derate_fits` | JSON file with per-cell delay(T, V) derate fits characterized from multi-corner liberty libraries. |
| `-config_file` | `key = value` file (`#` comments) with any of the keys accepted by `-set`. |
| `-set` | Set one configuration key directly: `{key value}`. Keys: `ambient`, `top_resistance`, `bottom_resistance`, `heat_sink_on_bottom`, `silicon_conductivity`, `silicon_heat_capacity`, `temperature_dependent_conductivity`, `silicon_thickness`, `beol_thickness`, `beol_conductivity`, `bond_thickness`, `bond_conductivity`, `thinned_die_thickness`, `two_die`, `second_die_power_source`, `second_die_power`, `second_die_power_file`, `grid_x`, `grid_y`, `solver_tolerance`, `solver_max_iterations`, `loop_peak_tolerance`, `loop_leakage_tolerance`, `loop_max_iterations`, `runaway_temperature`, `transient_time_step`, `em_activation_energy`, `em_current_exponent`, `em_reference_temperature`, `metal_tcr`, `activity_scale`, `activity_file`, `nominal_vdd`, `include_ir_drop`. |
| `-report` | Print the effective configuration. |

### Analyze Thermal

Runs the coupled analysis at the current placement: OpenSTA power extraction,
thermal solve, temperature-dependent leakage update (iterated until the peak
temperature and leakage converge, or runaway is detected), optional PDNSim IR
drop, and nominal versus derated timing.  Every run appends a snapshot (maps,
metrics, cell positions) to the physics history.

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

#### Options

| Switch Name | Description |
| ----- | ----- |
| `-transient` | Run a transient analysis through the configured activity phases instead of a steady-state solve. |
| `-phases` | Tcl list of phase names to replay (default: all phases, in the order they were added). |
| `-die` | Report the results of this die (0 = design die, 1 = second die of a two-die stack). |
| `-label` | Label stored with the history snapshot (default: `iteration N`). |
| `-corner` | Timing/power corner to analyze (default: the current command corner). |
| `-no_ir_drop` | Skip IR-drop analysis for this run only. |
| `-no_timing` | Skip the nominal/derated timing evaluation for this run only. |

### Report Physics

Reports peak/average temperature per die, maximum gradient, total and leakage
power, worst IR drop, nominal and derated WNS/TNS, electrothermal
convergence status, EM lifetime factor, clock-skew delta, HPWL and the
hottest instances for the last `analyze_thermal` run.  With `-json` the same
metrics are written to a file (keys `peak_temp_c`, `avg_temp_c`,
`max_gradient_c_per_mm`, `total_power_w`, `leakage_power_w`,
`worst_ir_drop_v`, `wns_nominal_s`, `wns_derated_s`, `tns_nominal_s`,
`tns_derated_s`, `electrothermal_iterations`, `converged`, `runaway`,
`em_lifetime_factor`, `clock_skew_delta_s`, `hpwl_um`, `runtime_s`,
`derating_enabled`).

```tcl
report_physics
    [-json file]
```

#### Options

| Switch Name | Description |
| ----- | ----- |
| `-json` | Also write the metrics as JSON to `file`. |

### Set Physics Derating

Applies (or removes) the per-instance temperature/voltage delay derates of
the last analysis through OpenSTA's instance derating so that every
subsequent timing report, the resizer and CTS see physics-aware timing.
Exactly one of `-enable` or `-disable` is required.

```tcl
set_physics_derating
    [-enable]
    [-disable]
```

#### Options

| Switch Name | Description |
| ----- | ----- |
| `-enable` | Apply the derates and keep them applied after future `analyze_thermal` runs. |
| `-disable` | Remove the derates; timing is nominal again. |

### Write Thermal Map

Writes one map of the last analysis as text (`# x_um y_um value` rows on the
thermal grid).

```tcl
write_thermal_map
    -file file
    [-map temperature|leakage|derate|ir_drop|delta|em]
    [-die die]
```

#### Options

| Switch Name | Description |
| ----- | ----- |
| `-file` | Output file. |
| `-map` | `temperature` (C, default), `leakage` (W per tile), `derate` (delay multiplier), `ir_drop` (V), `delta` (temperature minus the loaded reference, C) or `em` (relative EM lifetime factor). |
| `-die` | Die to write for a two-die stack (default 0). |

### Write Physics History

Writes every snapshot recorded by `analyze_thermal` and by the placer's
physics checkpoints (maps, metrics and cell positions) as JSON.

```tcl
write_physics_history
    file
```

#### Arguments

| Argument Name | Description |
| ----- | ----- |
| `file` | Output JSON file. |

### Load Physics Reference

Loads a previously written history as the reference for comparison mode: the
`delta` map and the `compare` animation show the difference between the
current history and the reference.

```tcl
load_physics_reference
    [-clear]
    [file]
```

#### Options

| Switch Name | Description |
| ----- | ----- |
| `-clear` | Drop the loaded reference. |

#### Arguments

| Argument Name | Description |
| ----- | ----- |
| `file` | History JSON written by `write_physics_history`. |

### Write Physics Animation

Renders the history as an animation.  GIF output has no external dependency;
MP4 uses `ffmpeg` when it is installed.

```tcl
write_physics_animation
    -file file
    [-type cooldown|transient|electrothermal|compare|stack]
    [-format gif|mp4]
    [-scale {min_c max_c}]
    [-fps fps]
    [-die die]
```

#### Options

| Switch Name | Description |
| ----- | ----- |
| `-file` | Output file. |
| `-type` | `cooldown` (placement iterations, default), `transient` (time steps of a transient run), `electrothermal` (leakage-temperature loop iterations), `compare` (current versus loaded reference) or `stack` (both dies of a two-die stack). |
| `-format` | `gif` (default) or `mp4`. |
| `-scale` | Fixed color scale `{min_c max_c}` (default: auto-scale over the history). |
| `-fps` | Frames per second (default 4). |
| `-die` | Die to render for a two-die stack (default 0). |

### Global Placement

Physics-driven placement is enabled through the placer command; see the
[gpl README](../gpl/README.md) for the full option list.

<!-- checker: skip -->
```tcl
global_placement -physics_driven ...
```

## Query helpers

Tcl helpers for scripting and tests, all reading the last analysis:
`thm::get_peak_temperature`, `thm::get_total_power`, `thm::get_worst_ir_drop`,
`thm::get_nominal_wns`, `thm::get_derated_wns`, `thm::get_history_size`,
`thm::thermal_converged`, `thm::thermal_runaway`,
`thm::get_screening_length_um`, `thm::physics_derating_enabled` and
`thm::reset_thermal` (clears the history and analysis state).

## Physics model

#### Power

Per-instance power (internal + switching + leakage) comes from OpenSTA
(`Sta::power`) at the analysis corner.  Switching activity is the propagated
OpenSTA activity times `-activity_scale`, or the activity of a VCD/SAIF file.
Power is never hand-assigned per cell.  Instance power is accumulated into a
`PowerMap` (W per lateral tile) on the thermal grid.

#### Heat conduction

The die stack is a set of homogeneous layers (bulk silicon, active layer,
BEOL, bond and second die for two-die stacks) with lumped resistances to
ambient at the top (heat-sink side, `-top_resistance`) and bottom
(package/board side, `-bottom_resistance`) faces, following the HotSpot
compact model [HS] and the layered 3D-ICE stack description [3DI].  Steady
state solves

\[ \nabla \cdot (k \nabla T) + q = 0 \]

on the 3-D grid; transient analysis integrates
\( \rho c_p \, \partial T / \partial t = \nabla \cdot (k \nabla T) + q \)
with the configured time step over the sustained activity phases (chip
thermal time constants are milliseconds to seconds, so phases are held
activity levels, never raw trace replay).  Material data (silicon, SiO2,
copper) are from Incropera and DeWitt [INC]; optionally
\( k(T) = k_{300} (300/T)^{1.3} \) [INC].

For the placer, lateral spreading in a thin plate of thickness \(t\) and
conductivity \(k\) that loses heat to ambient through an area-specific
resistance \(r\) (K m^2/W) obeys the screened Poisson (Helmholtz) equation

\[ k t \nabla^2 T - (T - T_{amb})/r = -q
   \quad\Longleftrightarrow\quad
   \nabla^2 T - T/\lambda^2 = -q/k_{eff},
   \qquad \lambda = \sqrt{k \, t \, r} . \]

The screening length \(\lambda\) (`thm::get_screening_length_um`) is the
lateral distance over which a hot spot decays and sets the range of the
thermal-spreading force in `global_placement -physics_driven`.  The lumped
die-level resistances are converted to area-specific values with the die
area.

#### Leakage and electrothermal loop

Leakage is temperature dependent through per-cell fits characterized from
liberty libraries of the same process and voltage at several temperatures:

\[ L(T) = L(T_{ref}) \, e^{\beta (T - T_{ref})} , \]

with a family-level fallback (median \(\beta\)).  `analyze_thermal` iterates
power -> temperature -> leakage until the peak temperature changes by less
than `-loop_peak_tolerance` and leakage by less than the relative tolerance,
or `-loop_max_iterations` is reached.  A peak above `-runaway_temperature`
is reported as thermal runaway.  Without fits the leakage stays at the STA
value (a warning is issued).

#### IR drop

`IrDropCoupling` feeds the temperature-aware instance powers to PDNSim
(`setInstPower`, `setNetVoltage`, `analyzePowerGrid`) on the routed supply
net, reads the per-layer voltage drop back onto the thermal grid and assigns
each instance its local supply voltage.  Metal resistance is optionally
corrected for temperature with the copper temperature coefficient,
\( R(T) = R_0 (1 + \alpha (T - T_0)) \) [CRC].

#### Timing derates

Delay derates are separable in temperature and voltage,
\( \text{derate} = f_T(T) \, f_V(V) \) with \( f_T(T_{nom}) = f_V(V_{nom}) = 1 \),
fitted as piecewise-linear tables from the delay tables of the same
multi-corner libraries (per cell where available, family otherwise).  They
are applied per instance through OpenSTA's `setTimingDerate`; nominal and
derated WNS/TNS are both reported.  Without fits the derate is 1.0.

#### Electromigration

Power-grid segment lifetime uses Black's equation [BLK]

\[ \text{MTTF} = A \, J^{-n} \, e^{E_a / (k T)} , \]

reported as the relative lifetime versus a reference condition
\( (j_{ref}/j)^n \exp\left(\frac{E_a}{k}\left(\frac{1}{T} - \frac{1}{T_{ref}}\right)\right) \)
with copper parameters from JEDEC JEP122H [JEP].

#### Coefficients and defaults

All defaults live in `include/thm/ThermalConfig.h` with their citation.

| Quantity | Default | Source |
| ----- | ----- | ----- |
| Ambient temperature | 25 C (sky130 `tt_025C` corner) | library corner |
| Top (heat-sink) resistance | 0.1 K/W | HotSpot `r_convec` [HS] |
| Bottom (package/board) resistance | 50 K/W | HotSpot `r_convec_sec` [HS] |
| Silicon conductivity | 100 W/mK (148 W/mK at 300 K in [INC]) | [HS], [INC] |
| Silicon volumetric heat capacity | 1.75e6 J/m^3K | [HS] |
| Silicon k(T) exponent (optional) | 1.3 | [INC] |
| Bulk silicon thickness | 150 um | HotSpot `t_chip` [HS] |
| BEOL thickness / conductivity | 10 um / 2 W/mK (SiO2 1.4 W/mK [INC] with Cu fill) | [3DI], [INC] |
| Bond layer thickness / conductivity | 5 um / 2 W/mK | hybrid bonding [HB], [3DI] |
| Bond layer heat capacity | 1.65e6 J/m^3K (SiO2) | [INC] |
| Thinned top die thickness | 50 um | [HB] |
| Grid | 64 x 64 | resolves hot spots of a few hundred um |
| Linear solver tolerance / iterations | 1e-6 / 5000 | numerical |
| Loop peak / leakage tolerance, iterations | 0.05 C / 1e-3 / 10 | numerical |
| Runaway temperature | 150 C | reporting threshold |
| Transient time step | 1 ms | numerical |
| EM activation energy / current exponent | 0.9 eV / 2.0 (Cu) | [JEP] |
| EM reference temperature | 105 C | JEDEC qualification [JEP] |
| Copper TCR | 3.9e-3 /K (ref 25 C) | [CRC] |
| Nominal VDD | 1.8 V | sky130 |

References:

- [HS] W. Huang et al., "HotSpot: A Compact Thermal Modeling Methodology for
  Early-Stage VLSI Design," IEEE TVLSI 14(5), 2006; HotSpot 6.0
  `hotspot.config`.
- [INC] F. P. Incropera, D. P. DeWitt, "Fundamentals of Heat and Mass
  Transfer," 6th ed., Wiley, 2007, Tables A.1/A.2.
- [3DI] A. Sridhar et al., "3D-ICE: Fast compact transient thermal modeling
  for 3D ICs with inter-tier liquid cooling," ICCAD 2010.
- [BLK] J. R. Black, "Electromigration - A Brief Survey and Some Recent
  Results," IEEE Trans. Electron Devices 16(4), 1969.
- [JEP] JEDEC JEP122H, "Failure Mechanisms and Models for Semiconductor
  Devices," 2016.
- [CRC] CRC Handbook of Chemistry and Physics, 97th ed., resistivity of
  copper versus temperature.
- [HB] Hybrid bonding effective conductivity survey (Cu/SiO2 interface
  ~1-4 W/mK effective); see also [3DI].

## Example scripts

Hermetic regression examples in `./test`:

```
./test/analyze_thermal_gcd.tcl
./test/thermal_map_gcd.tcl
./test/physics_flow_gcd.tcl
```

`physics_flow_gcd.tcl` is the end-to-end demo on Nangate45 gcd: baseline
`analyze_thermal` -> `write_physics_history` -> `report_physics -json` ->
temperature/leakage/derate maps -> `set_physics_derating -enable` ->
`report_worst_slack` -> `load_physics_reference` -> `delta` map, with
PASS/FAIL sanity lines.

#### Flow scripts (`./test/flow`, not part of the regression suite)

`physics_placement_demo.tcl` runs the full flow on gcd, whose floorplan is
already in the DEF: baseline `global_placement` -> `analyze_thermal` ->
history, then `global_placement -physics_driven` -> `analyze_thermal` ->
`report_physics -json` -> `write_physics_animation -type compare`.  If the
binary does not have `-physics_driven` the script says so and analyzes the
baseline placement again.

```
openroad -exit src/thm/test/flow/physics_placement_demo.tcl
THM_DEMO_OUT=/tmp/demo openroad -exit src/thm/test/flow/physics_placement_demo.tcl
```

`check_invariance.tcl` is the default-behavior check, see
[Default-behavior invariance](#default-behavior-invariance).

## Benchmarks

`test/flow/run_sky130hd_benchmarks.sh` runs baseline versus
`-physics_driven` global placement on the in-repo sky130hd designs
(`test/gcd_sky130hd.tcl`, `test/aes_sky130hd.tcl`, `test/ibex_sky130hd.tcl`)
with the stock platform settings from `test/sky130hd/sky130hd.vars`, up to
and including the two `global_placement` passes of `test/flow.tcl`.  Each run
writes a `report_physics -json` file and one CSV row; `summarize_benchmarks.py`
(standard library only) merges the rows into `benchmarks.csv` and a Markdown
table `benchmarks.md` with columns design, mode, config, status, HPWL, peak T,
average T, max gradient, leakage, worst IR drop, nominal/derated WNS and TNS
and placement runtime.

```
# gcd only (default); aes/ibex are reported as "not run (skipped)"
src/thm/test/flow/run_sky130hd_benchmarks.sh --openroad bazel-bin/openroad
# everything
src/thm/test/flow/run_sky130hd_benchmarks.sh --designs gcd,aes,ibex --threads 8
# regenerate the tables from existing results
python3 src/thm/test/flow/summarize_benchmarks.py --out-dir src/thm/test/flow/results/benchmarks
```

Physics-mode runs on a binary without `global_placement -physics_driven` are
reported as `not run (flag unavailable)`.

`--stress` applies the **stress thermal configuration, which is not stock**:
`set_thermal_config -ambient 105 -top_resistance 50 -activity_scale 4.0`
(AEC-Q100 Grade 2 maximum ambient; heat leaves only through the package/board
path, HotSpot's secondary-path default of 50 K/W [HS]; four times the default
switching activity as a workload assumption).  Stress tables are labeled
`stress` in the `config` column; stock results are labeled `stock`.

## Default-behavior invariance

`test/flow/check_invariance.tcl` reads the Nangate45 gcd design, runs
`global_placement` with default arguments (no `-physics_driven`), writes the
DEF and diffs it against `test/flow/check_invariance_gcd.defok`:

```
openroad -exit src/thm/test/flow/check_invariance.tcl
THM_INVARIANCE_REGEN=1 openroad -exit src/thm/test/flow/check_invariance.tcl  # rewrite golden
```

The golden was written by an `openroad` built from the
`physics-driven-placement` base branch (commit 5a311be7dc), whose `src/gpl` is
identical to `origin/master`; a PASS on a physics-enabled build proves that
default placement is bit-identical to master.

## Regression tests

There are a set of regression tests in `./test`.  For more information, refer
to this [section](../../README.md#regression-tests).

```
bazelisk test --//:platform=gui //src/thm/test/...
```

The C++ unit tests are in `./test/cpp` (`ThermalCoreTest`).

## Messages

`messages.txt` is generated from the sources:

```
python3 etc/find_messages.py -d src/thm > src/thm/messages.txt
```

## Limitations

- Leakage(T) and delay(T, V) fits require multi-temperature liberty
  characterizations of the same process/voltage corner; without them leakage
  stays at the STA value and derates are 1.0 (a warning is issued).
- IR drop requires a routed power grid (`pdngen`) and a supply net with
  special wires; otherwise the nominal VDD is used (THM-0073).
- The finite-volume solver currently falls back to the lumped per-column
  reference solver; `write_physics_animation` and the thermal clock-skew
  analysis are stubs that warn and produce no output.
- The second die of a two-die stack is modeled with a uniform or mirrored
  power map, not a placed design.
- Transient analysis replays sustained activity phases; it does not replay
  raw activity traces.

## License

BSD 3-Clause License. See [LICENSE](../../LICENSE) file.

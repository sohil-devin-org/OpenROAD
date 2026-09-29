# Thermal Analysis

The thermal analysis module in OpenROAD (`thm`) connects a placed design to
the external [HotSpot](https://github.com/uvahotspot/HotSpot) thermal
simulator. Per-instance power from OpenSTA is binned into floorplan tiles,
written out as the HotSpot floorplan and power trace, and the resulting
steady-state temperature grid is read back for reporting and for the
`Temperature` heat map in the GUI.

HotSpot must be built separately and available either on `PATH` or passed
with `-hotspot_binary`.

## Commands

```{note}
- Parameters in square brackets `[-param param]` are optional.
- Parameters without square brackets `-param2 param2` are required.
```

### Analyze Thermal

Runs the HotSpot steady-state grid model on the current placed design and
reports the peak and average temperature, the hottest region, and the
instances located in it.

```tcl
analyze_thermal
    [-corner corner]
    [-hotspot_binary path]
    [-hotspot_config file]
    [-work_dir dir]
    [-keep_files]
    [-tile_size size_um]
    [-grid_rows rows]
    [-grid_cols cols]
    [-ambient temperature_c]
    [-report_instances count]
    [-report_file file]
```

#### Options

| Switch Name | Description |
| ----- | ----- |
| `-corner` | Corner to compute instance power for. Defaults to the command corner. |
| `-hotspot_binary` | Path to the HotSpot executable. Defaults to `hotspot` found on `PATH`. |
| `-hotspot_config` | HotSpot configuration file, used verbatim instead of the built-in one (derived from the HotSpot `example.config`: 150 um silicon die, copper spreader and a 60 mm heat sink with `-r_convec 0.1`). The grid size is always passed on the command line; `-ambient` is only applied to the built-in configuration, so set `-ambient`/`-init_temp` (Kelvin) in a custom file. |
| `-work_dir` | Directory for the generated HotSpot inputs and outputs. Defaults to a temporary directory. |
| `-keep_files` | Keep the generated HotSpot files. |
| `-tile_size` | Size in microns of the square tiles instance power is binned into. Defaults to the die size divided by 32. |
| `-grid_rows`, `-grid_cols` | Resolution of the HotSpot grid model. Defaults to 64 x 64. Each dimension is limited to the die size in database units and the grid to 2^20 cells. |
| `-ambient` | Ambient temperature in degrees Celsius. Defaults to 45. Only used with the built-in configuration; with `-hotspot_config` it is ignored (with a warning) and the file's `-ambient`/`-init_temp` apply. |
| `-report_instances` | Number of instances to list for the hottest region. Defaults to 10. |
| `-report_file` | Also write the report to this file. |

### Report Thermal

Prints the report of the last `analyze_thermal` run.

```tcl
report_thermal
    [-file file]
```

#### Options

| Switch Name | Description |
| ----- | ----- |
| `-file` | Also write the report to this file. |

### Write Temperature Map

Writes the temperature grid as CSV (`x_min,y_min,x_max,y_max,temperature_c`
in microns and degrees Celsius).

```tcl
write_temperature_map file
```

#### Options

| Switch Name | Description |
| ----- | ----- |
| `file` | Output file. |

## GUI heat map

After `analyze_thermal` the `Temperature` heat map is available in the GUI
(`Heat Maps` > `Temperature`) and through the `gui::set_heatmap Temperature`
settings; it is empty until thermal results exist. Each map cell is one
HotSpot grid cell over the die area and values are absolute degrees Celsius
(legend, tooltips and `gui::dump_heatmap` all report `°C`, no SI prefixes).
By default the colour scale spans the real min/max of the grid. Enable
`FixedRange` together with `FixedMin`/`FixedMax` (also in the heat map setup
dialog) to pin the colour scale and legend to exactly `[FixedMin, FixedMax]`
so different designs can be compared fairly; cells outside the range clamp to
the end colours (equal endpoints show a 1 °C band centred on the value). The
map rebuilds automatically when these change:

```tcl
gui::set_heatmap Temperature FixedRange 1
gui::set_heatmap Temperature FixedMin 40
gui::set_heatmap Temperature FixedMax 60
gui::get_heatmap_double Temperature FixedMax
```

## Example scripts

`./test/gcd_hotspot.tcl` runs HotSpot on the placed sky130hd `gcd` fixture
and enables the `Temperature` heat map; it needs `hotspot` on `PATH` (or in
the `HOTSPOT` environment variable):

```shell
cd test && HOTSPOT=/path/to/hotspot openroad -gui gcd_hotspot.tcl
```

## Regression tests

There are a set of regression tests in `./test`. For more information, refer to this [section](../../README.md#regression-tests).

Simply run the following script:

```shell
./test/regression
```

## Limitations

- Only steady-state analysis is supported; power is the OpenSTA static
  (activity based) total power, so the result depends on the liberty,
  parasitics (`estimate_parasitics`) and activity setup used for
  `report_power`.
- Instance power is binned into square tiles (die size / 32 by default), so
  the temperature grid cannot resolve features smaller than a tile; the
  whole die is modelled as a single silicon layer and macros and standard
  cells are treated alike (a macro's liberty power is spread uniformly over
  its area).
- The package model dominates the absolute temperatures: the built-in
  configuration is HotSpot's example desktop package, so sky130 designs with
  tens or hundreds of milliwatts sit within about a degree of ambient. Use
  `-hotspot_config` to describe the real package (spreader/sink size,
  `-r_convec`).
- HotSpot accepts at most 8192 floorplan units, so the number of power tiles
  (`round(die_width / tile_size) * round(die_height / tile_size)`) must stay
  below that; `analyze_thermal` rejects a `-tile_size` that needs more tiles
  and suggests the smallest one that fits.
- HotSpot reports grid temperatures with two decimals, so designs with
  less than a milliwatt (e.g. `gcd`) show only a few hundredths of a degree
  of spatial variation; the heat map legend switches to two decimals when
  the map spans less than a degree.
- HotSpot runs as a separate process through `/bin/sh`; its output is kept in
  `<work_dir>/<block>.hotspot.log` and is only preserved with `-keep_files`
  or `-work_dir`.

## FAQs

Check out [GitHub discussion](https://github.com/The-OpenROAD-Project/OpenROAD/discussions/categories/q-a?discussions_q=category%3AQ%26A+thm)
about this tool.

## License

BSD 3-Clause License. See [LICENSE](../../LICENSE) file.

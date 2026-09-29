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
| `-hotspot_config` | HotSpot configuration file. Defaults to a built-in configuration derived from the HotSpot example config. |
| `-work_dir` | Directory for the generated HotSpot inputs and outputs. Defaults to a temporary directory. |
| `-keep_files` | Keep the generated HotSpot files. |
| `-tile_size` | Size in microns of the square tiles instance power is binned into. Defaults to the die size divided by 32. |
| `-grid_rows`, `-grid_cols` | Resolution of the HotSpot grid model. Defaults to 64 x 64. |
| `-ambient` | Ambient temperature in degrees Celsius. Defaults to 45. |
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
settings. Values are absolute degrees Celsius. Enable `FixedRange` together
with `FixedMin`/`FixedMax` to pin the color range so different designs can be
compared fairly:

```tcl
gui::set_heatmap Temperature FixedRange 1
gui::set_heatmap Temperature FixedMin 40
gui::set_heatmap Temperature FixedMax 60
gui::set_heatmap Temperature rebuild
```

## Example scripts

Example scripts demonstrating how to run thermal analysis on a sample design
of `gcd` as follows:

```shell
./test/gcd_hotspot.tcl
```

## Regression tests

There are a set of regression tests in `./test`. For more information, refer to this [section](../../README.md#regression-tests).

Simply run the following script:

```shell
./test/regression
```

## Limitations

- Only steady-state analysis is supported; power is the OpenSTA static
  (activity based) total power.
- The whole die is modelled as a single silicon layer; macros and standard
  cells are treated alike and package parameters come from the HotSpot
  configuration.

## FAQs

Check out [GitHub discussion](https://github.com/The-OpenROAD-Project/OpenROAD/discussions/categories/q-a?discussions_q=category%3AQ%26A+thm)
about this tool.

## License

BSD 3-Clause License. See [LICENSE](../../LICENSE) file.

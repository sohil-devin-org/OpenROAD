---
name: add-test
description: >
  Add integration or unit tests to an OpenROAD module. Handles the
  complete workflow: writing the Tcl/C++ test, generating golden files,
  and registering the test in BOTH CMake and Bazel build systems (the
  most common mistake is forgetting Bazel registration). Use when asked
  to add tests, improve test coverage, create regression tests, or write
  unit tests for any OpenROAD module (ant, cts, dpl, drt, gpl, grt, mpl,
  odb, pad, pdn, psm, rcx, rsz, sta, stt, tap, upf, utl, etc.).
  Also triggers on: "add test", "write test", "test coverage",
  "missing tests", "create regression test", "add unit test".
argument-hint: "<module> [test-description]"
---

# Add Test to OpenROAD Module

You are adding tests for: **$ARGUMENTS**

## 1. Identify the module and scope

Parse `$ARGUMENTS` to determine:
- **Module name** (e.g., `rsz`, `drt`, `gpl`)
- **What to test** (specific feature, edge case, or general coverage)

Explore existing tests to understand conventions:
```bash
ls src/MODULE/test/*.tcl | head -20
```

Read a representative test for the module's style:
```bash
# Good reference for integration test patterns:
cat src/rsz/test/repair_tie12_hier.tcl
```

## 2. Write the integration test (Tcl)

Create `src/MODULE/test/TEST_NAME.tcl`:

```tcl
# Brief description of what this test verifies
set test_name TEST_NAME
source "helpers.tcl"

# Read design files
read_lef "TECH.lef"
read_lef "CELLS.lef"
read_def "DESIGN.def"

# ... test operations ...

# Use make_result_file for temporary output
set def_filename "${test_name}.def"
set out_def [make_result_file $def_filename]
write_def $out_def

# Compare against golden file
diff_files ${test_name}.defok $out_def
```

Every data file the test reads (LEF/DEF/lib/sdc, fixture scripts) must be
visible to Bazel. Most modules glob common extensions into a
`regression_resources` filegroup in `src/MODULE/test/BUILD`; add any file
the glob misses (e.g. `.sh`, `.py`, `.grid`) to that filegroup explicitly.

### Test design files

Prefer reusing existing test data in `src/MODULE/test/`. Only create
new LEF/DEF files when testing a specific geometry or edge case that
existing files don't cover. Keep test data minimal.

### Naming conventions

- Test files: `src/MODULE/test/TEST_NAME.tcl`
- Golden log: `src/MODULE/test/TEST_NAME.ok`
- Golden output: `src/MODULE/test/TEST_NAME.{defok,vok,spefok,...}`
- Test data: `src/MODULE/test/*.{lef,def,lib,sdc,v}`

## 3. Generate golden files

The `.ok` file must match exactly what `test/regression_test.sh` captures:
stdout and stderr combined, run from `src/MODULE/test`. `-no_splash`
suppresses the banner, so no manual editing is needed.

```bash
# Build the binary once (Bazel; CMake is deprecated in this repo)
bazelisk build //:openroad

cd src/MODULE/test
../../../bazel-bin/openroad -no_splash -no_init -exit TEST_NAME.tcl > TEST_NAME.ok 2>&1
```

Golden files must not contain anything that varies between runs or
machines (absolute paths, timestamps, runtimes, thread counts).

If the test uses `diff_files`, also generate the golden output file
(e.g., `TEST_NAME.defok`) by copying the result written under `results/`.
Inspect every golden before committing; never accept output you have not
read.

## 4. Register in BOTH build systems

This is the most common mistake. **Every Tcl/Python integration test must
be in both CMake AND Bazel.** CMake-only tests silently pass locally but
are missing from Bazel CI. (Doc checks such as `*_man_tcl_check` and
`*_readme_msgs_check` are Bazel-only by convention; leave them that way.)

### CMake -- `src/MODULE/test/CMakeLists.txt`

Find the `or_integration_tests()` call. The first positional argument
is the **module name**, followed by the `TESTS` keyword and the list:

```cmake
or_integration_tests(
  "MODULE"           # <-- module name (e.g. "rsz")
  TESTS
    existing_test1
    existing_test2
    TEST_NAME        # <-- add here
)
```

Some modules also have a `PASSFAIL_TESTS` section after `TESTS` -- add
to the appropriate list. Look at the module's existing `CMakeLists.txt`
before editing.

### Bazel -- `src/MODULE/test/BUILD`

The typical pattern is a `TESTS` list consumed by a list comprehension
that calls `regression_test()` for each name:

```python
TESTS = [
    "buffer_ports1",
    "buffer_ports10",
    "buffer_ports11",
    "TEST_NAME",     # <-- add here, in the existing sort order
    # (other tests omitted)
]

[
    regression_test(
        name = test_name,
    )
    for test_name in TESTS
]
```

Some modules split tests across multiple lists (`TESTS`,
`PASSFAIL_TESTS`, `BIG_TESTS`, etc.) and define a combined
`ALL_TESTS = TESTS + PASSFAIL_TESTS + ...` that the comprehension
iterates over. Read the existing `BUILD` to find which list to
extend, and insert the new test name in whatever sort order that
file already uses (most modules are alphabetical).

## 5. Write unit tests (C++, if applicable)

Prefer C++ unit tests over Tcl for testing internal logic, algorithms,
and data structure operations. Use Tcl integration tests for
command-level behavior and end-to-end flows.

### Test file

Create `src/MODULE/test/cpp/TestName.cpp`:

```cpp
#include "gtest/gtest.h"
#include "odb/db.h"

// Simple test (no fixture)
namespace module {
namespace {

TEST(ModuleTest, TestDescription) {
  // ARRANGE - ACT - ASSERT
  EXPECT_EQ(expected, actual);
}

}  // namespace
}  // namespace module
```

### Using test fixtures

OpenROAD provides a fixture hierarchy for tests that need database
or tool setup. Choose the simplest one that covers your needs:

| Fixture | Use when |
|---------|----------|
| `tst::Fixture` | Need bare `dbDatabase` + STA, load LEFs manually |
| `tst::Nangate45Fixture` | Need pre-loaded Nangate45 tech/lib |
| `tst::Sky130Fixture` | Need pre-loaded Sky130 tech/lib |
| `tst::IntegratedFixture` | Need full tool integration (STA, DPL, GRT, RSZ) |

```cpp
#include "gtest/gtest.h"
#include "tst/nangate45_fixture.h"

class TestFeature : public tst::Nangate45Fixture
{
 protected:
  void SetUp() override { /* additional setup */ }
};

TEST_F(TestFeature, HandlesEdgeCase) {
  // block_, lib_ are available from the fixture
  EXPECT_TRUE(condition);
}
```

### CMake registration

Use whichever file the module already uses: `src/MODULE/test/cpp/CMakeLists.txt`
(e.g. odb, rsz, grt) or `src/MODULE/test/CMakeLists.txt` (e.g. gpl, dpl,
drt, cts). Match `WORKING_DIRECTORY` to the neighbouring tests.

```cmake
add_executable(TestName TestName.cpp)
target_link_libraries(TestName
    MODULE_lib
    GTest::gtest
    GTest::gtest_main
    tst
    odb
)
gtest_discover_tests(TestName
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/..
)
add_dependencies(build_and_test TestName)
```

### Bazel registration -- `src/MODULE/test/BUILD`

Add the `cc_test` target alongside the existing `regression_test`
entries. Most modules keep C++ tests in the same BUILD file:

```python
load("@rules_cc//cc:cc_test.bzl", "cc_test")

cc_test(
    name = "TestName",
    srcs = ["cpp/TestName.cpp"],
    deps = [
        "//src/MODULE",
        "//src/tst",
        "//src/tst:nangate45_fixture",  # if using Nangate45Fixture
        "@googletest//:gtest",
        "@googletest//:gtest_main",
    ],
)
```

## 6. Run and verify

Run through Bazel. Integration tests are named `<test>-tcl_test` (or
`<test>-py_test`); C++ tests use their `cc_test` name. Add
`--//:platform=gui` if the module or the binary you built needs the GUI.

```bash
bazelisk test //src/MODULE/test:TEST_NAME-tcl_test --test_output=errors

# Confirm Bazel discovers the target
bazelisk query 'tests(//src/MODULE/test/...)' | grep TEST_NAME
```

On failure, the log and diff against the golden are written to
`bazel-testlogs/src/MODULE/test/TEST_NAME-tcl_test/test.outputs/`
(zipped as `outputs.zip` unless you pass `--nozip_undeclared_test_outputs`).

`./regression -R TEST_NAME` and `ctest -N` only work if a CMake `build/`
directory exists; for CMake, just check the name was added to
`or_integration_tests(...)`.

## 7. Format and commit

```bash
# Format any C++ files (NEVER format src/sta/* or *.i files)
clang-format -i <changed-cpp-files>

git add src/MODULE/test/TEST_NAME.tcl \
        src/MODULE/test/TEST_NAME.ok \
        src/MODULE/test/CMakeLists.txt \
        src/MODULE/test/BUILD
git commit -s -m "MODULE: add TEST_NAME test

Test for DESCRIPTION."
```

## Checklist

- [ ] Test passes: `bazelisk test //src/MODULE/test:TEST_NAME-tcl_test`
- [ ] Golden file generated and committed
- [ ] Registered in CMake `CMakeLists.txt`
- [ ] Registered in Bazel `BUILD` (and any extra data files added to `regression_resources`)
- [ ] Test data is minimal (reuse existing files when possible)
- [ ] C++ files formatted with clang-format
- [ ] Commit is signed off (`-s`)

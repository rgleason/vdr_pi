Testing VDR
===========

The test/ directory contains a number of unit tests.
These are built by default but can be disabled by setting the
cmake configuration variable `BUILD_TESTING` to `OFF`.

A cmake build target `run-tests` is available to run the tests
using ctest.
This could be used on the command line like `make run-tests`.
It should also be visible in IDE target lists in for example
Visual Studio or CLion.

The tests are contained in a single `vdr_tests` binary  in
the build directory.
This can be run directly to get better control using the
various command line options listed by something like
`./vdr_tests --help` -- details are platform dependent.

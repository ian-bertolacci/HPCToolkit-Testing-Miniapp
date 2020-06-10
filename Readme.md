# HPCToolkit Testing Miniapp

Miniapp does really basic distributed-parallel reduce.
It's not meant to be fast, it's meant to help me learn how to use HPCToolkit.

## Quickstart

```
make viewall
```

That will launch both the trace (hpctraceviewer) and source (hpcviewer) profiles views, and resolve all the dependencies involved.

```
make view
```
To see only the source profile: `make viewprofile`
To see only the trace profile: `make viewtrace`

## All `make` Commands:

- `build` : Compile the miniapp executable.
- `run` : Run and profile the miniapp executable.
- `analyze` or `analyse` : Create HPCToolkit source structure and profile database.
- `viewprofile` : View the source profile of miniapp
- `viewtrace` : View the trace profile of the miniapp
- `viewall` : View both the source and trace profile of the miniapp
- `clean-bin` : Remove all compilation files.
- `clean-profile` : Remove all profiling files and directories.
- `clean` : Remove all compilation and profiling files and directories.

## Profiling Variables
There are three variables that can be set to change execution of the miniapp:
- `TEST_MPI_PROCESSES` : Number of MPI Processes to spawn
- `OMP_NUM_THREADS` : Number of OpenMP threads available to *EACH* process (e.g. 2 MPI processes and 4 OpenMP threads means 8 total OpenMP Threads) (Note this is a standard OpenMP environment variable)
- `TEST_NUM_ELEMENTS` : Number of elements in the distributed array.

They can either be set in the makefile or by exporting them in your shell.
Each profile and analysis of the miniapp is unique with respect to these variables, creating file and directory names specific to those settings.
As such, previous runs with different settings do not interfere with the make dependency resolution process and are preserved.

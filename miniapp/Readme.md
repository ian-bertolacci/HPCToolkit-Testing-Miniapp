# HPCToolkit Testing Miniapp

Miniapp does really basic distributed-parallel stencil-reduce.
It's not meant to be fast, it's meant to help me learn how to use HPCToolkit.

# Build and Analysis system

## Build
To build:
```bash
make build
```

## Profile
To create profile database:
```bash
make analyze
```
## All `make` Commands:
- `build` : Compile the miniapp executable.
- `run` : Run and profile the miniapp executable.
- `analyze` or `analyse` : Create HPCToolkit source structure and profile database.
- `clean-bin` : Remove all compilation files.
- `clean-profile` : Remove all profiling files and directories.
- `clean` : Remove all compilation and profiling files and directories.

## Profile Settings
There are three variables that can be set to change the profile configuration of  of the miniapp:
- `TEST_MPI_PROCESSES` : Number of MPI Processes to spawn.
  + Default: 4
- `TEST_OMP_NUM_THREADS` : Number of OpenMP threads available to *EACH* process (e.g. 2 MPI processes and 4 OpenMP threads means 8 total OpenMP Threads)
  + Default: the value of the standard OMP_NUM_THREADS environment variable, or 2 if unset.
- `TEST_NUM_ELEMENTS` : Number of elements in the distributed array.
  + Default: 1000
- `TEST_NUM_ITERATIONS` : Number of stencil-reduce iterations to perform.
  + Default: 1000
- `HPC_RUN_EVENTS` : list of one-or-more events that hpcrun will sample during profile run. Can include the sampling frequency or period.
  + Default: ( CPUTIME@f1000000 )

These can be set in a number of ways:
1. By assigning in the arguments to the make command  (preferred) (e.g. `make analyze TEST_MPI_PROCESSES=16 TEST_NUM_ELEMENTS=32`)
2. By modifying the makefile
3. By setting them in your shell environment and exporting them

Each profile and analysis of the miniapp is unique with respect to these variables, creating file and directory names specific to those settings.
As such, previous runs with different settings do not interfere with the make dependency resolution process and are preserved.

## Profile Database Names
Databases created by this system have the following name scheme:
`miniapp.exe_(fair)|(unfair)_procs-$(TEST_MPI_PROCESSES)_threads-$(TEST_OMP_NUM_THREADS)_n-elts-$(TEST_NUM_ELEMENTS)_n-iters-$(TEST_NUM_ITERATIONS)_trace-$(HPC_TRACE)_$(hpc_run_events_name)_metric-db-(yes)|(no).hpcdatabase`

# Using MiniApp standalone
The MiniApp can be used outside of the build system's profiling target: ```./miniapp.exe [optional arguments]```
This will not execute any HPCToolkit commands, and thus will not result in any profiling artifacts.

## MiniApp Arguments:
- `-h`
  + Print this message and exit.

- `-N <unsigned int>`
  + Set number of elements in distributed array.
  + Default: 10

- `-i <unsigned int>`
  + Set number of iterations to perform.
  + Default: 4

- `-n <unsigned int>`
  + Equivalent to -N <unsigned int>

- `-d <distribution type>`
  + Set type of distribution for elements of distributed array.
  + Values:
    * "fair" : Distribute values fairly.
    * "unfair" : Distribute values unfairly.

- `-w `
  + Force ranks to synchronize at each distributed array opperation.

- `-t <unsigned int>`
  + Set number of OpenMP threads.
  + Default: (system default)

- `-l <OpenMP schedule name>`
  + Set OpenMP loop schedule.
  + Values:
    * "default" : Use the default schedule, or schedule defined by the environment variable OMP_SCHEDULE.
    * "static"  : Use OpenMP static schedule.
    * "dynamic" : Use OpenMP dynamic schedule.
    * "guided"  : Use OpenMP quided schedule.
    * "auto"    : Use OpenMP auto schedule.
  + Default: "default"

- `-c <unsigned int>`
  + Set OpenMP chunk size.
  + Default: (system default)

- `-o <loop order>`
  + Set loop ordering method.
  + Values:
    * "default"  : Iterate over indices in the default implementation order.
    * "indirect" : Iterate over indices in ascending order (ignoring parallelism order) though an indirection array.
    * "random"   : Iterate over indices in random order through an indirection array.
  + Default : "default"

- `-v <string or int>`
  + Set verbosity level.
  + Values:
    * "normal" : Show standard messages.
    * "more"   : Show standard messages and some debugging info.
    * "less"   : Only show very important messages.
    * "errors" : Only show error messages.
    * "debug"  : Show standard messages and standard debugging info.
    * "all"    : Show all messages.
    * <int>    : Set to some priority integer value.
  + Default: "normal"

- `-q`
  + Set verbosity to quiet (equivalent to -v "less").

- `-s`
  + Set verbosity to silent (equivalent to -v "errors").

## MiniApp Parallelism
MiniApp is built with MPI and OpenMP.
To run with multiple MPI processes requires wrapping with `mpirun` or `mpispawn` (whichever is appropriate. The build system uses mpirun)
```bash
mpirun -np <number of processes> ./miniapp.exe [miniapp arguments]
```

TO run with multiple OpenMP threads requires using the `-t` argument for MiniApp:
```bash
./miniapp.exe -t <openmp threads> [other miniapp arguments]
```

The two can be combined:
```bash
mpirun -np <number of processes> ./miniapp.exe -t <openmp threads> [other miniapp arguments]
```

Each MPI process is given the specified number of threads.

For example, this command will run with 4 MPI processes, each using 2 OpenMP threads (for a total of 28 OpenMP threads)
```bash
mpirun -np 4 ./miniapp.exe -t 2
```

See (MiniApp Arguments)[#MiniApp-Arguments] for other arguments relating to OpenMp, in particular `-c` (OpenMP chunksize) and `-l` (OpenMP parallel  loop schedule).


# MiniApp Overview
The miniapp computation is a stencil application followed by sum reduction of a distributed, together in a loop a fixed number of times.
The stencil computation is simply `A'[i] = max( A[i-1], A[i], A[i+1] ) / (1 + abs( min( A[i-1], A[i], A[i+1]  ) ) )`.
At the bondaries, the invalid element is simply left out of the max/min calls.
Syncronization happens during the stencil operation between ranks containing adjacent portions of the array to update the local boundaries.
Synchronization happens during reduce between the primary and non-primary ranks to communicate local sums for the primary to compute the global sum.


# Notes
## Metric Databases
To use HPCToolkit metric databases, you (currently and unnecessarily) need to use an HPCToolkit build with MPI enabled.
This is unnecessary unless using another tool like Hatchett.
The (issue GitHub/HPCToolkit/hpctoolkit#289)[https://github.com/HPCToolkit/hpctoolkit/issues/289] may shed light on this.
I think I installed with Spack using `spack install hpctoolkit+mpi`

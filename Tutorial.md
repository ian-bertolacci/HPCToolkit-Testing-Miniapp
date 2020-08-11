# HPCToolkit Tutorial

This tutorial is a demo geared toward new HPCToolkit users and performance profiling novices.
The [HPCToolkit Manual](http://hpctoolkit.org/manual/HPCToolkit-users-manual.pdf) should be the go-to source for all questions related to using HPCToolkit software.

The demo in this tutorial uses the Miniapp program: a toy distributed stencil computation using MPI and OpenMP (see the [MiniApp readme](miniapp/Readme.md) for more details).

## Table of contents:
- [Quick-start](#quick-start)
- [Getting Started](#getting-started)
  + [Installing HPCToolkit](#Installing-HPCToolkit)
  + [Building the MiniApp](#Building-the-MiniApp)
- [Using HPCToolkit](#Using-HPCToolkit)
  + [Workflow](#Workflow)
  + [Analysis and Profiling](#Analysis-and-Profiling)
    * [Binaray Analaysis With HPCToolkit](#Binaray-Analaysis-With-HPCToolkit)
    * [Running Application With HPCToolkit](#Dynamic-Performance-Profiling-Application-With-HPCToolkit)
    * [Building Performance Database](#Building-Performance-Database)

  + [Viewing Profile](#Viewing-Profile)
    * [Source View (`hpcviewer`)](#Source-View-hpcviewer)
    * [Trace View (`hpctraceviewer`)](#Trace-View-hpctraceviewer)

# Quick-Start

## Full Build-Run-View
To fully build the MiniApp, run a benchmark, and view that benchmark's profiling output, simply run the quick-start script `examples/quickstart/basic_full.sh`.
You should be able to run this from anywhere.

I tried to make both basic_miniapp_run scripts fairly self explanatory.
See [examples/basic_miniapp_run/Readme.md](examples/basic_miniapp_run/Readme.md) for more details.

## Quick View
To skip the build and run parts, you can view a previous run of the basic_miniapp_run example by simply running `examples/quickstart/basic_view_only.sh`.
You should be able to run this from anywhere.

## MiniApp Fair/Unfair Profiles
Alternatively, you can run the analyses defined in the miniapp Makefile (`make analyze`) and view each database (all ending with the `.hpcdatabase` suffix) individually with `hpcviewer <database directory path>` or `hpcviewer <database directory path>`.
(Note that the hpcviewer application does not list the database path, which makes viewing multiple profiles simultaneously difficult.)

`make analyse` runs the  twice using all the same parameters but one: fairness.
One run uses a fair work distribution, and the other runs with an unfair work distribution.

See the [MiniApp readme](miniapp/Readme.md) for more details including how to change the MiniApp run configuration.


# Getting Started


## Installing HPCToolkit
I recommend using the [`spack`](https://spack.readthedocs.io/en/latest/getting_started.html).
The HPCToolkit compilation and test execution software can be installed with `spack install hpctoolkit`
The HPCToolkit visualization software can be installed with `spack install hpcviewer`
It is not necessary to have *both* packages installed on a system if it is only doing performance tests *xor* visualizing runs.


## Building the MiniApp
The miniapp should be easily built with `make -C miniapp build` (or just `make  build` while inside the miniapp directory).
This creates the miniapp executable, `miniapp.exe`
The only dependencies are an MPI installation (I've successfully used OpenMPI 3.1.6 and 1.10.2) and a viable C compiler.
This should work on any Unix-like system.

The [MiniApp readme](miniapp/Readme.md) has a more detailed explanation of the app and build system.


# Using HPCToolkit


## Workflow
Generally, there are 5+ stages involved in profiling an application with HPCToolkit.
1. Compilation.
  Typically this requires nothing more than compililing the application, ideally with debug information enabled.
  For applications that are statically linked, there is some rigmarole involved with linking using `hpclink`, but I have not yet needed to do that (despite having tried this on an application which I'm pretty confident is statically linked with a library the same source defines).
2. Static Binary Analysis.
  Using `hpcstruct` on the program's binary to create an binary analysis of the binary.
3. Execution Profiling.
  Wrapping the execution of the program using `hpcrun` to perform the run-time measurement of the specified performance event(s) or timer event(s).
4. Database Creation.
  `hpcprof`, using the output of the static binary analysis and dynamic performance analysis, along with the program source, creates a performance database


## Analysis and Profiling

### Compiling Applications With HPCToolkit
Compiling applications to be profiled with HPCToolkit does not require a special or separate build process.
I think the only additions that I use (which I'm not even sure are 100% necessar) are the additions of the C/C++ compiler flags `-gdwarf-2 -g3` which (at least for GNU compilers) enable more debugging information that is typically available.

The [HPCToolkit manual](http://hpctoolkit.org/manual/HPCToolkit-users-manual.pdf) discusses the use of `hpclink` for statically linked applications, but I haven't needed that, so I cant give much instruction there.


### Binaray Analaysis With HPCToolkit
Binary analysis is performed on an application with the `hpcstruct` command:
```bash
hpcstruct <application executable>
```

By default, this creates `<application executable>.hpcstruct`.
The analysis output file can be changed with the flag `-o <output file path>`

Below is a complete example of performing the binary analysis on the miniapp and writing the output to `miniapp.exe.hpcstruct` (which happens to be the default name):
```bash
cd miniapp
hpcstruct miniapp.exe -o miniapp.exe.hpcstruct
```


### Dynamic Performance Profiling Application With HPCToolkit
The most basic way to profile an application using HPCToolkit is with the following command:
```bash
hpcrun [HPCToolkit arguments] <application> [application arguments]
```
(8.6.2020 @ian-bertolacci Note:
  This *should* work for *any* application, including those that use MPI, but I'm having trouble getting it to work with the miniapp.
  In any-case, I'm pretty confident that it *use* to work, and invoking with mpirun works, and is how MPI applications should be run anyways.
  So I'm ignoring the issue.)

If the application runs with MPI, we further wrap this with a call to `mpirun` (or `mpispawn`, if you prefer):
```bash
mpirun [MPI arguments] hpcrun [HPCToolkit arguments] <application> [application arguments]
```

By default, this creates a directory `hpctoolkit-<application>-measurements` which contains the measurements data from the run.
(Note: this is not interacted with directly. It is input into the analysis software.)
The output directory can (and should) be changed with the flag `-o <output directory path>`.

By default, tracing is disabled.
To run with tracing enabled (which is necessary to create the trace view, described later), use the flag `-t`.
(8.6.2020 @ian-bertolacci Note:
  I'm not sure why tracing is not enabled by default.
  This makes me think that there could be a difference in the measurements created by hpcrun with/without tracing enabled.
  That's a concerning possibility, but in the worst case, you can just run the application twice with and without the tracing enabled.
  I do not do that, and simple run with tracing every time.)

By default, `hpcrun` measures using CPU time with the CPUTIME timer event.
The event or timer event used can be changed with the flag `-e <event name>`.
Multiple events can be used with multiple flag usages: `-e <event 1 name> -e <event 2 name>.`
The interval or frequency the sampling occurs can be set by the @ operator in the flag after the event.
The period between samples (number of events between a sample of the event) is set with the flag `-e <event name>@<period>`.
The frequency (in samples/second) of sampling the event is set with the flag `-e <event name>@f<frequency>` (note the `f` before the frequency).
The all available/supported events, and descriptions of them, can be listed with `hpcrun -l`.
I think higher frequency is suppose to give more accurate results, but has higher overhead.
I'm not sure how changing the period affects the measurements.
Supposedly mixing certain events and timer events has undesirable side effects.

Note: There are usually permissions issues with most events.
  If there is an error which reads similar to "Failed to initialize the <event name> event.: Permission denied", it is likely that `kernel.perf_event_paranoid` is set too high.
  This can be set with `sudo sysctl kernel.perf_event_paranoid=<level>` or `sudo sh -c 'echo <level> > /proc/sys/kernel/perf_event_paranoid'`, _*but you do so at your own security risk*_.
  The minimum level is probably dependent on what events are being measured, but I think that a level of 2 works, at least for what I tried.
  For UArizona systems, `kernel.perf_event_paranoid` level is (as of 8.6.2020) 1 for Ocelote and 2 for Puma.

Below is a complete example which runs the miniapp with 10000 elements and for 10000 iterations, using 2 processes and 2 threads, collecting metrics on the PERF_COUNT_HW_CACHE_MISSES event at a rate of 1000 samples/second, and putting the output in miniapp.exe.hpcmeasurements
```bash
cd miniapp
mpirun -np 2 hpcrun -t -e PERF_COUNT_HW_CACHE_MISSES@f1000 -o miniapp.exe.hpcmeasurements ./miniapp.exe -t 2 -n 10000 -i 10000
```


### Building Profile Database
After having performed the static binary analysis and the dynamic profiling, the profile database can now be created.
This requires access to the source used to compile the application's binary.

The database is created with `hpcprof`:
```bash
hpcprof -S <static binary analysis (hpcstruct) output file> <dynamic profiling (hpcrun) output directory> -I <path to program source>+
```

By default this creates the database directory `hpctoolkit-database`.
This can (and should) be changed with `-o <output directory path>`

The flag `-I` here has a similar meaning to `hpcprof` as it does to a compiler: the argument to the flag is the path to the application's source code directory.
To greatly simplify things, the path to the source code directory can be suffixed with the `+` operator to indicate that the directory should be searched recursively.
Note that this does require the *full* source, and not just the headers (which the compiler's `-I` interacts with).

Below is a complete example which creates the profile database directory miniapp.exe.hpcdatabase using the previously created analysis outputs:
```bash
cd miniapp
hpcprof -S miniapp.exe.hpcstruct miniapp.exe.hpcmeasurements -I ./+ -o miniapp.exe.hpcdatabase
```

## Viewing Profile
There are two views of the profile.
- The code-centric source view (using `hpcviewer`)
- The time-centric trace view (using `hpctraceviewer`)

The source view attaches a value from our event to lines of code.
The call tree can be explored in top-down/bottom-up/flat ways with calls sorted by the value of our events.

The trace view shows a timeline of function calls.

The trace view is useful for getting an overall picture, with the source view being better for more specific investigation and analysis.

The [HPCToolkit Manual](http://hpctoolkit.org/manual/HPCToolkit-users-manual.pdf) has a comprehensive description of the UIs for the viewer applications.

### Source View
Viewing a database using the source view is done with:
```bash
hpcviewer <path to profile database>
```


### Trace View
Viewing a database using the source view is done with:
```bash
hpctraceviewer <path to profile database>
```
Note: the database must have been created with measurements taken with with tracing enabled for hpcrun (`hpcrun -t`)


# Fair / Unfair Demo
The MiniApp can be configured to run with to work distributions:
- Fair: Each rank owns (and works on) `floor(N/processes)` elements of the distributed array, with the primary rank taking on the remainder (for a total of f`loor(N/processes) + N % processes` elements).
  This is a fair distribution.
  + `miniapp -d fair <other args>`
- Unfair: Each process owns (and works on) an amount of work such `sum( rank id in range(process), (rank id +1 )*work[rank]) == N`.
  This is to say, that the work is divided into `sum(processes)` pieces, and each rank gets `rank id + 1` of those.
  This is unfair, as ranks with lower rank ids get less work, and the higher rank ids get a lot more work.
  + `miniapp -d unfair <other args>`

The directory [examples/miniapp_UArizona_Puma](examples/miniapp_UArizona_Puma) has dozens of previous runs using the fair/unfair distribution across a range of sizes and iterations.

In this section, we will use the profile databases for a run with 4 processes, and 24 threads [examples/miniapp_UArizona_Puma/<DIR>](examples/miniapp_UArizona_Puma/<DIR>)

Alternatively, you can modify and run [examples/test_miniapp_fairness/run.sh](examples/test_miniapp_fairness/run.sh) and use the output profiling database

## View

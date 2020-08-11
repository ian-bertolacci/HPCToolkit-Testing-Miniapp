# HPCToolkit Demo

This is a demo for using HPCToolkit.
It is not comprehensive, nor is it necessarily "correct" in any sense.

# Contents
## MiniApp
(MiniApp Readme)[miniapp/Readme.md].

A MPI + OpenMP distributed array stencil + reduction 'mini-app' for playing around with HPCToolkit

In addition to the application it includes a small framework for executing profiling runs and creating profiling databases.

## Tutorial
(Tutorial)[tutorial.md].

An overview of using HPCToolkit with the included MiniApp and showing how to discover work-load imbalances with HPCToolkit

## Examples
(Examples)[examples].

A few scripted examples for running HPCToolkit, and some previously created profile databases if running the HPCToolkit profiling software or the miniapp is not possible for you (still requires using HPCToolkit's visualization software).

### Quick-Start
(Quickstart)[examples/quickstart].

A few no-nonsense run and get running scripts for running the miniapp or viewing previous miniapp runs.

(examples/quickstart/basic_full.sh)[examples/quickstart/basic_full.sh] will do a full build of MiniApp, run the (examples/basic_miniapp_run/run.sh)[examples/basic_miniapp_run/run.sh] script, and display the HPCToolkit views of the created profile database.

(examples/quickstart/basic_view_only.sh)[examples/quickstart/basic_view_only.sh] will only view the previously created profile database (examples/basic_miniapp_run/miniapp.exe.hpcdatabase)[examples/basic_miniapp_run/miniapp.exe.hpcdatabase].

### Basic MiniApp Run
(Basic MiniApp Run)[examples/basic_miniapp_run].

A pair of scripts that run the MiniApp under the same configuration.
They are written to demonstrate how HPCToolkit and an application are used together with different levels of rigmarole.

When unmodified, both scripts do the same thing.
However, (simple_run.sh)[examples/basic_miniapp_run/simple_run.sh] executes HPCToolkit and the MiniApp as a human on the commandline would.
The other, (run.sh)[examples/basic_miniapp_run/run.sh] executes HPCToolkit and the MiniApp as would be done in a more complicated script, using a significant number of variables and command construction to build the invocation before executing it, and is generally more configurable.

### MiniApp UArizona Puma
(MiniApp UArizona Puma)[examples/miniapp_UArizona_Puma].

This contains a plethora of previously created HPCToolkit profile databases of the MiniApp as run on the Puma supercomputer at UArizona.
It uses the MiniApp's profiling system, and so the database names directly reflect how the MiniApp was used.

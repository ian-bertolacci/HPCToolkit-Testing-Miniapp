#!/usr/bin/env bash

# Usage:
##   ./simple_run.sh

# Make hpcstruct object (creating the file miniapp.exe.hpcstruct)
echo "Performing binary analysis"
hpcstruct ../../miniapp/miniapp.exe -o miniapp.exe.hpcstruct

# Perform profiling (creating the directory miniapp_name.exe.hpcmeasurements containing the profile measurements)
## Run with 10000 elements and 10000 iterations (This is about how much work is needed to start seeing *any* measurements in the profile)
## Run with 2 mpi process and 2 OpenMP thread
## Measure CPUTIME at the event's standard rate.
echo "Performing profiling"
mpirun --oversubscribe -np 2 hpcrun -t -e CPUTIME -o miniapp.exe.hpcmeasurements ../../miniapp/miniapp.exe -t 2 -n 10000 -i 10000

# Make performance profiling database (creating the directory miniapp_name.exe.hpcdatabase containing the profile database)
echo -e "\nCreating performance database\n"
hpcprof -S miniapp.exe.hpcstruct -I ../../miniapp/+ miniapp.exe.hpcmeasurements -o miniapp.exe.hpcdatabase

# Output can be viewed with hpcviewer and hpctraceviewer:
## Source view:
###   hpcviewer miniapp.exe.hpcdatabase
## Trace view:
###   hpctraceviewer miniapp.exe.hpcdatabase

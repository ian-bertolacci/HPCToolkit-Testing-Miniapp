#!/usr/bin/env bash

# Make hpcstruct object (miniapp.hpcstruct)
echo "Performing binary analysis"
hpcstruct ../../miniapp/miniapp.exe -o miniapp.hpcstruct

# Perform profiling (creating measurements directory)
# Run with 10000 elements and 10000 iterations (This is about how much work is needed to start seeing *any* measurements)
# Run with 2 mpi process and 2 OpenMP thread
# Measure CPUTIME
echo "Performing profiling"
mpirun --oversubscribe -np 2 hpcrun -t -e CPUTIME -o miniapp.exe.hpcmeasurements ../../miniapp/miniapp.exe -t 2 -n 10000 -i 10000

# Make database
echo -e "\nCreating performance database\n"
hpcprof -S miniapp.hpcstruct -I ../../miniapp/+ miniapp.exe.hpcmeasurements -o miniapp.exe.hpcdatabase

# Output can be viewed with hpcviewer and hpctraceviewer
# Source view
#   hpcviewer miniapp.exe.hpcdatabase
# Trace view
#   hpctraceviewer miniapp.exe.hpcdatabase

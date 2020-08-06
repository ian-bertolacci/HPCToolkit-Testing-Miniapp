#!/usr/bin/env bash

# Make hpcstruct object (miniapp.hpcstruct)
echo "Performing binary analysis"
hpcstruct ../../mpi_omp/miniapp -o miniapp.hpcstruct

# Perform profiling (creating measurements directory)
echo "Performing profiling"
mpirun --oversubscribe -np 2 hpcrun -t -e CPUTIME -o miniapp.hpcmeasurements ../../mpi_omp/miniapp

# Make database
echo -e "\nCreating performance database\n"
hpcprof -S miniapp.hpcstruct -I ../../mpi_omp/+ miniapp.hpcmeasurements -o miniapp.hpcdatabase

# Output can be viewed with hpcviewer and hpctraceviewer
# Source view
#   hpcviewer miniapp.hpcdatabase
# Trace view
#   hpctraceviewer miniapp.hpcdatabase

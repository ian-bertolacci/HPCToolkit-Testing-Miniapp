#!/usr/bin/env bash

# Usage:
##   ./run.sh

# Basic test arguments
## The default configuration used here is:
### Run with 10000 elements and 10000 iterations (This is about how much work is needed to start seeing *any* measurements in the profile)
### Run with 2 mpi process and 2 OpenMP thread.
### Measure CPUTIME at the event's standard rate.

## Problem/work configuration
elements=10000
iterations=10000
work_distribution=fair

## Parallelism
processes=2
threads=2

## Profiling event
event="CPUTIME"

# Actual setup of commands
## Setup miniapp command
miniapp=../../miniapp/miniapp.exe
miniapp_source=$(dirname ${miniapp})
miniapp_name=$(basename ${miniapp})
miniapp_n_elements=${elements}
miniapp_iterations=${iterations}
miniapp_work_distribution=${work_distribution}
miniapp_threads=${threads}
miniapp_args="-n ${miniapp_n_elements} -i ${miniapp_iterations} -d ${miniapp_work_distribution} -t ${miniapp_threads}"
miniapp_command="${miniapp} ${miniapp_args}"

## Setting up MPI Command
mpi_spawn=mpirun
mpi_processes=${processes}
mpi_args="--oversubscribe -np ${mpi_processes}"
mpi_base_command="${mpi_spawn} ${mpi_args}"

## Setting up hpcstruct (static binary analysis) command
hpcstruct=hpcstruct
hpcstruct_output="./${miniapp_name}.hpcstruct"
hpcstruct_args="-o ${hpcstruct_output}"
hpcstruct_command="${hpcstruct} ${miniapp} ${hpcstruct_args}"

## Setting up hpcrun (dynamic performance profiling) command
hpcrun=hpcrun
hpcrun_event=${event}
hpcrun_output="${miniapp_name}.hpcmeasurements"
hpcrun_args="-t -o ${hpcrun_output} -e ${hpcrun_event}"
hpcrun_command="${mpi_base_command} ${hpcrun} ${hpcrun_args} ${miniapp_command}"

## Setting up hpcpfor (performance profile datababase creation) command
hpcprof=hpcprof
hpcprof_output="${miniapp_name}.hpcdatabase"
hpcprof_args="-S ${hpcstruct_output} -I ./+ ${hpcrun_output} -o ${hpcprof_output}"
hpcprof_command="${hpcprof} ${hpcprof_args}"

# Check for previous runs, exit if not clean
if [[ -e ${hpcstruct_output} ]] || [[ -e ${hpcrun_output} ]] || [[ -e ${hpcprof_output} ]]
then
  echo "Detected previous run. Please remove any hpctoolkit outputs from this directory."
  exit -1
fi

# Performing whole analysis and profiling
## Make hpcstruct object (creating the file miniapp.exe.hpcstruct)
echo -e "Performing binary analysis:\n${hpcstruct_command}"
eval ${hpcstruct_command}

## Perform profiling (creating the directory miniapp_name.exe.hpcmeasurements containing the profile measurements)
echo -e "\nPerforming Profiling analysis:\n${hpcrun_command}"
eval ${hpcrun_command}

## Make performance profiling database (creating the directory miniapp_name.exe.hpcdatabase containing the profile database)
echo -e "\nCreating performance database:\n${hpcprof_command}"
eval ${hpcprof_command}

# Output can be viewed with hpcviewer and hpctraceviewer:
## Source view:
###   hpcviewer miniapp.exe.hpcdatabase
## Trace view:
###   hpctraceviewer miniapp.exe.hpcdatabase

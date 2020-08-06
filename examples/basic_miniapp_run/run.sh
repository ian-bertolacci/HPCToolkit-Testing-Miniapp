#!/usr/bin/env bash

miniapp=../../mpi_omp/miniapp
miniapp_source=$(dirname ${miniapp})
miniapp_name=$(basename ${miniapp})
miniapp_args=""
miniapp_command="${miniapp} ${miniapp_args}"

mpi_spawn=mpirun
mpi_processes=2
mpi_args="--oversubscribe -np ${mpi_processes}"
mpi_base_command="${mpi_spawn} ${mpi_args}"

hpcstruct=hpcstruct
hpcstruct_output="./${miniapp_name}.hpcstruct"
hpcstruct_args="-o ${hpcstruct_output}"
hpcstruct_command="${hpcstruct} ${miniapp} ${hpcstruct_args}"

hpcrun=hpcrun
hpcrun_event="CPUTIME"
hpcrun_output="${miniapp_name}.hpcmeasurements"
hpcrun_args="-t -o ${hpcrun_output} -e ${hpcrun_event}"
hpcrun_command="${mpi_base_command} ${hpcrun} ${hpcrun_args} ${miniapp_command}"

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

# Make hpcstruct object (miniapp.hpcstruct)
echo -e "Performing binary analysis:\n${hpcstruct_command}"
eval ${hpcstruct_command}

# Perform profiling (creating measurements directory)
echo -e "\nPerforming Profiling analysis:\n${hpcrun_command}"
eval ${hpcrun_command}

# Make database
echo -e "\nCreating performance database:\n${hpcprof_command}"
eval ${hpcprof_command}

# Output can be viewed with hpcviewer and hpctraceviewer
# Source view
#   hpcviewer miniapp.hpcdatabase
# Trace view
#   hpctraceviewer miniapp.hpcdatabase

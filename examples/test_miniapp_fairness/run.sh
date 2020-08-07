#!/usr/bin/env bash

# Options worth configuring
miniapp_fairness_settings=( fair unfair )
miniapp_n_elements=100000
mpi_processes=2
hpcrun_events_array=( CPUTIME )

# Note: would like to use the below events, but they all require elevated privileges, MPI strongly suggests against that, and I agree.
# Alternatively, can set kernel.perf_event_paranoid=1, which also requires root privileges, and I'd like to avoid it. (https://superuser.com/questions/980632/run-perf-without-root-rights)
# perf::CACHE-MISSES PAPI_L1_DCM PAPI_L2_DCM PAPI_L3_DCM PAPI_FP_OPS PAPI_LD_INS PAPI_SR_INS CPUTIME

# Miniapp settings
miniapp=../../miniapp/miniapp.exe
miniapp_source=$(dirname ${miniapp})
miniapp_name=$(basename ${miniapp})

# MPI settings
mpi_spawn=mpirun
mpi_args="--oversubscribe -np ${mpi_processes}"
mpi_base_command="${mpi_spawn} ${mpi_args}"

# HPCToolkit settings
# hpcstruct settings
hpcstruct=hpcstruct
hpcstruct_output="./${miniapp_name}.hpcstruct"
hpcstruct_args="-o ${hpcstruct_output}"
hpcstruct_command="${hpcstruct} ${miniapp} ${hpcstruct_args}"

# hpcrun settings
hpcrun=hpcrun
hpcrun_base_args="-t"

# hpcprof settings
hpcprof=hpcprof
hpcprof_base_args="-S ${hpcstruct_output} -I ./+"

# Check for previous runs, exit if not clean
if [[ -e ${hpcstruct_output} ]]
then
  echo "Detected previous run. Please remove any hpctoolkit outputs from this directory."
  exit -1
fi

# Make hpcstruct object (miniapp.hpcstruct)
echo -e "Performing binary analysis:\n${hpcstruct_command}"
eval ${hpcstruct_command}

# Perform a run for each fairness test
for miniapp_fairness_setting in "${miniapp_fairness_settings[@]}"
do
  # Perfor a run for each hpcrun event
  for hpcrun_event in "${hpcrun_events_array[@]}"
  do
    # Name of run that uniquely identifies it
    run_name=${miniapp_name}.${miniapp_fairness_setting}.${hpcrun_event}

    # Perform profiling
    hpcrun_output="${run_name}.hpcmeasurements"
    hpcrun_args="${hpcrun_base_args} -o ${hpcrun_output} -e ${hpcrun_event}"
    hpcrun_command="${mpi_base_command} ${hpcrun} ${hpcrun_args} ${miniapp} -d ${miniapp_fairness_setting}"

    echo -e "\nPerforming Profiling analysis for \"${miniapp_fairness_setting}\" run with ${hpcrun_event} events:\n${hpcrun_command}"
    eval ${hpcrun_command}

    # Make database
    hpcprof_output="${run_name}.hpcdatabase"
    hpcprof_command="${hpcprof} ${hpcprof_base_args} ${hpcrun_output} -o ${hpcprof_output}"

    echo -e "\nCreating performance database:\n${hpcprof_command}"
    eval ${hpcprof_command}
  done
  done

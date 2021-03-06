#!/bin/bash

#interactive, debug, bsc_cs
#SBATCH --qos=debug
#SBATCH --job-name=cpic.@PARAM@
#SBATCH --workdir=/home/bsc15/bsc15557/cpic/perf/gridpoints
#SBATCH --output=log/@PARAM@.out
#SBATCH --error=log/@PARAM@.err
#SBATCH --ntasks=1
#SBATCH --cpus-per-task=32
#SBATCH --tasks-per-node=1
#SBATCH --time=00:30:00

#Not working cpu-bind=v,cores

. ../../modules

ulimit -s 8192

../../cpic conf/@PARAM@ > out/@PARAM@ 2> err/@PARAM@

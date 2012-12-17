#!/bin/bash

tce=$PWD/../../../../tce/
machine=${tce}/scheduler/testbench/ADF/ti64x_subset.adf
ttasim=${tce}/src/codesign/ttasim/ttasim
tcecc=${tce}/src/bintools/Compiler/tcecc
tce_ld=${tce}/src/ext/llvm-ld/tce-llvm-ld
optlevel=-O0
# this was default behavior
scheduler_flags=-O2
verbose=""
#verbose=-v

cd DENBench
rm -fr consumer/llvm-tce-systemtest 
./run_systemtest.sh -c $tcecc -t $ttasim -l $tce_ld -a $machine $optlevel -s "$scheduler_flags" $verbose | \
grep -v mp4decode

#!/bin/bash
EXPLORE_BIN="../../../../src/codesign/Explorer/explore"

PLUGIN="-e FrequencySweepExplorer"
TPEF="-d ../../../../systemtest/bintools/Scheduler/tests/DSPstone/fixed_point/matrix/matrix1"
DSDB="testi.dsdb"

RESULTS=($(${EXPLORE_BIN} ${PLUGIN} ${TPEF} \
-u verbose=0 -u start_freq_mhz=100 -u end_freq_mhz=150 -u step_freq_mhz=50 \
${DSDB} | grep -Ex '^[[:space:]]{1,}[0-9]{1,}' | grep -Eo '[0-9]{1,}' | xargs))

[ "${#RESULTS[@]}" -lt 2 ] && (echo "Not enough configures created."; exit 1)

for result in ${RESULTS[@]}; do
    ${EXPLORE_BIN} -w "$result" ${DSDB} 1>/dev/null
    rm -rf $result.{adf,idf}
done

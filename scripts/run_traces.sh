#! /bin/bash

traces=(
	"media"
	"compress"
	"fp"
	"infra"
	"web"
	"int"
)

if [ $# -ne 2 ]; then
    echo "Usage: run_traces.sh <cbp> <results-dir>"
    exit 1
fi

CBP_DIR="${2}"
CPPFLAGS="-D${1} -DPRINT_SIZE"
export CPPFLAGS

make shallow_clean
make

for trace in ${traces[@]}; do
    echo "===${trace}==="
    time python3 "scripts/trace_exec_training_list.py" --trace_dir "traces/15883615/${trace}" --results_dir "results/${CBP_DIR}"
    mv "results/${CBP_DIR}/results.csv" "results/${CBP_DIR}/${trace}_results.csv" || true
done


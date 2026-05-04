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
    echo "Usage: run_training_traces.sh <cbp> <results-dir>"
    exit 1
fi

CBP_DIR="${2}"
CPPFLAGS="-D${1} -DPRINT_SIZE"
export CPPFLAGS

make shallow_clean
make

time python3 "scripts/trace_exec_training_list.py" --trace_dir "traces/training_set" --results_dir "results/${CBP_DIR}"
cp "results/${CBP_DIR}/results.csv" "results/${CBP_DIR}/results.bak.csv"
python3 "scripts/fix_training_set_workload_labels.py" "results/${CBP_DIR}/results.bak.csv" "results/${CBP_DIR}/results.csv"


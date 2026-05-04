#! /bin/bash

models=(
	"TAGE2006"
	"TAGE2016"
	"TAGE2016COOKBOOK"
	"SKYLAKE"
	"FIRESTORM"
	"ORYON"
)

if [ $# -ne 0 ]; then
    echo "Usage: run_all_models.sh"
    exit 1
fi

for model in ${models[@]}; do
    echo "===${model}==="
    "./scripts/run_traces.sh" ${model} ${model} || true
done


import numpy as np
import argparse
import os
import re

GET_ALL = False

def get_collisions(log_dir):
    pattern = re.compile(r"Unused entries:\s*(\d+)/(\d+)\s*\(([\d.]+)")
    #pattern = re.compile(r"Entries w/ collisions:\s*(\d+)/(\d+)\s*\(([\d.]+)")

    # Get files only (skip subdirectories), sorted for deterministic row order
    files = sorted(
        f for f in os.listdir(log_dir)
        if os.path.isfile(os.path.join(log_dir, f))
    )

    if not files:
        raise ValueError("No files found in directory.")

    rows = []
    expected_matches = None

    for filename in files:
        path = os.path.join(log_dir, filename)

        with open(path, "r") as f:
            matches = []

            for line in f:
                m = pattern.search(line)
                if m:
                    if GET_ALL:
                        matches.append(np.array([float(m.group(1)), float(m.group(2))]))
                    else:
                        matches.append(float(m.group(3)))
#        if GET_ALL:
            matches.pop()

        if expected_matches is None:
            expected_matches = len(matches)
            if expected_matches == 0:
                raise ValueError(
                    f"No matching lines found in first file: {filename}"
                )
        elif len(matches) != expected_matches:
            raise ValueError(
                f"Inconsistent number of matches in {filename}: "
                f"expected {expected_matches}, found {len(matches)}"
            )

        rows.append(matches)

    return np.array(rows, dtype=float), files


parser = argparse.ArgumentParser()
parser.add_argument("workload", nargs=1)
parser.add_argument("models", nargs="+")
args = parser.parse_args()

results = []
workload = args.workload[0]
for model in args.models:
    if "hypotheses" in workload:
        colls, files = get_collisions(f"results/hypotheses/{model}/training_set")
    else:
        colls, files = get_collisions(f"results/{model}/{workload}")
    sum_trace = colls.sum(axis=1)
    avg_pht = colls.mean(axis=0)
    med_pht = np.median(colls, axis=0)

    min_i = sum_trace.argmin()
    max_i = sum_trace.argmax()

    print(f"{model}:")
    if GET_ALL:
        perc = (sum_trace[:, 0] / sum_trace[:, 1]) * 100
        print(f"\tAvg:\t{perc.mean():8.4f}\\%")
        #print(f"\tAvg:\t{sum_trace.mean():8.0f}")
        #print(f"\tMed:\t{np.median(sum_trace):8.0f}")
        #print(f"\tMin:\t{sum_trace[min_i]}\t({os.path.splitext(files[min_i])[0]})")
        #print(f"\tMax:\t{sum_trace[max_i]}\t({os.path.splitext(files[max_i])[0]})")
        print()
        continue

    for i in range(colls.shape[1]):
        #name = f"PHT #{colls.shape[1] - i - 1}" if i < colls.shape[1] - 1 else "Base"
        name = f"PHT #{colls.shape[1] - i}"
        print(f"\t{name}:\t{avg_pht[i]:8.2f}\\%")
        #print(f"\t{name}:")
        #print(f"\t\tAvg:\t{avg_pht[i]:8.2f}")
        #print(f"\t\tMed:\t{med_pht[i]:8.0f}")
    print()


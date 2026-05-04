import numpy as np
import sys

def percent_to_float(x):
    return float(x.rstrip('%'))

def load(path):
    return np.genfromtxt(path, dtype=None, delimiter=',', names=True, converters={12: percent_to_float, 24: percent_to_float})

if len(sys.argv) < 3:
    print("Usage: analyze_results <workload> <predictors...>")
    sys.exit()

workload = sys.argv[1]
names = [sys.argv[i] for i in range(2, len(sys.argv))]
if "hypotheses" in workload:
    stats = [load(f"results/hypotheses/{name}/results.csv") for name in names]
elif "all" in workload:
    workloads = [
            "media",
            "compress",
            "fp",
            "infra",
            "web",
            "int",
            ]
    stats = []
    for name in names:
        results = [load(f"results/{name}/{workload}_results.csv") for workload in workloads]
        stats.append(np.concatenate(results, dtype=results[0].dtype))
else:
    stats = [load(f"results/{name}/{workload}_results.csv") for name in names]

# TODO Group per workload

for i in range(len(names)):
    print(f"{names[i]}:")
    data = stats[i]
    for field in [
            "MR",
            "MPKI",
            "50PercMR",
            "50PercMPKI",
            ]:
        print(f"\t{field}:")
        field_data = np.array(data[field])
        avg = np.mean(field_data)
        med = np.median(field_data)
        #std = np.std(field_data)
        #min_idx = np.argmin(field_data)
        #max_idx = np.argmax(field_data)
        suffix = '%' if "MR" in field else ''
        print(f"\t\tAvg:\t{avg:8.4f}{suffix}")
        print(f"\t\tMed:\t{med:8.4f}{suffix}")
        # if not "MR" in field:
        #     print(f"\t\tStd:\t{std:8.4f}{suffix}")
        #print(f"\t\tMin:\t{field_data[min_idx]:8.4f}{suffix} ({data["Run"][min_idx]})")
        #print(f"\t\tMax:\t{field_data[max_idx]:8.4f}{suffix} ({data["Run"][max_idx]})")
    print()

for i in range(len(names)):
    for j in range(i):
        print(f"\n{names[j]} vs {names[i]}:")
        data1 = stats[j]
        data2 = stats[i]
        for field in ["MR", "MPKI", "50PercMR", "50PercMPKI"]:
            print(f"\t{field} diff:")
            diff = np.array(data1[field]) - np.array(data2[field])
            diff_abs = np.absolute(diff)
            avg = np.mean(diff)
            std = np.std(diff)
            min_idx = np.argmin(diff_abs)
            max_idx = np.argmax(diff_abs)
            suffix = '%' if "MR" in field else ''
            print(f"\t\tAvg:\t{avg:8.4f}{suffix}")
            # if not "MR" in field:
            #     print(f"\t\tStd:\t{std:8.4f}{suffix}")
            print(f"\t\tMin:\t{diff[min_idx]:8.4f}{suffix} ({data1["Run"][min_idx]})")
            print(f"\t\tMax:\t{diff[max_idx]:8.4f}{suffix} ({data1["Run"][max_idx]})")
        print()


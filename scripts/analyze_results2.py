import numpy as np
import sys

def percent_to_float(x):
    return float(x.rstrip('%'))

def load(path):
    return np.genfromtxt(path, dtype=None, delimiter=',', names=True, converters={12: percent_to_float, 24: percent_to_float})

for field in ["MR", "MPKI", "50PercMR", "50PercMPKI"]:
    print(f"\n\n{field}:")
    for model in ["SKYLAKE", "FIRESTORM", "ORYON", "TAGE2006", "TAGE2016", "TAGE2016COOKBOOK"]:
        print(f"\\multirow{{2}}{{*}}{{{model}}}", end="")
        for is_avg in [True, False]:
            for workload in [
                "compress",
                "fp",
                "infra",
                "int",
                "media",
                "web",
                "all",
                ]:
                if "all" in workload:
                    workloads = [
                        "media",
                        "compress",
                        "fp",
                        "infra",
                        "web",
                        "int",
                    ]
                    results = [load(f"results/{model}/{workload}_results.csv") for workload in workloads]
                    data = np.concatenate(results, dtype=results[0].dtype)
                else:
                    data = load(f"results/{model}/{workload}_results.csv")

                field_data = np.array(data[field])
                suffix = '\\%' if "MR" in field else ''
                if is_avg:
                    v = np.mean(field_data)
                else:
                    v = np.median(field_data)
                print(f" & {v:8.4f}{suffix}", end="")
            print(" \\\\")
        print("\\hline")
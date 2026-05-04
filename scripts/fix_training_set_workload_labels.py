import sys

if len(sys.argv) != 3:
    print("Usage: python3 fix_training_set_workload_labels.py input.csv output.csv")
    sys.exit(1)

with open(sys.argv[1], "r") as infile, open(sys.argv[2], "w") as outfile:
    # Copy header unchanged
    outfile.write(infile.readline())

    for line in infile:
        cols = line.rstrip("\n").split(",")

        if len(cols) >= 2 and "_" in cols[1]:
            cols[0] = cols[1].split("_", 1)[0]

        outfile.write(",".join(cols) + "\n")


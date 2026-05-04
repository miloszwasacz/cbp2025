import csv
import shutil
import sys
from pathlib import Path


def main():
    if len(sys.argv) != 4:
        print(
            "Usage: python3 extract_training_set.py "
            "<reference_dir> <full_results_dir> <output_dir>"
        )
        sys.exit(1)

    reference_dir = Path(sys.argv[1]) / "training_set"
    input_dir = Path(sys.argv[2])
    output_dir = Path(sys.argv[3]) / "training_set"
    input_csv = lambda w: Path(sys.argv[2]) / f"{w}_results.csv"
    output_csv = Path(sys.argv[3]) / "results.csv"

    # Validate inputs
    for p, label in [
        (reference_dir, "Reference directory"),
        (input_dir, "Input directory"),
    ]:
        if not p.is_dir():
            print(f"Error: {label} not found: {p}")
            sys.exit(1)

    #if not input_csv.is_file():
    #    print(f"Error: CSV not found: {input_csv}")
    #    sys.exit(1)

    output_dir.mkdir(parents=True, exist_ok=True)
    output_csv.parent.mkdir(parents=True, exist_ok=True)

    # Reference filenames and stems
    reference_files = [f for f in reference_dir.iterdir() if f.is_file()]
    reference_filenames = {f.name for f in reference_files}
    reference_stems = {f.stem for f in reference_files}
    workloads = set()

    # -------------------------
    # Copy matching files
    # -------------------------
    copied_count = 0
    for f in reference_filenames:
        workload = f.split('_', 1)[0]
        workloads.add(workload)
        shutil.copy(input_dir / workload / f, output_dir / f)
        copied_count += 1
    #for f in input_dir.iterdir():
    #    if f.is_file() and f.name in reference_filenames:
    #        shutil.copy2(f, output_dir / f.name)
    #        print(f"Copied file: {f.name}")
    #        copied_count += 1

    # -------------------------
    # Filter CSV rows
    # -------------------------
    rows_kept = 0

    with open(output_csv, "w", newline="", encoding="utf-8") as fout:
        header_added = False
        for workload in sorted(workloads):
            with open(input_csv(workload), newline="", encoding="utf-8") as fin:

                reader = csv.reader(fin)
                writer = csv.writer(fout)

                # Preserve header
                try:
                    header = next(reader)
                    if not header_added:
                        writer.writerow(header)
                        header_added = True
                except StopIteration:
                    print("Input CSV is empty.")
                    sys.exit(0)

                for row in reader:
                    # Keep row if ANY cell exactly matches a reference stem
                    if row[1].strip() in reference_stems:
                        writer.writerow(row)
                        rows_kept += 1

    print(f"Done.")
    print(f"Copied {copied_count} matching file(s).")
    print(f"Retained {rows_kept} CSV row(s).")
    print(f"Filtered CSV written to: {output_csv}")


if __name__ == "__main__":
    main()


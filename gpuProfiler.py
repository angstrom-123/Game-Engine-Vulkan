#!/usr/bin/env python3

SKIP_READINGS = 100

def clean_line(line):
    res = list()
    for s in line.split(","):
        s = s.strip()
        if len(s) > 0:
            res.append(s)
    return res

def read_timings(filename):
    with open(filename, "r") as f:
        lines = f.readlines();
        if (len(lines) < SKIP_READINGS):
            print(f"Not enough valid readings (<{SKIP_READINGS}).")
            exit()

        heading_line = lines[0]
        valid_lines = lines[SKIP_READINGS:]
        
        headings = clean_line(heading_line)

        timings = [0.0] * len(headings)
        for line in valid_lines:
            l = clean_line(line)
            for i, v in enumerate(l):
                timings[i] += float(v)

        n = len(valid_lines)
        timings = [t / n for t in timings]

        return (headings, timings)

def show_timings(results):
    headings, timings = results
    
    print(f"{"Heading":<16}|  {"Average ms":<16}")
    print("---------------------------------")
    for i in range(len(headings)):
        print(f"{headings[i]:<16}|  {timings[i]:<16.2f}")

def main():
    results = read_timings("gpu_results.txt")
    show_timings(results)

main()

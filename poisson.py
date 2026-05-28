#!/usr/bin/env python3

import random
import math
import matplotlib.pyplot as plt 

POISSON_SAMPLES = 32
POISSON_RADIUS = 0.25
POISSON_ATTEMPTS = 100
POISSON_RETRIES = 10

class Vec2:
    def __init__(self, x, y):
        self.x = x 
        self.y = y

    def random():
        return Vec2(random.random() * 2 - 1, random.random() * 2 - 1)

    def distance(a, b):
        return math.sqrt(((b.x - a.x)**2) + ((b.y - a.y)**2))

def generate_poisson_disk():
    point_set = set()
    centre = Vec2(0, 0)

    n = POISSON_SAMPLES
    r = POISSON_RADIUS
    t = POISSON_ATTEMPTS

    attempts = 0
    while len(point_set) < n and attempts < t:
        candidate_point = Vec2.random()

        # Skip over points outside the radius 1 circle
        if Vec2.distance(candidate_point, centre) > 1:
            continue

        # Check that the point is far enough from all others
        valid = True
        for existing_point in point_set:
            if Vec2.distance(candidate_point, existing_point) < r:
                valid = False 
                break

        # Add to disk if valid
        if valid:
            point_set.add(candidate_point)
            attempts = 0
        else:
            attempts += 1

    return point_set

def write_disk_to_file(disk, filename):
    with open(filename, "w") as f:
        f.write(f"#define POISSON_SAMPLES {len(disk)}\n")
        f.write(f"const vec2 poissonDisk[{len(disk)}] = vec2[{len(disk)}](\n")
        for i, point in enumerate(disk):
            f.write(f"    vec2({point.x}, {point.y})")
            # Add comma if not final item
            if i < len(disk) - 1:
                f.write(",")
            f.write("\n")
        f.write(");\n")
    print(f"Poisson disk written to '{filename}' with {len(disk)} samples")

def visualize_disk(disk):
    plt.figure(figsize=(6,6))

    # Points
    xs = [p.x for p in disk]
    ys = [p.y for p in disk]
    plt.scatter(xs, ys, c="red", s=50, edgecolors="black")

    plt.axis("equal")
    plt.xlim(-1.1, 1.1)
    plt.ylim(-1.1, 1.1)
    plt.title(f"Poisson Disk ({len(disk)} samples)")
    plt.grid(alpha=0.3)
    plt.show()

def main():
    disk = generate_poisson_disk()

    retries = 1
    while len(disk) < POISSON_SAMPLES:
        if retries > POISSON_RETRIES:
            print(f"[ERROR]: Failed to generate poisson disk with samples={POISSON_SAMPLES} and radius={POISSON_RADIUS}")
            exit()

        disk = generate_poisson_disk()
        retries += 1

    write_disk_to_file(disk, "src/Engine/Resource/Shader/poisson.glsl")
    # visualize_disk(disk)

main()

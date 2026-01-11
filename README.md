# Index Hash Table

A **fixed-capacity, in-process hash table** designed for **high-performance caching** with **predictable memory usage**.

This project implements an open-addressing hash table intended for low-latency lookups in performance-sensitive code paths, where dynamic growth and unbounded memory allocation are undesirable.

---

## Motivation

In many systems-level and analytics workloads, a cache must be:

- fast and predictable
- bounded in memory
- simple to reason about
- safe to use in hot paths

General-purpose hash tables often trade determinism for flexibility.  
This project explores a different point in the design space: **fixed capacity, explicit trade-offs, and minimal overhead**.

---

## Design Goals

- **Fixed capacity**  
  Memory usage is defined up-front and does not grow.

- **In-process only**  
  No IPC, no shared memory, no persistence.

- **Open addressing**  
  Avoids pointer chasing and minimizes allocations.

- **Low-latency focus**  
  Optimized for fast lookups under high load factors.

- **Explicit behavior**  
  Limitations and failure modes are intentional and visible.

---

## Non-Goals

This project intentionally does **not** aim to be:

- a general-purpose container
- thread-safe (by default)
- dynamically resizable
- a replacement for `unordered_map`

If you need those features, this is not the right tool.

---

## Status

⚠️ **Work in progress**

This repository is currently used for:
- experimentation
- correctness testing
- performance exploration

APIs, internal structures, and behavior may change.

---

## Testing

The project includes:
- deterministic unit tests
- Stress-style tests to validate correctness under load

Test coverage will evolve as the design stabilizes.

---

## Usage

At this stage, the code is intended for **experimentation and learning** rather than production use.

Basic usage examples and API documentation will be added once the core behavior settles.

---

## License

No license is specified yet.

All rights are reserved until a license is added.

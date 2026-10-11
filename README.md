# OS Memory Allocator - Custom Heap Manager

![Language](https://img.shields.io/badge/language-C-blue.svg)
![Platform](https://img.shields.io/badge/platform-Linux-orange.svg)
![License](https://img.shields.io/badge/license-BSD--3--Clause-green.svg)

## 📖 Overview

**OS Memory Allocator** is a custom dynamic memory management library that implements the standard C allocation functions: `malloc`, `calloc`, `free`, and `realloc`.

The project focuses on efficient memory usage through strategies like **heap pre-allocation**, **memory block splitting**, and **block coalescing**. It interfaces directly with the Linux kernel using `brk` for small heap allocations and `mmap` for large memory requests, providing a robust alternative to the standard `glibc` allocator.

---

## 🚀 Features

### 1. Allocation Strategies

* **Heap Pre-allocation:** To minimize the number of system calls, the allocator pre-allocates a large chunk of memory (128 KB) using `brk` upon the first request.
* **Threshold Management:** Uses a specific threshold (`MMAP_THRESHOLD = 128 KB`) to decide whether to allocate memory on the heap (via `brk`) or create a separate mapped region (via `mmap`).
* **Best-Fit Strategy:** Scans the free list to find the smallest available block that satisfies the requested size, reducing internal fragmentation.

### 2. Efficiency Mechanisms

* **Block Splitting:** When a free block is significantly larger than the requested size, it is split into two: one for the current allocation and a new free block for future use.
* **Block Coalescing:** When `free()` is called, the allocator automatically merges adjacent free blocks in the heap to prevent memory fragmentation.
* **Heap Expansion:** If no suitable block is found in the existing heap, the allocator expands the last block or requests more space from the OS.

### 3. Core API Implementation

* **`os_malloc`:** The primary allocator that handles alignment, block searching, and OS-level memory requests.
* **`os_calloc`:** Allocates memory for an array of elements, initializes the memory to zero, and uses a lower `mmap` threshold (page size) for specialized usage.
* **`os_realloc`:** Resizes existing memory blocks, attempting to expand the current block in place or merging it with the next free block before moving the data to a new location.
* **`os_free`:** Releases memory back to the allocator (marking it as free) or unmaps it from the process address space if it was allocated via `mmap`.

### 4. Metadata Management

Each memory block is preceded by a `block_meta` structure that tracks its state:

```c
struct block_meta {
    size_t size;          // Size of the data payload
    int status;           // STATUS_FREE, STATUS_ALLOC, or STATUS_MAPPED
    struct block_meta *prev;
    struct block_meta *next;
};
```

---

## 📂 Project Structure

The repository is organized to separate the core logic from utility functions:

```
.
├── osmem.c             # Main implementation of the allocation logic
├── osmem.h             # Public API definitions (os_malloc, os_free, etc.)
├── block_meta.h        # Internal metadata structures and DIE macro
├── printf.c            # Tiny printf implementation for debugging
├── printf.h            # Header for utility functions
├── Makefile            # Build system (generates libosmem.so)
└── README.md           # Project documentation
```

---

## 🛠️ Technical Details

**Memory Alignment:**
All allocations are aligned to an 8-byte boundary using the `ALIGN` macro to ensure optimal CPU access and prevent faults.

**Error Handling:**
Employs a robust `DIE` macro for system call validation. If a kernel-level call (`sbrk`, `mmap`, `munmap`) fails, the program terminates with a descriptive error message and the corresponding `errno`.

**Shared Library:**
The allocator is compiled with `-fPIC` (Position Independent Code) and linked as a shared object (`libosmem.so`), allowing it to be dynamically linked or injected into existing binaries.

---

## ⚙️ Building the Library

### Prerequisites

* **Compiler:** GCC (tested on version 13.3)
* **Build Tool:** GNU Make
* **OS:** Linux (requires `sys/mman.h` and `unistd.h`)

### Compilation

To build the library, simply run the following command in the project root:

```bash
make
```

This will produce:

* `osmem.o` & `printf.o` — Intermediate object files
* `libosmem.so` — The final shared library

---

### Workspace Maintenance

To remove compiled files and reset the directory:

```bash
make clean
```

To create a source archive:

```bash
make pack
```

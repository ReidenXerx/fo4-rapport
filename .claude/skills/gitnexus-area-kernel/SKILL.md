---
name: gitnexus-area-kernel
description: "Skill for the Kernel area of fo4-rapport. 40 symbols across 9 files."
---

# Kernel

40 symbols | 9 files | Cohesion: 92%

## When to Use

- Working with code in `extern/`
- Understanding how calloc, malloc, calloc work
- Modifying kernel-related functionality

## Key Files

| File | Symbols |
|------|---------|
| `extern/CommonLibF4/CommonLibF4/include/RE/Scaleform/Kernel/SF_RefCount.h` | Ptr, TryAttach, TryDetach, operator=, reset (+2) |
| `extern/CommonLibF4RD/CommonLibF4/include/RE/Scaleform/Kernel/SF_RefCount.h` | Ptr, TryAttach, TryDetach, operator=, reset (+2) |
| `extern/CommonLibF4/CommonLibF4/include/RE/Scaleform/Kernel/SF_Memory.h` | calloc, malloc, Alloc, Free, GetGlobalHeap (+1) |
| `extern/CommonLibF4RD/CommonLibF4/include/RE/Scaleform/Kernel/SF_Memory.h` | calloc, malloc, Alloc, Free, GetGlobalHeap (+1) |
| `extern/CommonLibF4/CommonLibF4/include/RE/Scaleform/Kernel/SF_Threads.h` | AcquireInterface, Event, Mutex, Waitable |
| `extern/CommonLibF4RD/CommonLibF4/include/RE/Scaleform/Kernel/SF_Threads.h` | AcquireInterface, Event, Mutex, Waitable |
| `extern/CommonLibF4RD/CommonLibF4/include/RE/Scaleform/Kernel/SF_Allocator.h` | AllocatorPagedCC, ConstructorMov, ConstructorPagedMovCC |
| `extern/CommonLibF4/CommonLibF4/include/RE/Scaleform/Kernel/SF_ArrayPaged.h` | ConstructorPagedMov, ConstructorPagedMovCC |
| `extern/CommonLibF4/CommonLibF4/include/RE/Scaleform/Kernel/SF_Allocator.h` | ConstructorMov |

## Entry Points

Start here when exploring this area:

- **`calloc`** (Function) — `extern/CommonLibF4/CommonLibF4/include/RE/Scaleform/Kernel/SF_Memory.h:137`
- **`malloc`** (Function) — `extern/CommonLibF4/CommonLibF4/include/RE/Scaleform/Kernel/SF_Memory.h:115`
- **`calloc`** (Function) — `extern/CommonLibF4RD/CommonLibF4/include/RE/Scaleform/Kernel/SF_Memory.h:137`
- **`malloc`** (Function) — `extern/CommonLibF4RD/CommonLibF4/include/RE/Scaleform/Kernel/SF_Memory.h:115`
- **`operator==`** (Function) — `extern/CommonLibF4/CommonLibF4/include/RE/Scaleform/Kernel/SF_RefCount.h:268`

## Key Symbols

| Symbol | Type | File | Line |
|--------|------|------|------|
| `Ptr` | Class | `extern/CommonLibF4/CommonLibF4/include/RE/Scaleform/Kernel/SF_RefCount.h` | 70 |
| `Ptr` | Class | `extern/CommonLibF4RD/CommonLibF4/include/RE/Scaleform/Kernel/SF_RefCount.h` | 70 |
| `AcquireInterface` | Class | `extern/CommonLibF4/CommonLibF4/include/RE/Scaleform/Kernel/SF_Threads.h` | 10 |
| `Event` | Class | `extern/CommonLibF4/CommonLibF4/include/RE/Scaleform/Kernel/SF_Threads.h` | 11 |
| `Mutex` | Class | `extern/CommonLibF4/CommonLibF4/include/RE/Scaleform/Kernel/SF_Threads.h` | 12 |
| `Waitable` | Class | `extern/CommonLibF4/CommonLibF4/include/RE/Scaleform/Kernel/SF_Threads.h` | 14 |
| `AcquireInterface` | Class | `extern/CommonLibF4RD/CommonLibF4/include/RE/Scaleform/Kernel/SF_Threads.h` | 10 |
| `Event` | Class | `extern/CommonLibF4RD/CommonLibF4/include/RE/Scaleform/Kernel/SF_Threads.h` | 11 |
| `Mutex` | Class | `extern/CommonLibF4RD/CommonLibF4/include/RE/Scaleform/Kernel/SF_Threads.h` | 12 |
| `Waitable` | Class | `extern/CommonLibF4RD/CommonLibF4/include/RE/Scaleform/Kernel/SF_Threads.h` | 14 |
| `ConstructorMov` | Class | `extern/CommonLibF4/CommonLibF4/include/RE/Scaleform/Kernel/SF_Allocator.h` | 31 |
| `ConstructorPagedMov` | Class | `extern/CommonLibF4/CommonLibF4/include/RE/Scaleform/Kernel/SF_ArrayPaged.h` | 13 |
| `ConstructorPagedMovCC` | Class | `extern/CommonLibF4/CommonLibF4/include/RE/Scaleform/Kernel/SF_ArrayPaged.h` | 19 |
| `AllocatorPagedCC` | Class | `extern/CommonLibF4RD/CommonLibF4/include/RE/Scaleform/Kernel/SF_Allocator.h` | 36 |
| `ConstructorMov` | Class | `extern/CommonLibF4RD/CommonLibF4/include/RE/Scaleform/Kernel/SF_Allocator.h` | 21 |
| `ConstructorPagedMovCC` | Class | `extern/CommonLibF4RD/CommonLibF4/include/RE/Scaleform/Kernel/SF_Allocator.h` | 28 |
| `calloc` | Function | `extern/CommonLibF4/CommonLibF4/include/RE/Scaleform/Kernel/SF_Memory.h` | 137 |
| `malloc` | Function | `extern/CommonLibF4/CommonLibF4/include/RE/Scaleform/Kernel/SF_Memory.h` | 115 |
| `calloc` | Function | `extern/CommonLibF4RD/CommonLibF4/include/RE/Scaleform/Kernel/SF_Memory.h` | 137 |
| `malloc` | Function | `extern/CommonLibF4RD/CommonLibF4/include/RE/Scaleform/Kernel/SF_Memory.h` | 115 |

## How to Explore

1. `context({name: "calloc"})` — see callers and callees
2. `query({search_query: "kernel"})` — find related execution flows
3. Read key files listed above for implementation details
4. `explain({target: "<file or symbol>"})` — persisted taint findings (source→sink data flows), when indexed with `--pdg`

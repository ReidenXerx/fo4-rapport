---
name: gitnexus-area-netimmerse
description: "Skill for the NetImmerse area of fo4-rapport. 46 symbols across 19 files."
---

# NetImmerse

46 symbols | 19 files | Cohesion: 88%

## When to Use

- Working with code in `extern/`
- Understanding how operator<=>, operator==, operator<=> work
- Modifying netimmerse-related functionality

## Key Files

| File | Symbols |
|------|---------|
| `extern/CommonLibF4/CommonLibF4/include/RE/NetImmerse/NiSmartPointer.h` | NiPointer, TryAttach, TryDetach, operator=, reset (+3) |
| `extern/CommonLibF4RD/CommonLibF4/include/RE/NetImmerse/NiSmartPointer.h` | NiPointer, TryAttach, TryDetach, operator=, reset (+3) |
| `extern/CommonLibF4/CommonLibF4/src/RE/NetImmerse/NiPoint.cpp` | NiPoint2, NiPoint4, operator*, operator-, Cross (+2) |
| `extern/CommonLibF4/CommonLibF4/include/RE/NetImmerse/NiTArray.h` | operator++, operator--, slot_filled, validate |
| `extern/CommonLibF4RD/CommonLibF4/include/RE/NetImmerse/NiTArray.h` | operator++, operator--, slot_filled, validate |
| `extern/CommonLibF4/CommonLibF4/include/RE/Bethesda/ImageSpaceModifier.h` | ImageSpaceModifierInstance, ImageSpaceModifierInstanceTemp |
| `extern/CommonLibF4/CommonLibF4/include/RE/NetImmerse/NiExtraData.h` | NiExtraData |
| `extern/CommonLibF4/CommonLibF4/include/RE/NetImmerse/NiObject.h` | NiObject |
| `extern/CommonLibF4/CommonLibF4/include/RE/NetImmerse/NiTimeController.h` | NiTimeController |
| `extern/CommonLibF4/CommonLibF4/include/RE/Bethesda/BSGeometry.h` | BSGeometry |

## Entry Points

Start here when exploring this area:

- **`operator<=>`** (Function) — `extern/CommonLibF4/CommonLibF4/include/RE/NetImmerse/NiSmartPointer.h:172`
- **`operator==`** (Function) — `extern/CommonLibF4/CommonLibF4/include/RE/NetImmerse/NiSmartPointer.h:165`
- **`operator<=>`** (Function) — `extern/CommonLibF4RD/CommonLibF4/include/RE/NetImmerse/NiSmartPointer.h:169`
- **`operator==`** (Function) — `extern/CommonLibF4RD/CommonLibF4/include/RE/NetImmerse/NiSmartPointer.h:162`
- **`ImageSpaceModifierInstance`** (Class) — `extern/CommonLibF4/CommonLibF4/include/RE/Bethesda/ImageSpaceModifier.h:22`

## Key Symbols

| Symbol | Type | File | Line |
|--------|------|------|------|
| `ImageSpaceModifierInstance` | Class | `extern/CommonLibF4/CommonLibF4/include/RE/Bethesda/ImageSpaceModifier.h` | 22 |
| `ImageSpaceModifierInstanceTemp` | Class | `extern/CommonLibF4/CommonLibF4/include/RE/Bethesda/ImageSpaceModifier.h` | 97 |
| `NiExtraData` | Class | `extern/CommonLibF4/CommonLibF4/include/RE/NetImmerse/NiExtraData.h` | 7 |
| `NiObject` | Class | `extern/CommonLibF4/CommonLibF4/include/RE/NetImmerse/NiObject.h` | 35 |
| `NiTimeController` | Class | `extern/CommonLibF4/CommonLibF4/include/RE/NetImmerse/NiTimeController.h` | 11 |
| `NiPointer` | Class | `extern/CommonLibF4/CommonLibF4/include/RE/NetImmerse/NiSmartPointer.h` | 7 |
| `NiPointer` | Class | `extern/CommonLibF4RD/CommonLibF4/include/RE/NetImmerse/NiSmartPointer.h` | 7 |
| `BSGeometry` | Class | `extern/CommonLibF4/CommonLibF4/include/RE/Bethesda/BSGeometry.h` | 23 |
| `NiAVObject` | Class | `extern/CommonLibF4/CommonLibF4/include/RE/NetImmerse/NiAVObject.h` | 17 |
| `NiNode` | Class | `extern/CommonLibF4/CommonLibF4/include/RE/NetImmerse/NiNode.h` | 9 |
| `NiObjectNET` | Class | `extern/CommonLibF4/CommonLibF4/include/RE/NetImmerse/NiObjectNET.h` | 18 |
| `NiProperty` | Class | `extern/CommonLibF4/CommonLibF4/include/RE/NetImmerse/NiProperty.h` | 8 |
| `NiShadeProperty` | Class | `extern/CommonLibF4/CommonLibF4/include/RE/NetImmerse/NiShadeProperty.h` | 6 |
| `NiBinaryStream` | Class | `extern/CommonLibF4/CommonLibF4/include/RE/NetImmerse/NiBinaryStream.h` | 4 |
| `NiFile` | Class | `extern/CommonLibF4/CommonLibF4/include/RE/NetImmerse/NiFile.h` | 7 |
| `NiTListBase` | Class | `extern/CommonLibF4/CommonLibF4/include/RE/NetImmerse/NiTListBase.h` | 14 |
| `NiTPointerListBase` | Class | `extern/CommonLibF4/CommonLibF4/include/RE/NetImmerse/NiTPointerListBase.h` | 7 |
| `operator<=>` | Function | `extern/CommonLibF4/CommonLibF4/include/RE/NetImmerse/NiSmartPointer.h` | 172 |
| `operator==` | Function | `extern/CommonLibF4/CommonLibF4/include/RE/NetImmerse/NiSmartPointer.h` | 165 |
| `operator<=>` | Function | `extern/CommonLibF4RD/CommonLibF4/include/RE/NetImmerse/NiSmartPointer.h` | 169 |

## How to Explore

1. `context({name: "operator<=>"})` — see callers and callees
2. `query({search_query: "netimmerse"})` — find related execution flows
3. Read key files listed above for implementation details
4. `explain({target: "<file or symbol>"})` — persisted taint findings (source→sink data flows), when indexed with `--pdg`

# Architecture

EspScreen is a four-layer system (App → OS → HAL → Hardware) with a parallel Slave-SPI mode that bypasses the upper layers entirely. The HAL is written in C, the OS and App layers run MicroPython + lv_micropython from Phase 1 onward, and Phase 0 uses the Arduino framework for fastest bring-up. For the full layered diagram, phased milestones, ADRs, config schema, and open risks, see [/PLAN.md](../PLAN.md#1-architecture-overview).

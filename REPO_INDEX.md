# Repository Index

## GitHub Repositories
1. Captain Fantastic Home Edition
   - https://github.com/Phil-CIA/Captain-Fantastic-home-edition
2. Development Station Power Supply
   - https://github.com/Phil-CIA/Development-Station-Power-Supply

## Captain Repository Layout
1. Legacy firmware
   - src/
   - include/
   - data/
   - docs/
2. Captain-v2 split-MPU work
   - Captain-v2/
3. Hardware assets
   - hardware/kicad/esp32vroom/
   - hardware/kicad/libs/
   - hardware/gerbers/root-export-esp32vroom/
   - hardware/gerbers/legacy-gerber-files/
   - hardware/backups/esp32vroom-backups/
4. Archive material
   - archive/backups/
   - archive/battery-charger/
   - archive/rtos-main-firmware-restore/

## Current Cleanup Status
1. Project map PR merged
2. Archive normalization PR merged
3. Hardware normalization PR merged
4. Final indexing/handoff branch in progress

## Local-Only Files That Should Stay Out Of GitHub
1. .pio/ build output
2. VS Code machine-specific files like c_cpp_properties.json and launch.json
3. LTspice generated outputs in Captain-v2/hw_sim/ (`*.raw`, `*.db`, `*.log`)

# MicronetViz

Desktop diagnosticka appka pre `micronet` s natyvnym bridge napojenim.

## Co uz vie

- zobrazit snapshot uzlov a ich online stav z C bridge
- zobrazit per-node counters (`packets_sent`, `packets_recv`, `health`, `free_heap`)
- ukazat event buffer (online/offline/custom/publish)
- vizualizovat lokalne `microdb` kluce z jadra
- aktivne ovladanie cez bridge (`publish`, `update`, `send_custom`, `broadcast_custom`)
- group operacie (`group_create`, `group_join`, `group_leave`)
- manualny target node id vstup (32 bajtov hex) popri vybere z combo
- live validacia vstupov + deaktivacia akcii pri nevalidnych hodnotach
- pairing info pre Arduino (`WPF Node ID` + fixny UDP port `33477` v ovladacom paneli)
- quick Arduino akcie v UI (`Ping`, `LED Toggle`, `Relay Toggle`) cez custom msg type `32`
- register panel zariadeni (alias + node id), ulozenie zoznamu a rychle `Use as Target`
- fallback na simulaciu, ak `micronet_bridge.dll` nie je dostupne

## Spustenie (bridge mode)

```powershell
cmake -S . -B build
cmake --build build --config Debug
dotnet run --project tools\MicronetViz
```

WPF app sa pokusi nacitat `micronet_bridge.dll` z:
- `tools\MicronetViz\bin\Debug\net9.0-windows\`
- `build\Debug\`
- `build\Release\`

## Arduino Pairing

- app bridge pocuva na UDP porte `33477`
- v control paneli je vypisany plny `WPF Node ID` (hex, 32 bajtov)
- tieto hodnoty skopiruj do `arduino\micronet\examples\wpf_bridge_mesh\wpf_bridge_mesh.ino`
- onboarding flow pre zariadenie:
  - na ESP32 spusti `whoami` (alebo `regen_nodeid` + reboot + `whoami`)
  - tento Node ID pouzi v appke ako registrovany target
  - pridaj zariadenie do rovnakej skupiny (`group_create` / `group_join`)

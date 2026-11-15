# CrossOver as `wine` for tools/linux_toolchain.py (macOS)

`linux_toolchain.py` runs Microsoft's compiler under Wine with `wine`, `wineboot -i` and a
`WINEPREFIX`. On macOS it puts this folder first on `PATH`, so those names reach CrossOver:
`wine` runs CrossOver's wine with the prefix as a private bottle at that absolute path, and
`wineboot` creates that bottle (`cxbottle --create --template win10_64`) when it is missing.
The CrossOver path is `/Applications/CrossOver Preview.app`; `EDVR_CROSSOVER` overrides it.

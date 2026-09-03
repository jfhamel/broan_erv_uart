# Broan ERV serial component

## Additional features in this fork (not yet upstreamed)

This branch is based on the original work of [spitko](https://github.com/nspitko/broan_erv_uart), and exists so I can test additional features against my own ERV.

My long-term goal is not to maintain this branch, but to help the community develop and maintain Broan ERV control features for Home Assistant.

I've tried to mimic the features offered by the official Broan wall controllers, as described in the [wall control spec sheet](https://broan-nutone.com/getmedia/a91b1616-4d06-449c-abfa-0c14add1aa99/Spec_Sheet_WallControls_En_Fr.pdf?ext=.pdf).

Features added in this branch:
* Filter life reset fix: reproduces the exact three-step write sequence used by the wall controller when resetting filter life, plus a configurable reset duration (1-24 months)
* Turbo control: dedicated switch, configurable duration, and a countdown sensor showing time remaining
* Boost countdown sensor: time remaining on the bathroom dry-contact override (OVR). 
* Base fan mode / ventilation state sensors: expose what the ERV is actually doing underneath Turbo, Absence, Dehumidistat, or Smart mode
* Automatic mode support (available on VAUTOW wall control)
* Absence (Away) mode support
* Finer-grained fan mode control: distinct Exchange (min/med/max/adjustable) and Recirculation (min/med/max) options instead of a single generic mode
* Switch at runtime between the ESP32 controlling the ERV, and listening only, leaving control to the wall controller. To do this, the wall controller must be connected to a GPIO/relay on the ESP32. Refer to the example YAML below for the configuration details.

See [full_ha_interface_example.yaml](./examples/full_ha_interface_example.yaml) for a complete example configuration using all of these features.

The full protocol I reverse-engineered is documented in [broan-erv-protocole.md](./broan-erv-protocole.md).

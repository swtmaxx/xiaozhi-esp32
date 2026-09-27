# Vendored copy of 78/esp-ml307

This directory is a source copy of the upstream `78/esp-ml307` component, kept in
tree instead of being pulled as a managed dependency.

- Upstream: https://github.com/78/esp-ml307
- Version: 3.6.6
- Commit: 9f2a278ac6bf4ca3d6bc5057e8fcf10097cc583f
- License: Apache-2.0 (see LICENSE)

Reason for vendoring: `main/idf_component.yml` previously requested
`78/esp-ml307: ~3.6.6`, which the ESP32-S2 build could not resolve. Carrying the
sources locally keeps the ESP32-S2 (AlphaPi One S) target buildable while the
component also stays available to the other network implementations.

When updating, replace this directory with the upstream tag and refresh the
version and commit fields above, then rebuild the AlphaPi One S target.
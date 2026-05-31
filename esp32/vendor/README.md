# Vendored ESP32 libraries

## TFT_eSPI

Copied from [Xinyuan-LilyGO/T-Deck](https://github.com/Xinyuan-LilyGO/T-Deck) `lib/TFT_eSPI`.

Upstream `bodmer/TFT_eSPI` does not ship the T-Deck ST7789 init sequence
(LilyGO added it 2024-07-26; see their README). Using the registry package
with `-DINIT_SEQUENCE_2` leaves a blank panel.

The `[env:t-deck]` build loads this copy via `lib_extra_dirs = vendor` instead
of `lib_deps`.

# Changelog

## 0.1.1

**Diagnostics were shipping enabled. They are off now.**

`debug.json` ships with `active: "debug"` because that is the right default on a
development machine, and the release packaging copied the config folder verbatim — so
the 0.1.0 archive carried it. That profile turns on **Papyrus tracing**, which slows the
script engine, and writes to your `Fallout4Custom.ini`.

If you installed 0.1.0, updating fixes it. Nothing you need to do by hand; the config is
replaced with `active: "off"`.

Packaging now forces the shipped copy to `off` and **reads it back**, refusing to build a
release that would ship diagnostics on — and checks the file has no byte-order mark,
because the first version of that fix added one and a BOM is not JSON to the plugin's
parser.

No gameplay changes.

## 0.1.0

First release.

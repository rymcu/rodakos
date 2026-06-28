# RodakOS Architecture

RodakOS is split into two layers.

Phone OS owns app metadata, lifecycle, navigation, system services, and resource
policy. Apps are described by `PhoneAppDescriptor` and instantiated by
`PhoneAppHost`.

Phone UI Framework owns LVGL-facing screen construction, theme, layout helpers,
cards, buttons, and common app chrome.

```mermaid
flowchart TD
  "app_main" --> "BigSmartBoard"
  "app_main" --> "PhoneSystem"
  "PhoneSystem" --> "BuiltInApps"
  "PhoneSystem" --> "PhoneAppRegistry"
  "PhoneSystem" --> "PhoneAppHost"
  "PhoneSystem" --> "PhoneNavigation"
  "PhoneSystem" --> "PhoneAppContext"
  "PhoneAppContext" --> "PhoneUi"
  "PhoneAppContext" --> "Settings"
  "BuiltInApps" --> "HomeApp"
  "BuiltInApps" --> "SettingsApp"
  "PhoneAppHost" --> "HomeApp"
  "PhoneAppHost" --> "SettingsApp"
  "PhoneUi" --> "LVGL"
```

The first milestone keeps all apps statically linked. The plug-in property is
architectural: Home and launch routing are driven by descriptors instead of
hard-coded launcher and switch statements.

## Why this split exists

The current xiaozhi shell mixes app policy into `Application` and `LcdDisplay`:
launch aliases, visibility booleans, per-app `unique_ptr` members, launcher item
construction, and return-to-launcher behavior all live near the central
application object. Adding an app therefore means editing several switches and
state checks instead of registering one app descriptor.

RodakOS moves that responsibility into `PhoneAppDescriptor`,
`PhoneAppRegistry`, `PhoneAppHost`, and `PhoneNavigation`. The Home desktop is a
view of the registry, not a hand-maintained app list.

## App pluggability model

On ESP32-S3, the first target is not runtime-loaded binary plug-ins. Apps are
compiled into the firmware image. "Pluggable" means the app is modular at the
firmware architecture level:

- app metadata is declared in one descriptor;
- app creation goes through a factory callback;
- Home discovers apps from `PhoneAppRegistry`;
- launch routing uses app IDs and aliases instead of a central enum/switch;
- OS core calls `RegisterRodakBuiltInApps()` and does not include concrete app
  headers.

Future work can add component-level app packages or manifest generation, but
the first milestone deliberately stays static-link friendly for ESP-IDF.

## Related docs

- [Firmware download](firmware-download.md)
- [Project roadmap](roadmap.md)

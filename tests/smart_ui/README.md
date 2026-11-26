# Smart UI regression

This target compiles the production `SmartApp`, `PhoneUi`, theme and components against
LVGL 9.3's real 320×240 in-memory display with the production fonts (Montserrat → Ionicons
→ PuHui). `LightService` is a header fake with the same public surface the app uses; it
records every patch and can fail writes, so the tests check exactly what the app sends.
It does not test the LED strip driver; `tests/light_service` covers that.

The 11 cases cover the single-light control page and its Chinese board-light name, the
hero row sending one `enabled` patch, colour selection while off (colour only, light stays
off, toast) and while on, brightness preview during a drag with one commit on release,
the commit going to the light the drag started on, refresh of changes made outside the app
and the voice status light overlay, a rejected write keeping the accepted state, the
multi-light overview with quick toggles and back navigation, the empty state, and a theme
rebuild. The suite writes `smart-*.ppm` screenshots for visual inspection.

```sh
cmake -S tests/smart_ui -B "$HOME/.cache/rodakos-smart-ui" -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build "$HOME/.cache/rodakos-smart-ui" -j 6
ctest --test-dir "$HOME/.cache/rodakos-smart-ui" --output-on-failure
```

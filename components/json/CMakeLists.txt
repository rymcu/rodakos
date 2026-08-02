# ESP-SR 2.2.x still names ESP-IDF 5's built-in `json` component.
# Re-export the IDF 6 managed cJSON component under that legacy name.
idf_component_register(REQUIRES espressif__cjson)

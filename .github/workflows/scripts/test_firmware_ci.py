"""Offline regressions for firmware CI input validation."""

import importlib.util
from pathlib import Path
import unittest

import yaml


SCRIPT = Path(__file__).with_name("firmware_ci.py")
SPEC = importlib.util.spec_from_file_location("firmware_ci", SCRIPT)
firmware_ci = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(firmware_ci)


class RegistrySourceTests(unittest.TestCase):
    def test_both_reviewed_registry_spellings_are_accepted(self):
        for url in ("https://components.espressif.com", "https://components.espressif.com/"):
            with self.subTest(url=url):
                firmware_ci.validate_registry_source("fixture/component", {"registry_url": url})

    def test_every_service_entry_in_actual_lock_is_accepted_without_rewriting(self):
        path = firmware_ci.ROOT / "dependencies.lock"
        original = path.read_bytes()
        lock = yaml.safe_load(original)
        count = 0
        for name, entry in lock["dependencies"].items():
            if entry["source"]["type"] == "service":
                with self.subTest(component=name):
                    firmware_ci.validate_registry_source(name, entry["source"])
                count += 1
        self.assertGreater(count, 0)
        self.assertEqual(path.read_bytes(), original)

    def test_unreviewed_or_malformed_registry_values_are_rejected(self):
        values = (
            "http://components.espressif.com/",
            "https://components.espressif.com.evil.example/",
            "https://mirror.example/",
            "https://components.espressif.com:443/",
            "https://user@components.espressif.com/",
            "https://components.espressif.com@evil.example/",
            "https://components.espressif.com/api/",
            "https://components.espressif.com//",
            "https://components.espressif.com/../",
            "https://components.espressif.com/?query=1",
            "https://components.espressif.com/#fragment",
            "https://components.espressif.com\\",
            "https://COMPONENTS.ESPRESSIF.COM/",
            " https://components.espressif.com/",
            "https://components.espressif.com/\n",
            "", None, [], {}
        )
        for value in values:
            with self.subTest(value=value):
                with self.assertRaisesRegex(RuntimeError, "Unreviewed component Registry"):
                    firmware_ci.validate_registry_source("fixture/component", {"registry_url": value})
        for source in ({}, None):
            with self.subTest(source=source):
                with self.assertRaisesRegex(RuntimeError, "Unreviewed component Registry"):
                    firmware_ci.validate_registry_source("fixture/component", source)


if __name__ == "__main__":
    unittest.main()

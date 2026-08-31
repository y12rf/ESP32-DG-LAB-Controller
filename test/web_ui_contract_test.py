from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[1]
ASSETS_H = ROOT / "src" / "WebAssets.h"
ASSETS_CPP = ROOT / "src" / "WebAssets.cpp"
WEB_UI_H = ROOT / "src" / "WebUi.h"
WEB_UI_CPP = ROOT / "src" / "WebUi.cpp"
MAIN_CPP = ROOT / "src" / "main.cpp"


def read(path: Path) -> str:
    return path.read_text(encoding="utf-8")


class WebUiAssetContractTest(unittest.TestCase):
    def test_flash_asset_is_self_contained_mobile_shell(self):
        self.assertTrue(ASSETS_H.exists())
        self.assertTrue(ASSETS_CPP.exists())
        source = read(ASSETS_CPP)
        self.assertIn("const char kIndexHtml[] PROGMEM", source)
        self.assertIn('name="viewport"', source)
        self.assertIn('data-tab="status"', source)
        self.assertIn('data-tab="control"', source)
        self.assertIn('data-tab="logs"', source)
        self.assertNotIn("http://", source)
        self.assertNotIn("https://", source)
        self.assertNotIn('meta http-equiv="refresh"', source.lower())
        self.assertNotIn("innerHTML", source)


class WebUiReadApiContractTest(unittest.TestCase):
    def test_root_and_read_routes_have_fixed_methods(self):
        source = read(WEB_UI_CPP)
        for route in ("/", "/api/status", "/api/devices", "/api/logs"):
            self.assertIn(f'server_.on("{route}", HTTP_GET', source)
        self.assertIn("send_P", source)
        self.assertIn("web_assets::kIndexHtml", source)

    def test_dynamic_full_page_rendering_is_removed(self):
        header = read(WEB_UI_H)
        source = read(WEB_UI_CPP)
        self.assertNotIn("makeHtml", header + source)
        self.assertNotIn("redirectHome", header + source)
        self.assertNotIn("meta http-equiv", source.lower())

    def test_status_contract_fields_are_present(self):
        source = read(WEB_UI_CPP)
        for field in (
            "connected", "ready", "type", "name", "strengthA",
            "strengthB", "confirmed", "waiting", "wave", "sending",
            "autoConnect", "scanRevision",
        ):
            self.assertIn(f'\\"{field}\\"', source)

    def test_json_strings_are_escaped(self):
        header = read(WEB_UI_H)
        source = read(WEB_UI_CPP)
        self.assertIn("appendJsonString", header + source)
        self.assertIn("case '\\\\'", source)
        self.assertIn("case '\"'", source)


if __name__ == "__main__":
    unittest.main()

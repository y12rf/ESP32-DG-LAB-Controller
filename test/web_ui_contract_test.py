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


if __name__ == "__main__":
    unittest.main()

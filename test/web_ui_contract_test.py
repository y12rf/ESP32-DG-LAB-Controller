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


class WebUiActionApiContractTest(unittest.TestCase):
    def test_actions_are_post_only(self):
        source = read(WEB_UI_CPP)
        for route in (
            "/api/scan", "/api/connect", "/api/disconnect",
            "/api/auto-connect", "/api/output", "/api/wave",
            "/api/strength",
        ):
            self.assertIn(f'server_.on("{route}", HTTP_POST', source)

    def test_legacy_state_changing_get_routes_are_removed(self):
        source = read(WEB_UI_CPP)
        for route in (
            "/scan", "/connect", "/disconnect", "/auto-connect",
            "/start", "/stop", "/wave", "/strength",
        ):
            self.assertNotIn(f'server_.on("{route}"', source)

    def test_strength_response_preserves_queue_disposition(self):
        source = read(WEB_UI_CPP)
        self.assertIn("prepared", source)
        self.assertIn("queued", source)
        self.assertIn("RequestDisposition::Rejected", source)


class WebUiBrowserContractTest(unittest.TestCase):
    def test_browser_uses_all_api_paths(self):
        source = read(ASSETS_CPP)
        for route in (
            "/api/status", "/api/devices", "/api/logs", "/api/scan",
            "/api/connect", "/api/disconnect", "/api/auto-connect",
            "/api/output", "/api/wave", "/api/strength",
        ):
            self.assertIn(route, source)

    def test_polling_and_visibility_contract(self):
        source = read(ASSETS_CPP)
        self.assertIn("STATUS_INTERVAL_MS=1000", source)
        self.assertIn("LOG_INTERVAL_MS=2000", source)
        self.assertIn("statusInFlight", source)
        self.assertIn("visibilitychange", source)
        self.assertIn("document.hidden", source)

    def test_safe_dom_updates_and_form_posts(self):
        source = read(ASSETS_CPP)
        self.assertIn("textContent", source)
        self.assertIn("URLSearchParams", source)
        self.assertIn("method:'POST'", source)
        self.assertNotIn("innerHTML", source)

    def test_discrete_strength_controls_are_present(self):
        source = read(ASSETS_CPP)
        for value in ("-10", "-5", "-1", "+1", "+5", "+10", "归零", "50%"):
            self.assertIn(value, source)
        self.assertNotIn('type="range"', source)


class WebUiIntegrationContractTest(unittest.TestCase):
    def test_due_output_runs_before_http_client(self):
        source = read(MAIN_CPP)
        output_at = source.index("outputController.handleWaveSend();")
        web_at = source.index("webUi.handleClient();")
        self.assertLess(output_at, web_at)

    def test_ci_runs_web_contract_before_platformio(self):
        workflow = read(ROOT / ".github" / "workflows" / "platformio.yml")
        contract_at = workflow.index("python test/web_ui_contract_test.py")
        native_at = workflow.index("pio test -e native")
        firmware_at = workflow.index("pio run -e esp32dev")
        self.assertLess(contract_at, native_at)
        self.assertLess(contract_at, firmware_at)


if __name__ == "__main__":
    unittest.main()

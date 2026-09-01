from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[1]
SERIAL_H = ROOT / "src" / "SerialCli.h"
SERIAL_CPP = ROOT / "src" / "SerialCli.cpp"
APP_LOG_H = ROOT / "src" / "AppLog.h"
APP_LOG_CPP = ROOT / "src" / "AppLog.cpp"
MAIN_CPP = ROOT / "src" / "main.cpp"
PLATFORMIO = ROOT / "platformio.ini"


def read(path: Path) -> str:
    return path.read_text(encoding="utf-8")


class SerialCliStructureTest(unittest.TestCase):
    def test_fixed_input_and_non_blocking_watch(self):
        header = read(SERIAL_H)
        source = read(SERIAL_CPP)
        self.assertIn("kInputCapacity = 96", header)
        self.assertIn("char input_[kInputCapacity]", header)
        self.assertIn("kWatchIntervalMs = 500", header)
        self.assertIn("kBarWidth = 20", header)
        self.assertNotIn("delay(", source)
        self.assertNotIn("xTaskCreate", source)
        self.assertNotIn("ArduinoJson", header + source)

    def test_all_commands_are_dispatched(self):
        source = read(SERIAL_CPP)
        for command in (
            "Help", "Status", "Watch", "Scan", "Devices", "Connect",
            "Disconnect", "AutoConnect", "Output", "Wave", "Strength",
            "Logs",
        ):
            self.assertIn(f"CliCommandType::{command}", source)

    def test_actions_use_existing_controllers(self):
        source = read(SERIAL_CPP)
        for call in (
            "ble_.startBleScan()", "ble_.connectToDevice(",
            "ble_.disconnectDevice()", "output_.onManualConnectionAttempt()",
            "output_.onConnected(true)", "output_.onConnected(false)",
            "output_.startSending()", "output_.stopSending()",
            "output_.selectWave(", "output_.adjustStrength(",
        ):
            self.assertIn(call, source)

    def test_watch_controls_log_mirroring_and_ansi(self):
        source = read(SERIAL_CPP)
        self.assertIn("setSerialMirrorEnabled(false)", source)
        self.assertIn("setSerialMirrorEnabled(true)", source)
        self.assertIn("\\033[2J", source)
        self.assertIn("\\033[H", source)
        self.assertIn('F("█")', source)
        self.assertIn('F("░")', source)


class SerialCliWiringTest(unittest.TestCase):
    def test_log_mirror_is_conditional(self):
        header = read(APP_LOG_H)
        source = read(APP_LOG_CPP)
        self.assertIn("setSerialMirrorEnabled", header)
        self.assertIn("serialMirrorEnabled_", header + source)
        self.assertIn("if (serialMirrorEnabled_)", source)

    def test_cli_runs_after_due_output_and_before_http(self):
        source = read(MAIN_CPP)
        output_at = source.index("outputController.handleWaveSend();")
        cli_at = source.index("serialCli.handleInput();")
        web_at = source.index("webUi.handleClient();")
        self.assertLess(output_at, cli_at)
        self.assertLess(cli_at, web_at)

    def test_baud_rate_is_consistent(self):
        self.assertIn("Serial.begin(115200)", read(MAIN_CPP))
        self.assertIn("monitor_speed = 115200", read(PLATFORMIO))


if __name__ == "__main__":
    unittest.main()

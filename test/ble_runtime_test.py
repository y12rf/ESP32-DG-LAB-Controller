"""Fault-inject the production subscription methods without an ESP32."""
from pathlib import Path
import importlib.util
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


class BleSubscriptionTest(unittest.TestCase):
    def test_failed_client_destruction_waits_for_full_dispatch(self):
        spec = importlib.util.spec_from_file_location(
            "overlay", ROOT / "scripts/ble_dispatch_overlay.py")
        overlay = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(overlay)
        source = (ROOT / "test/fixtures/ble_dispatch.cpp").read_text(encoding="utf-8")
        source = overlay.guarded_source(source)
        with tempfile.TemporaryDirectory(prefix="dglab-dispatch-") as folder:
            tmp = Path(folder)
            (tmp / "freertos").mkdir()
            (tmp / "freertos/FreeRTOS.h").write_text('''
#pragma once
#include <atomic>
#include <cassert>
#include <mutex>
#define configASSERT(x) assert(x)
constexpr int portMAX_DELAY=-1;
using SemaphoreHandle_t=std::mutex*;
extern std::atomic<bool> deletionBlocked;
inline SemaphoreHandle_t xSemaphoreCreateMutex(){return new std::mutex;}
inline void xSemaphoreTake(SemaphoreHandle_t mutex,int){
  if(!mutex->try_lock()){deletionBlocked=true;mutex->lock();}
}
inline void xSemaphoreGive(SemaphoreHandle_t mutex){mutex->unlock();}
''', encoding="utf-8")
            (tmp / "freertos/semphr.h").write_text('#include "FreeRTOS.h"\n', encoding="utf-8")
            (tmp / "test.cpp").write_text(source, encoding="utf-8")
            subprocess.run(["g++", "-std=c++17", "-pthread", "-I" + str(tmp),
                            "-I" + str(ROOT / "lib/BleDispatch/src"),
                            str(tmp / "test.cpp"),
                            str(ROOT / "lib/BleDispatch/src/BleDispatch.cpp"),
                            "-o", str(tmp / "test.exe")], check=True, timeout=120)
            subprocess.run([str(tmp / "test.exe")], check=True, timeout=10)

    def test_v2_initial_read_is_required(self):
        source = (ROOT / "src/BleManager.cpp").read_text(encoding="utf-8")
        start = source.index("  if (type == DeviceType::DG2) {")
        end = source.index(" else if (type == DeviceType::DG3)", start)
        branch = source[start:end]
        harness = (ROOT / "test/fixtures/v2_initial_read.cpp").read_text(encoding="utf-8")
        harness = harness.replace("// PRODUCTION_BRANCH", branch)
        with tempfile.TemporaryDirectory(prefix="dglab-v2-read-") as folder:
            tmp = Path(folder)
            (tmp / "test.cpp").write_text(harness, encoding="utf-8")
            subprocess.run(["g++", "-std=c++17", "-I" + str(ROOT / "lib/DgLabControl/src"),
                            str(tmp / "test.cpp"),
                            str(ROOT / "lib/DgLabControl/src/DgLabControl.cpp"),
                            "-o", str(tmp / "test.exe")], check=True, timeout=120)
            subprocess.run([str(tmp / "test.exe")], check=True, timeout=10)

    def test_subscription_failure_paths(self):
        source = (ROOT / "src/BleManager.cpp").read_text(encoding="utf-8")
        methods = []
        for name in ("void BleManager::afterGattEvent(",
                     "bool BleManager::waitSubscription(",
                     "bool BleManager::subscribe("):
            start = source.index(name)
            methods.append(source[start:source.index("\n}", start) + 2])
        harness = (ROOT / "test/fixtures/ble_subscription.cpp").read_text(encoding="utf-8")
        harness = harness.replace("// PRODUCTION_METHODS", "\n".join(methods))
        compiler = shutil.which("g++")
        self.assertIsNotNone(compiler, "g++ is required on PATH")
        with tempfile.TemporaryDirectory(prefix="dglab-subscription-") as folder:
            cpp, executable = Path(folder) / "test.cpp", Path(folder) / "test.exe"
            cpp.write_text(harness, encoding="utf-8")
            subprocess.run([compiler, "-std=c++17", str(cpp), "-o", str(executable)],
                           check=True, timeout=120)
            subprocess.run([str(executable)], check=True, timeout=10)


if __name__ == "__main__":
    unittest.main()

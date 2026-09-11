"""Compile a guarded copy of the pinned Arduino BLE dispatcher, never edit it."""
from pathlib import Path


def guarded_source(source):
    signature = "esp_ble_gattc_cb_param_t* param) {"
    start = source.index("/* STATIC */ void BLEDevice::gattClientEventHandler(")
    end = source.index(signature, start) + len(signature)
    return ('#include "BleDispatch.h"\n' + source[:end]
            + "\n\tBleDispatchGuard dispatchGuard;\n" + source[end:])


def install(env):
    project = Path(env.subst("$PROJECT_DIR"))
    framework = Path(env.PioPlatform().get_package_dir("framework-arduinoespressif32"))
    original = framework / "libraries/BLE/src/BLEDevice.cpp"
    output = Path(env.subst("$BUILD_DIR")) / "ble-overlay/BLEDevice.cpp"
    output.parent.mkdir(parents=True, exist_ok=True)
    content = guarded_source(original.read_text(encoding="utf-8"))
    if not output.exists() or output.read_text(encoding="utf-8") != content:
        output.write_text(content, encoding="utf-8")
    env.Append(CPPPATH=[str(original.parent), str(project / "lib/BleDispatch/src")])

    def replace(env, node):
        if Path(node.srcnode().get_abspath()) == original:
            return env.File(str(output))
        return node

    env.AddBuildMiddleware(replace, "*BLEDevice.cpp")


if "Import" in globals():
    Import("env")
    install(env)

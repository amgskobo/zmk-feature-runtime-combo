"""Independent protobuf wire oracle for the controlled outbound list records."""

import importlib.util
import re
import subprocess


def load_protocol(work, root):
    destination = work / "python-protocol"
    destination.mkdir()
    subprocess.run(
        [
            "protoc",
            "-I" + str(root / "proto"),
            "--python_out=" + str(destination),
            str(root / "proto/cormoran/runtime_combo/runtime_combo.proto"),
        ],
        check=True,
        timeout=60,
    )
    path = destination / "cormoran/runtime_combo/runtime_combo_pb2.py"
    spec = importlib.util.spec_from_file_location("combo_wire_oracle", path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def expected_response(protocol, count, mode):
    response = protocol.Response()
    result = response.list_combos
    result.SetInParent()
    for index in range(count):
        if mode == "empty" or (mode == "holes" and index in (1, 3)):
            continue
        combo = result.combos.add()
        combo.index = index
        if mode != "holes" or index != 2:
            combo.name = "".join(chr(ord("A") + (index + i) % 26) for i in range(63))
        combo.key_positions.extend((index + 1) * 65536 + i for i in range(16))
        combo.behavior.SetInParent()
        if index % 3:
            combo.behavior.behavior_id = 1
            combo.behavior.param1 = 0xFFFFFFFF - index
            combo.behavior.param2 = 0x80000000 + index
        combo.layer_mask = 0xFFFFFFFF - index
        combo.enabled = index % 2 == 0
        combo.timeout_ms = 65535 if index % 2 else 0
        combo.require_prior_idle_ms = 0 if index % 2 else 65535
        combo.slow_release_override = index % 3
        combo.source = index % 4
    return response.SerializeToString(deterministic=True)


def verify_vectors(output, protocol, count):
    rows = re.findall(r"^WIRE (\w+) ([0-9a-f]+)$", output, re.M)
    if [name for name, _ in rows] != ["maximum", "holes", "empty"]:
        raise AssertionError("Missing/duplicate ordered list wire vectors")
    for name, encoded in rows:
        actual = bytes.fromhex(encoded)
        expected = expected_response(protocol, count, name)
        if actual != expected:
            raise AssertionError(f"List wire mismatch: count={count} mode={name}")
    marker = f"runtime combo list wire: PASS max={count}"
    if output.count(marker) != 1:
        raise AssertionError("Missing/duplicate list completion")


def verify_response_storage(size, count, abi):
    # Fixed generated scalar/string fields; positions/layers/binding params
    # remain full width. Every configured slot stays resident before encoding.
    profiles = {
        (4, 1, 1): ("ARM EABI", 160),
        (4, 4, 4): ("native32", 168),
        (8, 4, 4): ("native64", 168),
    }
    if tuple(abi) not in profiles:
        raise AssertionError(f"Unmeasured Combo response ABI: {abi}")
    name, combo_size = profiles[tuple(abi)]
    if size != 8 + combo_size * count:
        raise AssertionError(
            f"Configured Combo Response RAM: ABI={name} count={count} bytes={size}"
        )

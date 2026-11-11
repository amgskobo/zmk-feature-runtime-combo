"""Actual linked ARM allocations of active combos and the frozen RPC response."""

import hashlib
import json
from pathlib import Path
import re
import subprocess
import sys


def verify_active_storage(size, positions, pressed):
    if size != (20 + 4 * positions) * pressed:
        raise AssertionError("Runtime combo active storage budget exceeded")


def verify_pending_storage(size, positions):
    if size != 16 * positions:
        raise AssertionError("Runtime combo pending storage budget exceeded")


def verify_cache_storage(size, positions, slots):
    if size != ((28 + 2 * positions + 3) & ~3) * slots:
        raise AssertionError("Runtime combo cache storage budget exceeded")


def input_allocation(text, symbol, object_name):
    regions = [
        (int(a, 16), int(a, 16) + int(b, 16))
        for a, b in re.findall(
            r"^\s*(?:RAM|SRAM)\s+(0x[0-9a-fA-F]+)\s+(0x[0-9a-fA-F]+)\s+\S+\s*$",
            text,
            re.M,
        )
    ]
    if not regions:
        raise AssertionError("Missing linked RAM region")
    rows, pending = [], False
    section = ".bss." + symbol
    for line in text.splitlines():
        single = re.match(
            r"^\s+(\.\S+)\s+(0x[0-9a-fA-F]+)\s+(0x[0-9a-fA-F]+)\s+(.+)$", line
        )
        wrapped = re.match(r"^\s+(0x[0-9a-fA-F]+)\s+(0x[0-9a-fA-F]+)\s+(.+)$", line)
        if single:
            name, address, size, owner = single.groups()
            selected = name == section
        elif pending and wrapped:
            address, size, owner = wrapped.groups()
            selected = True
        else:
            pending = line.strip() == section
            continue
        pending = False
        address, size = int(address, 16), int(size, 16)
        if (
            not selected
            or not address
            or not size
            or not owner.endswith("(" + object_name + ")")
        ):
            continue
        if not any(
            start <= address and address + size <= end for start, end in regions
        ):
            raise AssertionError("Allocation is outside linked RAM")
        rows.append({"address": address, "bytes": size})
    if len(rows) > 1:
        raise AssertionError("Duplicate owner allocation")
    return rows[0] if rows else None


def verify_artifact(artifact, owner):
    artifact, owner = Path(artifact), Path(owner).resolve()
    sys.path.insert(0, str(owner / "tests/rpc-lock"))
    from list_wire import verify_response_storage

    config = dict(
        re.findall(
            r"^(CONFIG_\w+)=(.+)$", (artifact / "zephyr/.config").read_text(), re.M
        )
    )
    text = (artifact / "zephyr/zmk.map").read_text()
    active = input_allocation(text, "active_combos", "runtime_combo.c.obj")
    pending = input_allocation(text, "pending_keys", "runtime_combo.c.obj")
    decoded = input_allocation(text, "runtime_combo_cache", "runtime_combo.c.obj")
    response_name = "zmk_rpc_custom_subsystem_response_buffer_cormoran__runtime_combo"
    response = input_allocation(text, response_name, "runtime_combo_handler.c.obj")
    enabled = config.get("CONFIG_ZMK_RUNTIME_COMBO") == "y"
    rpc = config.get("CONFIG_ZMK_RUNTIME_COMBO_STUDIO_RPC") == "y"
    if enabled:
        assert active is not None, "Missing linked runtime combo active state"
        verify_active_storage(
            active["bytes"],
            int(config["CONFIG_ZMK_RUNTIME_COMBO_MAX_POSITIONS_PER_COMBO"]),
            int(config["CONFIG_ZMK_COMBO_MAX_PRESSED_COMBOS"]),
        )
        assert pending is not None, "Missing linked runtime combo pending state"
        verify_pending_storage(
            pending["bytes"],
            int(config["CONFIG_ZMK_RUNTIME_COMBO_MAX_POSITIONS_PER_COMBO"]),
        )
        assert decoded is not None, "Missing linked runtime combo decoded cache"
        verify_cache_storage(
            decoded["bytes"],
            int(config["CONFIG_ZMK_RUNTIME_COMBO_MAX_POSITIONS_PER_COMBO"]),
            int(config["CONFIG_ZMK_RUNTIME_COMBO_MAX_COMBOS"]),
        )
    else:
        assert (
            active is None and pending is None and decoded is None and response is None
        ), "Disabled combo retains its state"
    if rpc:
        assert response is not None, "Missing linked combo response"
        verify_response_storage(
            response["bytes"],
            int(config["CONFIG_ZMK_RUNTIME_COMBO_MAX_COMBOS"]),
            (4, 1, 1),
        )
    else:
        assert response is None, "Disabled RPC retains its response"
    cache = (artifact / "CMakeCache.txt").read_text()
    nm = re.search(r"^CMAKE_NM:FILEPATH=(.+)$", cache, re.M)
    assert nm and Path(nm.group(1)).is_absolute(), "Missing selected ARM nm"
    result = subprocess.run(
        [nm.group(1), "-S", "--defined-only", str(artifact / "zephyr/zmk.elf")],
        capture_output=True,
        text=True,
        check=True,
    )
    (artifact / "combo-memory-symbols.txt").write_text(result.stdout)
    symbols = {
        (int(row[0], 16), int(row[1], 16), row[3])
        for line in result.stdout.splitlines()
        if len(row := line.split()) == 4
    }
    for name, row in [
        ("active_combos", active),
        ("pending_keys", pending),
        ("runtime_combo_cache", decoded),
        (response_name, response),
    ]:
        if row:
            assert (row["address"], row["bytes"], name) in symbols, (
                "ELF/map allocation mismatch"
            )
    commands = json.loads((artifact / "compile_commands.json").read_text())
    sources = {}
    for selected, path in [
        (enabled, "src/runtime_combo/runtime_combo.c"),
        (rpc, "src/studio/runtime_combo_handler.c"),
    ]:
        rows = [row for row in commands if Path(row["file"]).resolve() == owner / path]
        assert len(rows) == int(selected), (
            "Compiled combo source selection mismatch: " + path
        )
        if selected:
            sources[path] = hashlib.sha256((owner / path).read_bytes()).hexdigest()
    usage = {
        "active": active,
        "pending": pending,
        "cache": decoded,
        "response": response,
        "source_sha256": sources,
        "elf_sha256": hashlib.sha256(
            (artifact / "zephyr/zmk.elf").read_bytes()
        ).hexdigest(),
        "scope": "These linked ARM allocations only, not module total or runtime stack/heap peak",
    }
    (artifact / "combo-static-memory.json").write_text(
        json.dumps(usage, indent=2) + "\n"
    )
    return usage

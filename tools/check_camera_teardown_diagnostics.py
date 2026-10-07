"""Audit the camera teardown recorder in a final ESP32-S3 ELF, without modifying it."""
from __future__ import annotations

import argparse
import json
from pathlib import Path
import re
import struct
import subprocess
import sys

from check_screen_jpeg_allocator import Elf32, digest, require, run_tool


STORAGE = "rodak_camera_teardown_diagnostics"
RECORDER = "rodak_camera_teardown_record"
MAGIC = 0x43544447
STORAGE_SIZE = 536
DRAM = (0x3FC88000, 0x3FD00000)
IRAM = (0x40370000, 0x403E0000)
# No indirect control flow, hardware loops, calls, privileged interrupt control,
# atomics other than the one CAS, or unreviewed opcodes are admitted.
OPCODES = frozenset("""
entry l32r l32i l32i.n s32i s32i.n mov mov.n movi movi.n
add add.n addi addi.n addmi sub slli srli and or xor memw
wsr.scompare1 s32c1i j beq beqi beqz beqz.n bne bnei bnez bnez.n
bge bgeu bgeui bgez blt blti bltu bltui bltz retw retw.n ret ret.n nop nop.n
""".split())
RETURNS = {"ret", "ret.n", "retw", "retw.n"}


def within(address, size, bounds):
    return bounds[0] <= address and size > 0 and address + size <= bounds[1]


def section_bytes(elf, address, size, required_flags=2):
    candidates = [section for section in elf.sections
                  if section["type"] == 1 and section["flags"] & required_flags == required_flags
                  and within(address, size, (section["address"],
                                             section["address"] + section["size"]))]
    require(len(candidates) == 1, f"ambiguous/unmapped initialized bytes: {address:x}+{size}")
    section = candidates[0]
    offset = section["offset"] + address - section["address"]
    data = elf.data[offset:offset + size]
    require(len(data) == size, "truncated section contents")
    return data


def unique_symbol(elf, wanted, expected_type):
    definitions = []
    for section in elf.sections:
        if section["type"] != 2:
            continue
        strings_section = elf.sections[section["link"]]
        strings = elf.data[strings_section["offset"]:
                           strings_section["offset"] + strings_section["size"]]
        for offset in range(section["offset"], section["offset"] + section["size"], 16):
            name, value, size, info, other, index = struct.unpack_from("<IIIBBH", elf.data, offset)
            if Elf32.cstring(strings, name) == wanted and index != 0:
                definitions.append((value, size, info, other, index))
    require(len(definitions) == 1, f"missing/duplicate symbol: {wanted}")
    address, size, info, other, index = definitions[0]
    require(info == (0x10 | expected_type) and other == 0,
            f"expected a global default-visibility symbol: {wanted}")
    require(0 < index < len(elf.sections), f"invalid symbol section: {wanted}")
    return {"address": address, "size": size, "section": index, "type": expected_type}


def verify_load_segment(elf, address, size, required_flags):
    offset = struct.unpack_from("<I", elf.data, 28)[0]
    width, count = struct.unpack_from("<HH", elf.data, 42)
    require(width == 32 and count > 0, "final ELF needs load program headers")
    matches = []
    for index in range(count):
        kind, position, virtual, physical, file_size, memory_size, flags, alignment = \
            struct.unpack_from("<8I", elf.data, offset + index * width)
        if kind != 1 or not within(address, size, (virtual, virtual + memory_size)):
            continue
        require(flags & required_flags == required_flags, "wrong load segment permissions")
        require(address + size <= virtual + file_size, "symbol is not initialized in load image")
        file_offset = position + address - virtual
        require(file_offset + size <= len(elf.data), "truncated load segment")
        require(elf.data[file_offset:file_offset + size] == section_bytes(elf, address, size),
                "load segment/section bytes disagree")
        matches.append((virtual, physical, file_size, memory_size, flags, alignment))
    require(len(matches) == 1, f"missing/ambiguous load segment: {address:x}")


def verify_layout(elf):
    require(struct.unpack_from("<H", elf.data, 16)[0] == 2,
            "expected a final executable ELF, not a relocatable object")
    storage = unique_symbol(elf, STORAGE, 1)
    recorder = unique_symbol(elf, RECORDER, 2)
    require(storage["size"] == STORAGE_SIZE, "diagnostics object must be exactly 536 bytes")
    require(storage["address"] % 4 == 0 and within(storage["address"], STORAGE_SIZE, DRAM),
            "diagnostics object must be aligned and entirely in internal DRAM")
    section = elf.sections[storage["section"]]
    require(section["flags"] & 7 == 3 and section["type"] == 1,
            "diagnostics need writable allocated initialized data, not executable/PSRAM/BSS")
    initial = section_bytes(elf, storage["address"], STORAGE_SIZE, 3)
    require(struct.unpack_from("<6I", initial) == (MAGIC, 1, 32, 0, 0, 0),
            "diagnostics header ABI/initial state mismatch")
    require(initial[24:] == bytes(512), "diagnostics records must start unpublished and zero")
    verify_load_segment(elf, storage["address"], STORAGE_SIZE, 6)

    require(recorder["address"] % 4 == 0 and
            within(recorder["address"], recorder["size"], IRAM),
            "recorder must be aligned and entirely in internal IRAM")
    section = elf.sections[recorder["section"]]
    require(section["flags"] & 7 == 6 and section["type"] == 1,
            "recorder needs allocated executable readonly code")
    require(recorder["size"] <= 512, "unexpectedly large recorder; review generated code")
    section_bytes(elf, recorder["address"], recorder["size"], 6)
    verify_load_segment(elf, recorder["address"], recorder["size"], 5)
    return storage, recorder


def verify_recorder(elf, storage, recorder, disassemble):
    start, end = recorder["address"], recorder["address"] + recorder["size"]
    pending, visited, occupied = [start], {}, {}
    branches, literals = [], []
    # Each branch target is disassembled separately so skipped alignment padding
    # cannot hide an instruction or make objdump decode the wrong boundary.
    while pending:
        pc = pending.pop()
        if pc in visited:
            continue
        require(start <= pc < end and pc not in occupied, "branch overlaps instruction bytes")
        instructions = []
        for line in disassemble(pc, end).splitlines():
            match = re.match(r"^\s*([0-9a-f]+):\s+([0-9a-f]+)\s+([\w.]+)\s*(.*)$", line)
            if match:
                instructions.append((int(match[1], 16), match[2], match[3], match[4].strip()))
        require(instructions and instructions[0][0] == pc, f"cannot decode recorder at {pc:x}")
        terminated = False
        for address, raw, opcode, operands in instructions:
            require(address == pc, f"instruction gap at {pc:x}")
            if address in visited:
                terminated = True
                break
            width = len(raw) // 2
            require(len(raw) % 2 == 0 and width in (2, 3) and address + width <= end,
                    f"invalid instruction extent at {address:x}")
            require(opcode in OPCODES, f"forbidden/unknown recorder opcode: {opcode} at {address:x}")
            require(section_bytes(elf, address, width) == bytes.fromhex(raw)[::-1],
                    f"objdump bytes disagree with ELF at {address:x}")
            require(not any(byte in occupied for byte in range(address, address + width)),
                    "overlapping recorder instructions")
            visited[address] = {"address": address, "size": width, "opcode": opcode,
                                "operands": operands}
            for byte in range(address, address + width):
                occupied[byte] = address
            pc = address + width
            if opcode == "l32r":
                literal = re.fullmatch(
                    r"a\d+,\s*([0-9a-f]+)(?:\s+<[^>]+>)?"
                    r"(?:\s+\(([0-9a-f]+)(?:\s+<[^>]+>)?\))?", operands)
                require(literal, "unrecognized l32r operands")
                address_of_literal = int(literal[1], 16)
                require(address_of_literal % 4 == 0 and
                        (within(address_of_literal, 4, DRAM) or
                         within(address_of_literal, 4, IRAM)),
                        "recorder literal must be in internal RAM")
                value = struct.unpack("<I", section_bytes(elf, address_of_literal, 4))[0]
                if literal[2] is not None:
                    require(int(literal[2], 16) == value,
                            "objdump literal annotation disagrees with ELF value")
                require(storage["address"] <= value < storage["address"] + STORAGE_SIZE,
                        "recorder literal must reference the fixed diagnostic object")
                literals.append({"instruction": address, "address": address_of_literal,
                                 "value": value})
            if opcode in RETURNS:
                terminated = True
                break
            if opcode == "j" or opcode.startswith("b"):
                branch = re.search(r"(?:^|,\s*)([0-9a-f]+)\s*(?:<[^>]+>)?$", operands)
                require(branch, f"unknown branch operands at {address:x}")
                target = int(branch[1], 16)
                require(address < target < end, f"backward/self/outside branch at {address:x}")
                branches.append({"address": address, "target": target, "opcode": opcode})
                pending.append(target)
                if opcode != "j":
                    require(pc < end, "branch fallthrough outside recorder")
                    pending.append(pc)
                terminated = True
                break
        require(terminated, f"unterminated recorder path at {pc:x}")

    opcodes = [item["opcode"] for item in visited.values()]
    require(opcodes.count("s32c1i") == 1, "recorder must contain exactly one native 32-bit CAS")
    require(opcodes.count("wsr.scompare1") == 1, "recorder must set compare register exactly once")
    require(opcodes.count("entry") == 1 and visited[start]["opcode"] == "entry",
            "unexpected recorder entry convention")
    require(any(opcode in RETURNS for opcode in opcodes), "recorder has no return")
    require(literals, "missing fixed-storage references")
    # Unreachable bytes may only be the tiny zero/nop alignment gaps emitted by
    # the reviewed compiler. They are reported explicitly, never decoded as code.
    gaps = []
    gap_start = None
    for address in range(start, end + 1):
        if address < end and address not in occupied:
            if gap_start is None:
                gap_start = address
        elif gap_start is not None:
            raw = section_bytes(elf, gap_start, address - gap_start)
            require(len(raw) <= 3 and raw in (b"\0", b"\0\0", b"\0\0\0", b"\x3d\xf0",
                                             b"\x3d\xf0\0", b"\0\x3d\xf0",
                                             b"\xf0\x20\0"),
                    f"unreviewed unreachable bytes at {gap_start:x}")
            gaps.append({"address": gap_start, "bytes": raw.hex()})
            gap_start = None
    return {"instructions": sorted(visited.values(), key=lambda item: item["address"]),
            "instruction_count": len(visited), "native_cas_count": 1,
            "calls": [], "backward_branches": [], "branches": branches,
            "literals": literals, "unreachable_alignment": gaps}


def audit(args):
    require(args.target == "esp32s3" and args.idf_version == "6.0.2",
            "unreviewed target/ESP-IDF baseline")
    config = args.sdkconfig.read_text(encoding="utf-8")
    require('CONFIG_IDF_TARGET="esp32s3"' in config and "CONFIG_IDF_TARGET_ESP32S3=y" in config,
            "sdkconfig is not ESP32-S3")
    before = digest(args.elf)
    elf = Elf32(args.elf)
    storage, recorder = verify_layout(elf)
    def disassemble(start, end):
        return run_tool(args.tool_prefix, "objdump", "-d", f"--start-address={start}",
                        f"--stop-address={end}", args.elf)
    generated = verify_recorder(elf, storage, recorder, disassemble)
    require(digest(args.elf) == before, "ELF changed while being audited")
    return {"success": True, "camera_teardown_link_verified": True,
            "elf_sha256": before, "sdkconfig_sha256": digest(args.sdkconfig),
            "target": args.target, "idf_version": args.idf_version,
            "storage": storage, "recorder": recorder, "generated_code": generated,
            "scope": "diagnostic storage and recorder only; no driver/IRQ/hardware proof"}


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--elf", type=Path, required=True)
    parser.add_argument("--sdkconfig", type=Path, required=True)
    parser.add_argument("--tool-prefix", required=True)
    parser.add_argument("--target", required=True)
    parser.add_argument("--idf-version", required=True)
    parser.add_argument("--output", type=Path)
    args = parser.parse_args(argv)
    if args.output and args.output.resolve() in {args.elf.resolve(), args.sdkconfig.resolve()}:
        parser.error("report path must not overwrite an input")
    try:
        report = audit(args)
    except (ValueError, OSError, subprocess.CalledProcessError, KeyError, IndexError,
            struct.error) as error:
        report = {"success": False, "camera_teardown_link_verified": False, "error": str(error)}
    rendered = json.dumps(report, ensure_ascii=False, indent=2)
    if args.output:
        args.output.write_text(rendered + "\n", encoding="utf-8")
        print(f"Camera teardown audit: success={report['success']} report={args.output}")
        if not report["success"]:
            print(report["error"], file=sys.stderr)
    else:
        print(rendered)
    return 0 if report["success"] else 1


if __name__ == "__main__":
    sys.exit(main())

"""Fail-closed ABI and final linked-call audit for scoped screen JPEG allocation.

Input archive, map and ELF are never modified; only reports and temporary
analysis views are written. A successful preflight is not a linked-ELF audit.
"""
from __future__ import annotations

import argparse
from collections import Counter, defaultdict
import hashlib
import json
from pathlib import Path
import re
import struct
import subprocess
import sys
import tempfile

ALLOCATORS = ("jpeg_calloc", "jpeg_calloc_inner", "jpeg_calloc_align", "jpeg_calloc_align_inner")
PROTOTYPES = {
    "jpeg_calloc": ["size_t", "size_t"],
    "jpeg_calloc_inner": ["size_t"],
    "jpeg_calloc_align": ["size_t", "int"],
    "jpeg_calloc_align_inner": ["size_t", "int"],
}
PINS = {
    "lib/esp32s3/libesp_new_jpeg.a": "4205b1258ce0ef9fd9946abfc7cd5f105b08316ea567e1136cec60b3e6330896",
    "include/esp_jpeg_common.h": "548c85257c3cecd19c07015be2257ea870b8c9b5c31de9668c1884d5fe387351",
    "include/esp_jpeg_enc.h": "a73eeee202e52b9285bcfa6cbf2d8d803c2f96e2b3dd6338d26736c3e6eb5deb",
    "idf_component.yml": "7bb885d359b0e225493159853f42faa24ca0f492e8d5b15797d8dcf012307b3d",
}
COMPONENT_HASH = "e6af208a875abd0ecfc0213d3751a11b504b463ebde6930f24096047925fa5c1"
# Instructions present in the pinned allocator-calling codec functions, plus
# direct calls produced by linker relaxation. Unreviewed opcodes fail closed.
CODEC_OPCODES = frozenset("""
add add.n addi addi.n addmi addx2 addx4 and bany bbci bbsi beq beqi beqz beqz.n
bge bgeu bgeui blt blti bltu bltui bne bnei bnez bnez.n bnone call8 callx8 entry
extui j l16si l16ui l32i l32i.n l32r l8ui loop mov.n moveqz movgez movi movi.n
movnez mull neg or quos quou rems remu retw.n s16i s32i s32i.n s8i sll slli srai
srli ssl sub xor
""".split())


def require(condition, message):
    if not condition:
        raise ValueError(message)


def digest(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def run_tool(prefix, name, *args):
    binary = str(prefix) + name
    if not Path(binary).exists() and Path(binary + ".exe").exists():
        binary += ".exe"
    result = subprocess.run([binary, *map(str, args)], text=True, capture_output=True, check=True)
    return result.stdout


def check_identity(repo, target, idf_version, sdkconfig=None):
    require(target == "esp32s3", "unreviewed target")
    require(idf_version == "6.0.2", "unreviewed ESP-IDF version")
    component = Path(repo) / "managed_components/espressif__esp_new_jpeg"
    hashes = {}
    for name, expected in PINS.items():
        actual = digest(component / name)
        require(actual == expected, f"unreviewed bytes: {name}")
        hashes[name] = actual
    manifest = (Path(repo) / "main/idf_component.yml").read_text(encoding="utf-8")
    require(re.search(r'espressif/esp_new_jpeg:\s*[\"\']0\.6\.1[\"\']', manifest), "project JPEG version drift")
    lock = (Path(repo) / "dependencies.lock").read_text(encoding="utf-8")
    block = re.search(r"(?ms)^  espressif/esp_new_jpeg:\n(.*?)(?=^  \S|\Z)", lock)
    require(block and COMPONENT_HASH in block.group(1), "lock component identity drift")
    require(re.search(r"\bversion:\s*['\"]?0\.6\.1\b", block.group(1)), "lock JPEG version drift")
    config = Path(sdkconfig or Path(repo) / "sdkconfig").read_text(encoding="utf-8")
    require('CONFIG_IDF_TARGET="esp32s3"' in config and "CONFIG_IDF_TARGET_ESP32S3=y" in config,
            "resolved sdkconfig target drift")
    require("CONFIG_HEAP_ABORT_WHEN_ALLOCATION_FAILS=y" not in config,
            "heap abort prevents recoverable PSRAM OOM")
    return component, hashes


def dwarf_prototypes(text):
    member = re.search(r"(?ms)^esp_jpeg_memory\.c\.obj:.*?(?=^\S+\.obj:|\Z)", text)
    require(member, "missing allocator DWARF member")
    dies, stack, current = {}, [], None
    for line in member.group().splitlines():
        head = re.match(r"\s*<(\d+)><([0-9a-f]+)>:.*\((DW_TAG_\w+)\)", line)
        if head:
            depth, offset, tag = int(head[1]), int(head[2], 16), head[3]
            while stack and stack[-1][0] >= depth:
                stack.pop()
            current = {"tag": tag, "children": []}
            dies[offset] = current
            if stack:
                dies[stack[-1][1]]["children"].append(offset)
            stack.append((depth, offset))
            continue
        if re.match(r"\s*<\d+><[0-9a-f]+>: Abbrev Number: 0", line):
            current = None
        attr = re.search(r"DW_AT_(name|type|byte_size|encoding)\s*:\s*(.*)", line)
        if current is not None and attr:
            value = attr[2].strip()
            if attr[1] == "name":
                value = value.rsplit(": ", 1)[-1]
            if attr[1] == "type":
                value = int(re.search(r"<0x([0-9a-f]+)>", value)[1], 16)
            current[attr[1]] = value
    def typename(offset):
        die = dies[offset]
        if die["tag"] == "DW_TAG_pointer_type" and "type" not in die:
            require(die.get("byte_size") == "4", "unexpected pointer width")
            return "void*"
        if die.get("name") == "size_t":
            base = dies[die["type"]]
            require(base.get("byte_size") == "4" and "unsigned" in base.get("name", ""),
                    "unexpected size_t ABI")
            return "size_t"
        if die.get("name") == "int":
            require(die.get("byte_size") == "4", "unexpected int ABI")
            return "int"
        return die.get("name", die["tag"])
    found = {}
    for die in dies.values():
        if die["tag"] != "DW_TAG_subprogram" or die.get("name") not in ALLOCATORS:
            continue
        parameters = [typename(dies[child]["type"]) for child in die["children"]
                      if dies[child]["tag"] == "DW_TAG_formal_parameter"]
        require(typename(die["type"]) == "void*", "allocator return ABI drift")
        found[die["name"]] = parameters
    require(found == PROTOTYPES, f"allocator prototype drift: {found}")
    return found


def relocation_calls(text):
    caller = None
    calls = defaultdict(Counter)
    for line in text.splitlines():
        section = re.match(r"RELOCATION RECORDS FOR \[\.text\.(.+)\]:", line)
        if section:
            caller = section[1]
        elif line.startswith("RELOCATION RECORDS FOR") or "file format" in line:
            caller = None
        reference = re.match(r"\s*[0-9a-f]+\s+R_XTENSA_ASM_EXPAND\s+(\w+)\s*$", line)
        if caller and reference and reference[1] in ALLOCATORS:
            calls[caller][reference[1]] += 1
    require(calls, "no allocator call relocations found")
    return {name: dict(counts) for name, counts in calls.items()}


def relocation_owners(text):
    member = None
    owners = {}
    for line in text.splitlines():
        heading = re.match(r"^(\S+\.obj):\s+file format", line)
        if heading:
            member = heading[1]
        section = re.match(r"RELOCATION RECORDS FOR \[\.text\.(.+)\]:", line)
        if section and member:
            require(section[1] not in owners or owners[section[1]] == member,
                    "ambiguous archive function owner")
            owners[section[1]] = member
    return owners


def verify_map(text, symbols, retained, owners, archive):
    entries = defaultdict(list)
    for match in re.finditer(r"(?m)^\s*\.text\.(\S+)\s+0x([0-9a-f]+)\s+0x([0-9a-f]+)\s+([^\r\n]+)", text):
        entries[match[1]].append((int(match[2], 16), int(match[3], 16), match[4].strip()))
    expected_archive = str(archive.resolve()).replace("\\", "/").casefold()
    selected = {}
    for name in set(retained) | set(ALLOCATORS):
        matches = [entry for entry in entries[name] if entry[0] == symbols[name]["address"] and
                   entry[1] == symbols[name]["size"]]
        require(len(matches) == 1, f"map/ELF function address or size mismatch: {name}")
        owner = matches[0][2].replace("\\", "/")
        require(owner.casefold() == f"{expected_archive}({owners[name]})".casefold(),
                f"unreviewed linked archive/member: {name}: {owner}")
        selected[name] = owner
    return selected


class Elf32:
    def __init__(self, path):
        self.data = Path(path).read_bytes()
        require(self.data[:7] == b"\x7fELF\x01\x01\x01", "expected little-endian ELF32")
        require(struct.unpack_from("<H", self.data, 18)[0] == 94, "expected Xtensa machine")
        shoff = struct.unpack_from("<I", self.data, 32)[0]
        entsize, count, strings_index = struct.unpack_from("<HHH", self.data, 46)
        require(entsize == 40 and count > 0, "unsupported ELF section layout")
        raw = [struct.unpack_from("<10I", self.data, shoff + i * entsize) for i in range(count)]
        names = self.data[raw[strings_index][4]:raw[strings_index][4] + raw[strings_index][5]]
        self.section_names_offset = raw[strings_index][4]
        self.sections = [{"name": self.cstring(names, h[0]), "type": h[1], "flags": h[2], "address": h[3],
                          "offset": h[4], "size": h[5], "link": h[6], "entsize": h[9],
                          "name_offset": h[0]} for h in raw]
        self.symbols = {}
        for section in self.sections:
            if section["type"] != 2:
                continue
            strings = self.sections[section["link"]]
            strings = self.data[strings["offset"]:strings["offset"] + strings["size"]]
            require(section["entsize"] == 16, "unsupported symbol size")
            for offset in range(section["offset"], section["offset"] + section["size"], 16):
                name, value, size, info, other, index = struct.unpack_from("<IIIBBH", self.data, offset)
                name = self.cstring(strings, name)
                if name and index != 0:
                    self.symbols[name] = {"address": value, "size": size, "type": info & 15,
                                          "section": index}
        require(self.symbols, "stripped or missing ELF symbols")

    def load_identity(self):
        sections = {}
        for section in self.sections:
            if not section["flags"] & 2:
                continue
            start, size = section["offset"], section["size"]
            sections[section["name"]] = (section["address"], size, section["type"], section["flags"],
                hashlib.sha256(self.data[start:start + size]).hexdigest() if section["type"] != 8 else None)
        offset = struct.unpack_from("<I", self.data, 28)[0]
        width, count = struct.unpack_from("<HH", self.data, 42)
        require(width == 32, "unexpected program header size")
        segments = []
        for index in range(count):
            kind, position, vaddr, paddr, filesz, memsz, flags, align = struct.unpack_from("<8I", self.data, offset + index * width)
            if kind == 1:
                segments.append((vaddr, paddr, filesz, memsz, flags, align,
                                 hashlib.sha256(self.data[position:position + filesz]).hexdigest()))
        return {"sections": sections, "load_segments": segments}

    @staticmethod
    def cstring(data, offset):
        return data[offset:data.find(b"\0", offset)].decode("utf-8", errors="strict")

    def word(self, address):
        for section in self.sections:
            start = section["address"]
            if section["type"] != 8 and start <= address and address + 4 <= start + section["size"]:
                return struct.unpack_from("<I", self.data, section["offset"] + address - start)[0]
        raise ValueError(f"literal outside readable ELF sections: {address:x}")


def linked_calls(disassembly, word, functions=None):
    """Resolve direct calls and compiler l32r→callx sequences, conservatively.

    Register constants do not survive calls or branches. Unknown indirect calls
    are retained as unresolved; required allocation counts then fail closed.
    """
    edges, unresolved, registers, caller = [], [], {}, None
    for line in disassembly.splitlines():
        header = re.match(r"^([0-9a-f]+) <(.+)>:$", line)
        if header:
            caller, registers = header[2], {}
            if functions is not None and (caller not in functions or functions[caller]["type"] != 2):
                caller = None
            continue
        instruction = re.match(r"^\s*([0-9a-f]+):\s+[0-9a-f]+\s+([\w.]+)\s*(.*)$", line)
        if not caller or not instruction:
            continue
        address, opcode, operands = int(instruction[1], 16), instruction[2], instruction[3].strip()
        if functions is not None:
            function = functions[caller]
            if not function["address"] <= address < function["address"] + function["size"]:
                continue
        if opcode == "l32r":
            load = re.match(r"(a\d+),\s*([0-9a-f]+)", operands)
            if load:
                try:
                    registers[load[1]] = word(int(load[2], 16))
                except ValueError:
                    registers.pop(load[1], None)
                    unresolved.append({"caller": caller, "address": address,
                                       "register": load[1], "kind": "unreadable-literal"})
            continue
        if re.fullmatch(r"call(?:0|4|8|12)", opcode):
            target = re.match(r"([0-9a-f]+)", operands)
            require(target, f"unparsed direct call at {address:x}")
            edges.append({"caller": caller, "address": address, "target": int(target[1], 16), "kind": opcode})
            registers.clear()
            continue
        if re.fullmatch(r"callx(?:0|4|8|12)", opcode):
            register = operands.split()[0]
            if register in registers:
                edges.append({"caller": caller, "address": address, "target": registers[register], "kind": opcode})
            else:
                unresolved.append({"caller": caller, "address": address, "register": register})
            registers.clear()
            continue
        # Narrow support is intentional: unfamiliar allocator call shapes must
        # fail expected-count verification instead of being guessed correct.
        if opcode.startswith("b") or opcode in {"j", "jx", "ret", "retw", "retw.n"}:
            registers.clear()
        else:
            destination = re.match(r"(a\d+)(?:,|$)", operands)
            if destination:
                registers.pop(destination[1], None)
    return edges, unresolved


def codec_cfg(name, info, disassemble, word, allowed_opcodes=CODEC_OPCODES):
    """Decode only reachable blocks, never fall through a jump into padding.

    The pinned old archive loses non-COMDAT .xt.prop during IDF 6 section GC.
    A temporary metadata-free view is used, with unchanged load bytes. Every
    basic block starts at the function entry or an actual branch destination.
    """
    start, end = info["address"], info["address"] + info["size"]
    pending, visited, occupied = [start], set(), {}
    edges, unresolved = [], []
    while pending:
        pc = pending.pop()
        if pc in visited:
            continue
        require(start <= pc < end and pc not in occupied, f"invalid/overlapping codec branch: {name}: {pc:x}")
        block = [f"{pc:08x} <{name}>:"]
        instructions = []
        for line in disassemble(pc, end).splitlines():
            match = re.match(r"^\s*([0-9a-f]+):\s+([0-9a-f]+)\s+([\w.]+)\s*(.*)$", line)
            if match:
                instructions.append((int(match[1], 16), match[2], match[3], match[4].strip(), line))
        require(instructions and instructions[0][0] == pc, f"cannot decode codec entry: {name}: {pc:x}")
        terminated = False
        for address, raw, opcode, operands, line in instructions:
            require(address == pc, f"codec instruction gap: {name}: {pc:x}")
            if address in visited:
                terminated = True
                break
            width = len(raw) // 2
            require(width in (2, 3, 4) and address + width <= end and
                    opcode in allowed_opcodes,
                    f"unknown codec instruction/jump: {name}: {address:x}: {opcode}")
            require(not any(byte in occupied for byte in range(address, address + width)),
                    f"overlapping codec instruction: {name}: {address:x}")
            visited.add(address)
            for byte in range(address, address + width):
                occupied[byte] = address
            block.append(line)
            pc = address + width
            if opcode in {"ret", "ret.n", "retw", "retw.n"}:
                terminated = True
                break
            if opcode == "j" or opcode.startswith("b") or opcode.startswith("loop"):
                branch = re.search(r"(?:^|,\s*)([0-9a-f]+)\s*(?:<[^>]+>)?$", operands)
                require(branch, f"unrecognized codec branch: {name}: {address:x}")
                target = int(branch[1], 16)
                require(start <= target < end, f"codec branch outside function: {name}: {target:x}")
                pending.append(target)
                if opcode != "j":
                    pending.append(pc)
                terminated = True
                break
        require(terminated, f"unterminated codec block: {name}: {pc:x}")
        block_edges, block_unresolved = linked_calls("\n".join(block), word)
        edges.extend(block_edges)
        unresolved.extend(block_unresolved)
    return edges, unresolved


def make_analysis_view(elf, view):
    # objcopy reconstructs PT_LOAD memsz for IDF's overlapping NOBITS layout.
    # Rename only the non-ALLOC section's name in a byte-for-byte copy, so
    # every section body/program header remains exactly the final ELF's.
    data = bytearray(elf.data)
    properties = [section for section in elf.sections if section["name"] == ".xt.prop"]
    require(len(properties) == 1 and not properties[0]["flags"] & 2,
            "expected exactly one non-loadable Xtensa property table")
    offset = elf.section_names_offset + properties[0]["name_offset"]
    data[offset:offset + 8] = b".no.prop"
    view.write_bytes(data)
    return verify_analysis_view(elf, Elf32(view))


def verify_analysis_view(elf, analyzed):
    identity = elf.load_identity()
    require(analyzed.load_identity() == identity, "audit view changed loadable firmware bytes or layout")
    require(analyzed.symbols == elf.symbols, "audit view changed ELF symbols")
    return identity


def audited_codec_calls(args, elf, retained):
    directory = args.output.parent if args.output else None
    with tempfile.TemporaryDirectory(prefix="jpeg-audit-", dir=directory) as temporary:
        view = Path(temporary) / "analysis-only.elf"
        identity = make_analysis_view(elf, view)
        edges, unresolved = [], []
        for name in sorted(retained):
            disassemble = lambda start, end: run_tool(args.tool_prefix, "objdump", "-d",
                f"--start-address={start}", f"--stop-address={end}", view)
            function_edges, function_unresolved = codec_cfg(name, elf.symbols[name], disassemble, elf.word)
            edges.extend(function_edges)
            unresolved.extend(function_unresolved)
        return edges, unresolved, {"view_sha256": digest(view), "load_identity": identity,
                                   "method": "pinned-codec-reachable-CFG-on-load-identical-analysis-view"}


def audited_scope_calls(args, elf):
    # The new object's properties can also be GC'd. Decode branch targets in
    # the original ELF, so alignment padding cannot hide a wrapper heap call.
    required = {name for name, info in elf.symbols.items() if info["type"] == 2 and
                (name.startswith("__wrap_jpeg_") or "ScreenJpegAllocationScope" in name)}
    edges, unresolved = [], []
    for name in sorted(required):
        disassemble = lambda start, end: run_tool(args.tool_prefix, "objdump", "-d",
            f"--start-address={start}", f"--stop-address={end}", args.elf)
        found, unknown = codec_cfg(name, elf.symbols[name], disassemble, elf.word,
                                   CODEC_OPCODES | {"rur.threadptr"})
        edges.extend(found)
        unresolved.extend(unknown)
    return required, edges, unresolved


def verify_edges(symbols, expected, edges, unresolved):
    originals, wrappers = {}, {}
    for name in ALLOCATORS:
        require(name in symbols and "__wrap_" + name in symbols, f"missing linked allocator/wrapper: {name}")
        originals[name] = symbols[name]["address"]
        wrappers[name] = symbols["__wrap_" + name]["address"]
        require(originals[name] != wrappers[name], "wrapper aliases original allocator")
    calls = defaultdict(Counter)
    for edge in edges:
        calls[edge["caller"]][edge["target"]] += 1
        for name, target in originals.items():
            if edge["target"] == target:
                require(edge["caller"] == "__wrap_" + name,
                        f"allocator bypass: {edge['caller']} -> {name} at {edge['address']:x}")
    retained = {}
    for caller, counts in expected.items():
        if caller not in symbols:
            continue  # --gc-sections can remove complete codec functions.
        retained[caller] = counts
        require(not any(edge["caller"] == caller for edge in unresolved),
                f"unresolved call inside retained codec caller: {caller}")
        for name, count in counts.items():
            require(calls[caller][wrappers[name]] == count,
                    f"missing/unrecognized wrapped call: {caller} -> {name}, expected {count}, got {calls[caller][wrappers[name]]}")
    require("jpeg_enc_open" in retained, "screen codec open allocation caller was not verified")
    for name in ALLOCATORS:
        caller = "__wrap_" + name
        require(calls[caller][originals[name]] == 1, f"missing real passthrough: {caller}")
        require(calls[caller][wrappers[name]] == 0, f"recursive wrapper: {caller}")
        require(not any(edge["caller"] == caller for edge in unresolved), f"unresolved call inside {caller}")
        heap_name = "heap_caps_aligned_calloc" if "align" in name else "heap_caps_calloc"
        require(heap_name in symbols and calls[caller][symbols[heap_name]["address"]] == 1,
                f"missing exact PSRAM heap call: {caller}")
        require(sum(calls[caller].values()) == 2, f"unexpected call inside {caller}")
    return retained


def verify_scope(elf, edges):
    tls = {name: info for name, info in elf.symbols.items() if "g_screen_jpeg_scope_active" in name}
    require(len(tls) == 1 and next(iter(tls.values()))["type"] == 6 and
            next(iter(tls.values()))["size"] == 1, "scope flag is not one-byte native TLS")
    scope = {name: info for name, info in elf.symbols.items() if "ScreenJpegAllocationScope" in name and info["type"] == 2}
    constructors = {v["address"] for k, v in scope.items() if re.search(r"C[12]Ev$", k)}
    destructors = {v["address"] for k, v in scope.items() if re.search(r"D[12]Ev$", k)}
    require(constructors and destructors, "scope lifetime functions missing/inlined; audit required")
    enters = [edge for edge in edges if edge["target"] in constructors]
    leaves = [edge for edge in edges if edge["target"] in destructors]
    require(enters and leaves, "no screen codec scope lifetime calls")
    for edge in enters + leaves:
        require("DisplayService" in edge["caller"] and "EncodeJpeg" in edge["caller"],
                f"scope used outside screen codec: {edge['caller']}")
    forbidden = ("emutls", "pthread_setspecific", "__cxa_thread_atexit", "_ZTH")
    # A single address can have constructor aliases; never hide a forbidden
    # target just because another symbol happens to share its address.
    addresses = defaultdict(list)
    for name, info in elf.symbols.items():
        addresses[info["address"]].append(name)
    for edge in edges:
        if edge["caller"].startswith("__wrap_jpeg_") or "ScreenJpegAllocationScope" in edge["caller"]:
            target_names = addresses[edge["target"]]
            require(not any(part in name for name in target_names for part in forbidden),
                    "dynamic/emulated TLS allocation found")
            require("ScreenJpegAllocationScope" not in edge["caller"],
                    "unexpected call inside trivial TLS scope lifetime")
    boundaries = {name: elf.symbols[name]["address"] for name in
                  ("_thread_local_data_start", "_thread_local_data_end", "_thread_local_bss_start", "_thread_local_bss_end")
                  if name in elf.symbols}
    require(len(boundaries) == 4, "missing IDF native TLS linker boundaries")
    ordered = [boundaries[name] for name in
               ("_thread_local_data_start", "_thread_local_data_end", "_thread_local_bss_start", "_thread_local_bss_end")]
    require(ordered == sorted(ordered) and ordered[0] < ordered[-1], "invalid native TLS linker boundaries")
    size = boundaries["_thread_local_bss_end"] - boundaries["_thread_local_data_start"]
    flag = next(iter(tls.values()))
    require(0 < flag["section"] < len(elf.sections), "invalid scope TLS section")
    section = elf.sections[flag["section"]]
    require(section["flags"] & 0x400 and section["type"] in (1, 8),
            "scope flag must be in a native TLS section")
    # Defined STT_TLS symbol values are offsets within the TLS block, not VMAs.
    require(ordered[2] - ordered[0] <= flag["address"] and flag["address"] + flag["size"] <= size,
            "scope flag lies outside native TLS BSS")
    address = ordered[0] + flag["address"]
    require(section["address"] <= address and address < section["address"] + section["size"],
            "scope flag lies outside its TLS section")
    # IDF 6.0.2 merges .tdata and .tbss inputs into .flash.tdata; when .tdata
    # exists this output section is PROGBITS, not NOBITS. Inspect the zero byte.
    if section["type"] == 1:
        offset = section["offset"] + address - section["address"]
        require(elf.data[offset:offset + 1] == b"\0", "scope flag is not constant-zero TLS")
    return {"flag": tls, "boundaries": boundaries, "per_task_aligned_bytes": (size + 15) & ~15,
            "scope_calls": enters + leaves}


def verify_native_tls(elf, disassembly, unresolved):
    require(any(section["name"] == ".xt.prop" and section["size"] for section in elf.sections),
            "missing Xtensa instruction properties; disassembly may misread padding")
    relevant = {name: info for name, info in elf.symbols.items()
                if name.startswith("__wrap_jpeg_") or
                ("ScreenJpegAllocationScope" in name and info["type"] == 2)}
    blocks = list(re.finditer(r"(?m)^([0-9a-f]+) <(.+)>:$", disassembly))
    native = set()
    for index, block in enumerate(blocks):
        body = disassembly[block.end():blocks[index + 1].start() if index + 1 < len(blocks) else len(disassembly)]
        if re.search(r"\brur\.threadptr\b", body):
            native.add(int(block[1], 16))
    for name, info in relevant.items():
        require(info["address"] in native, f"missing native thread-pointer access: {name}")
        require(not any(edge["caller"] == name for edge in unresolved), f"unresolved TLS scope call: {name}")
    return sorted(native & {info["address"] for info in relevant.values()})


def preflight(args):
    component, hashes = check_identity(args.repo, args.target, args.idf_version, args.sdkconfig)
    archive = component / "lib/esp32s3/libesp_new_jpeg.a"
    dwarf = run_tool(args.tool_prefix, "objdump", "--dwarf=info", archive)
    prototypes = dwarf_prototypes(dwarf)
    relocations = run_tool(args.tool_prefix, "objdump", "-r", archive)
    expected = relocation_calls(relocations)
    body = run_tool(args.tool_prefix, "objdump", "-d", "-r", *sum((["-j", ".text." + name] for name in ALLOCATORS), []), archive)
    return {"mode": "preflight-only", "target": args.target, "idf_version_assertion": args.idf_version,
            "hashes": hashes, "prototypes": prototypes, "expected_callers": expected,
            "archive_members": relocation_owners(relocations),
            "allocator_disassembly_sha256": hashlib.sha256(body.encode()).hexdigest(),
            "allocator_disassembly": body, "allocator_link_verified": False}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--repo", type=Path, required=True)
    parser.add_argument("--tool-prefix", required=True)
    parser.add_argument("--target", required=True)
    parser.add_argument("--idf-version", required=True)
    parser.add_argument("--sdkconfig", type=Path)
    parser.add_argument("--elf", type=Path)
    parser.add_argument("--map", type=Path)
    parser.add_argument("--baseline-elf", type=Path)
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    try:
        report = preflight(args)
        if args.elf:
            require(args.map is not None, "linked verification requires the matching linker map")
            elf = Elf32(args.elf)
            disassembly = run_tool(args.tool_prefix, "objdump", "-d", args.elf)
            edges, unresolved = linked_calls(disassembly, elf.word, elf.symbols)
            retained = {name: counts for name, counts in report["expected_callers"].items() if name in elf.symbols}
            archive = args.repo / "managed_components/espressif__esp_new_jpeg/lib/esp32s3/libesp_new_jpeg.a"
            owners = verify_map(args.map.read_text(encoding="utf-8"), elf.symbols, retained,
                                report["archive_members"], archive)
            codec_edges, codec_unresolved, view_report = audited_codec_calls(args, elf, retained)
            edges = [edge for edge in edges if edge["caller"] not in retained] + codec_edges
            unresolved = [edge for edge in unresolved if edge["caller"] not in retained] + codec_unresolved
            scoped, scope_edges, scope_unknown = audited_scope_calls(args, elf)
            edges = [edge for edge in edges if edge["caller"] not in scoped] + scope_edges
            unresolved = [edge for edge in unresolved if edge["caller"] not in scoped] + scope_unknown
            verify_edges(elf.symbols, report["expected_callers"], edges, unresolved)
            scope = verify_scope(elf, edges)
            scope["native_thread_pointer_functions"] = verify_native_tls(elf, disassembly, unresolved)
            report.update(mode="linked-elf", allocator_link_verified=True, elf_sha256=digest(args.elf),
                          retained_callers=retained, scope=scope,
                          linked_archive_members=owners, map_sha256=digest(args.map),
                          codec_analysis_view=view_report,
                          unrelated_unresolved_count=len([edge for edge in unresolved
                              if edge["caller"] not in retained and edge["caller"] not in scoped]),
                          unrelated_unresolved=[edge for edge in unresolved
                              if edge["caller"] not in retained and edge["caller"] not in scoped],
                          allocator_edges=[e for e in edges if e["caller"] in retained or e["caller"].startswith("__wrap_jpeg_")])
            if args.baseline_elf:
                baseline = Elf32(args.baseline_elf)
                size = baseline.symbols["_thread_local_bss_end"]["address"] - baseline.symbols["_thread_local_data_start"]["address"]
                report["tls_aligned_delta_per_task"] = scope["per_task_aligned_bytes"] - ((size + 15) & ~15)
                report["baseline_elf_sha256"] = digest(args.baseline_elf)
        report["success"] = True
    except (ValueError, OSError, subprocess.CalledProcessError, KeyError, struct.error) as error:
        report = {"success": False, "allocator_link_verified": False, "error": str(error)}
    rendered = json.dumps(report, ensure_ascii=False, indent=2)
    if args.output:
        args.output.write_text(rendered + "\n", encoding="utf-8")
    if args.output:
        print(f"JPEG allocator audit: success={report['success']} linked={report['allocator_link_verified']} report={args.output}")
        if not report["success"]:
            print(report["error"], file=sys.stderr)
    else:
        print(rendered)
    return 0 if report["success"] else 1


if __name__ == "__main__":
    sys.exit(main())

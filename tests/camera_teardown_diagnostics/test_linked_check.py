from pathlib import Path
from types import SimpleNamespace
import contextlib
import io
import json
import struct
import sys
import tempfile
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "tools"))
import check_camera_teardown_diagnostics as check


class Fixture:
    def __init__(self, directory):
        self.path = directory / "test.elf"
        self.config = directory / "sdkconfig"
        self.config.write_text('CONFIG_IDF_TARGET="esp32s3"\nCONFIG_IDF_TARGET_ESP32S3=y\n')
        self.storage = 0x3FC90000
        self.iram = 0x40378000
        self.code = self.iram + 0x20
        self.instructions = [
            (0x00, "004136", "entry", "a1, 32"),
            (0x03, "ffff81", "l32r", f"a8, {self.iram:x} <literal>"),
            (0x06, "180c", "movi.n", "a8, 1"),
            (0x08, "130c90", "wsr.scompare1", "a9"),
            (0x0B, "00e982", "s32c1i", "a8, a9, 0"),
            (0x0E, "18cc", "bnez.n", f"a8, {self.code + 0x12:x} <exit>"),
            (0x10, "0899", "s32i.n", "a9, a8, 0"),
            (0x12, "f01d", "retw.n", ""),
        ]
        self.names = b"\0.iram0.text\0.dram0.data\0.shstrtab\0.symtab\0.strtab\0"
        self.strings = ("\0" + check.STORAGE + "\0" + check.RECORDER + "\0").encode()
        self.data = bytearray(0xA00)
        self.data[:7] = b"\x7fELF\x01\x01\x01"
        struct.pack_into("<HHIIIIIHHHHHH", self.data, 16,
                         2, 94, 1, self.code, 52, 0x800, 0, 52, 32, 2, 40, 6, 3)
        struct.pack_into("<8I", self.data, 52,
                         1, 0x100, self.iram, self.iram, 0x34, 0x34, 5, 4)
        struct.pack_into("<8I", self.data, 84,
                         1, 0x200, self.storage, self.storage, 536, 536, 6, 4)
        struct.pack_into("<I", self.data, 0x100, self.storage + 12)
        for relative, raw, _opcode, _operands in self.instructions:
            encoded = bytes.fromhex(raw)[::-1]
            self.data[0x120 + relative:0x120 + relative + len(encoded)] = encoded
        struct.pack_into("<6I", self.data, 0x200, check.MAGIC, 1, 32, 0, 0, 0)
        self.data[0x480:0x480 + len(self.strings)] = self.strings
        struct.pack_into("<IIIBBH", self.data, 0x510,
                         1, self.storage, 536, 0x11, 0, 2)
        struct.pack_into("<IIIBBH", self.data, 0x520,
                         len(check.STORAGE) + 2, self.code, 0x14, 0x12, 0, 1)
        self.data[0x600:0x600 + len(self.names)] = self.names
        self.section(1, ".iram0.text", 1, 6, self.iram, 0x100, 0x34)
        self.section(2, ".dram0.data", 1, 3, self.storage, 0x200, 536)
        self.section(3, ".shstrtab", 3, 0, 0, 0x600, len(self.names))
        self.section(4, ".symtab", 2, 0, 0, 0x500, 48, 5, 16)
        self.section(5, ".strtab", 3, 0, 0, 0x480, len(self.strings))

    def section(self, index, name, kind, flags, address, offset, size, link=0, entsize=0):
        struct.pack_into("<10I", self.data, 0x800 + index * 40,
                         self.names.index(name.encode()), kind, flags, address, offset,
                         size, link, 0, 4, entsize)

    def write(self):
        self.path.write_bytes(self.data)
        return check.Elf32(self.path)

    def disassemble(self, start, end):
        return "\n".join(f"{self.code + offset:x}: {raw} {opcode} {operands}"
                         for offset, raw, opcode, operands in self.instructions
                         if start <= self.code + offset < end)

    def verify(self):
        elf = self.write()
        storage, recorder = check.verify_layout(elf)
        return check.verify_recorder(elf, storage, recorder, self.disassemble)


class LinkedCheckTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.directory = Path(self.temp.name)
        self.fixture = Fixture(self.directory)

    def test_positive_exact_abi_native_cas_and_forward_control_flow(self):
        report = self.fixture.verify()
        self.assertEqual(report["native_cas_count"], 1)
        self.assertEqual(report["calls"], [])
        self.assertEqual(report["backward_branches"], [])
        self.assertEqual(report["instruction_count"], 8)
        self.assertEqual(report["unreachable_alignment"], [])

    def test_rejects_relocatable_and_non_xtensa(self):
        for field, value in ((16, 1), (18, 3)):
            with self.subTest(field=field), self.assertRaises(ValueError):
                fixture = Fixture(self.directory)
                struct.pack_into("<H", fixture.data, field, value)
                fixture.verify()

    def test_rejects_wrong_storage_size_alignment_and_external_address(self):
        for offset, value in ((0x518, 532), (0x514, self.fixture.storage + 1),
                              (0x514, 0x3C100000)):
            with self.subTest(offset=offset, value=value), self.assertRaises(ValueError):
                fixture = Fixture(self.directory)
                struct.pack_into("<I", fixture.data, offset, value)
                fixture.verify()

    def test_rejects_wrong_header_and_nonzero_initial_record(self):
        for offset in (0x200, 0x204, 0x208, 0x20C, 0x210, 0x214, 0x218, 0x227):
            with self.subTest(offset=offset), self.assertRaises(ValueError):
                fixture = Fixture(self.directory)
                fixture.data[offset] ^= 1
                fixture.verify()

    def test_rejects_storage_section_or_load_mismatch(self):
        changes = ((0x800 + 2 * 40 + 4, 8),   # NOBITS
                   (0x800 + 2 * 40 + 8, 7),  # executable data
                   (84 + 24, 4),             # read-only load segment
                   (84 + 16, 532),           # truncated file payload
                   (84 + 4, 0x204))          # load bytes differ from section
        for offset, value in changes:
            with self.subTest(offset=offset), self.assertRaises(ValueError):
                fixture = Fixture(self.directory)
                struct.pack_into("<I", fixture.data, offset, value)
                fixture.verify()

    def test_rejects_missing_duplicate_weak_or_local_symbols(self):
        for change in ("missing", "duplicate", "weak", "local"):
            with self.subTest(change=change), self.assertRaises(ValueError):
                fixture = Fixture(self.directory)
                if change == "missing":
                    struct.pack_into("<H", fixture.data, 0x51E, 0)
                elif change == "duplicate":
                    fixture.data[0x530:0x540] = fixture.data[0x510:0x520]
                    struct.pack_into("<I", fixture.data, 0x800 + 4 * 40 + 20, 64)
                else:
                    fixture.data[0x51C] = 0x21 if change == "weak" else 0x01
                fixture.verify()

    def test_rejects_flash_unaligned_or_oversized_recorder(self):
        for offset, value in ((0x524, 0x42010000), (0x524, self.fixture.code + 1),
                              (0x528, 513)):
            with self.subTest(offset=offset, value=value), self.assertRaises(ValueError):
                fixture = Fixture(self.directory)
                struct.pack_into("<I", fixture.data, offset, value)
                fixture.verify()

    def test_rejects_calls_indirect_control_loops_and_unknown_opcodes(self):
        for opcode in ("call8", "callx8", "call0", "jx", "loop", "loopnez", "ill",
                       "rsil", "waiti", "unknown"):
            with self.subTest(opcode=opcode), self.assertRaisesRegex(ValueError, "opcode"):
                fixture = Fixture(self.directory)
                offset, raw, _, operands = fixture.instructions[2]
                fixture.instructions[2] = (offset, raw, opcode, operands)
                fixture.verify()

    def test_rejects_zero_or_two_native_cas(self):
        for index, opcode in ((4, "movi"), (3, "s32c1i")):
            with self.subTest(index=index), self.assertRaisesRegex(ValueError, "exactly one"):
                fixture = Fixture(self.directory)
                offset, raw, _, operands = fixture.instructions[index]
                fixture.instructions[index] = (offset, raw, opcode, operands)
                fixture.verify()

    def test_rejects_missing_compare_register_setup(self):
        offset, raw, _, operands = self.fixture.instructions[3]
        self.fixture.instructions[3] = (offset, raw, "movi", operands)
        with self.assertRaisesRegex(ValueError, "compare register"):
            self.fixture.verify()

    def test_rejects_backward_self_outside_and_overlapping_targets(self):
        for relative in (0, 0x0E, 0x40, 0x13):
            with self.subTest(relative=relative), self.assertRaises(ValueError):
                fixture = Fixture(self.directory)
                offset, raw, opcode, _ = fixture.instructions[5]
                fixture.instructions[5] = (offset, raw, opcode,
                                          f"a8, {fixture.code + relative:x} <target>")
                fixture.verify()

    def test_rejects_instruction_gap_and_byte_mismatch(self):
        for change in ("gap", "bytes", "no_return"):
            with self.subTest(change=change), self.assertRaises(ValueError):
                fixture = Fixture(self.directory)
                if change == "gap":
                    fixture.instructions.pop(2)
                elif change == "bytes":
                    fixture.data[0x126] ^= 1
                else:
                    fixture.instructions.pop()
                fixture.verify()

    def test_rejects_flash_literals_and_external_or_unrelated_literal_values(self):
        for change in ("flash_literal", "unaligned_literal", "psram_value", "unrelated_value"):
            with self.subTest(change=change), self.assertRaises(ValueError):
                fixture = Fixture(self.directory)
                if change.endswith("literal"):
                    address = 0x42000000 if change == "flash_literal" else fixture.iram + 1
                    offset, raw, opcode, _ = fixture.instructions[1]
                    fixture.instructions[1] = (offset, raw, opcode, f"a8, {address:x} <literal>")
                else:
                    value = 0x3C100000 if change == "psram_value" else fixture.storage + 536
                    struct.pack_into("<I", fixture.data, 0x100, value)
                fixture.verify()

    def test_accepts_real_objdump_resolved_literal_annotation(self):
        offset, raw, opcode, operands = self.fixture.instructions[1]
        self.fixture.instructions[1] = (
            offset, raw, opcode, operands +
            f" ({self.fixture.storage + 12:x} <rodak_camera_teardown_diagnostics+0xc>)")
        report = self.fixture.verify()
        self.assertEqual(report["literals"][0]["value"], self.fixture.storage + 12)

    def test_rejects_mismatched_or_malformed_literal_annotations(self):
        annotations = (" (3c000000 <wrong>)", " (not-a-word)", " (3fc9000c <correct>) junk",
                       " (3fc9000c <correct>", " ((3fc9000c))")
        for annotation in annotations:
            with self.subTest(annotation=annotation), self.assertRaises(ValueError):
                fixture = Fixture(self.directory)
                offset, raw, opcode, operands = fixture.instructions[1]
                fixture.instructions[1] = (offset, raw, opcode, operands + annotation)
                fixture.verify()

    def test_report_cannot_overwrite_elf(self):
        self.fixture.write()
        before = self.fixture.path.read_bytes()
        with contextlib.redirect_stderr(io.StringIO()), self.assertRaises(SystemExit):
            check.main(["--elf", str(self.fixture.path), "--sdkconfig", str(self.fixture.config),
                        "--tool-prefix", "fixture-", "--target", "esp32s3",
                        "--idf-version", "6.0.2", "--output", str(self.fixture.path)])
        self.assertEqual(self.fixture.path.read_bytes(), before)

    def test_cli_reports_exact_input_identity_and_fails_closed(self):
        self.fixture.write()
        output = self.directory / "result.json"
        arguments = ["--elf", str(self.fixture.path), "--sdkconfig", str(self.fixture.config),
                     "--tool-prefix", "fixture-", "--target", "esp32s3", "--idf-version", "6.0.2",
                     "--output", str(output)]
        def fake_tool(_prefix, _name, _mode, start, end, _elf):
            return self.fixture.disassemble(int(start.split("=")[1]), int(end.split("=")[1]))
        before = self.fixture.path.read_bytes()
        with patch.object(check, "run_tool", side_effect=fake_tool), contextlib.redirect_stdout(io.StringIO()):
            self.assertEqual(check.main(arguments), 0)
            positive = json.loads(output.read_text())
            self.assertTrue(positive["camera_teardown_link_verified"])
            self.assertEqual(positive["elf_sha256"], check.digest(self.fixture.path))
            self.assertEqual(self.fixture.path.read_bytes(), before)
            self.fixture.config.write_text('CONFIG_IDF_TARGET="esp32"\n')
            with contextlib.redirect_stderr(io.StringIO()):
                self.assertEqual(check.main(arguments), 1)
            negative = json.loads(output.read_text())
            self.assertFalse(negative["camera_teardown_link_verified"])
            self.assertIn("not ESP32-S3", negative["error"])

    def test_rejects_elf_change_during_disassembly(self):
        self.fixture.write()
        args = SimpleNamespace(elf=self.fixture.path, sdkconfig=self.fixture.config,
                               tool_prefix="fixture-", target="esp32s3", idf_version="6.0.2")
        def change_input(_prefix, _name, _mode, start, end, _elf):
            self.fixture.path.write_bytes(self.fixture.path.read_bytes() + b"changed")
            return self.fixture.disassemble(int(start.split("=")[1]), int(end.split("=")[1]))
        with patch.object(check, "run_tool", side_effect=change_input):
            with self.assertRaisesRegex(ValueError, "changed while"):
                check.audit(args)


if __name__ == "__main__":
    unittest.main()

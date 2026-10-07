from pathlib import Path
import tempfile
import struct
from types import SimpleNamespace
import unittest
from unittest.mock import patch

import sys
sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "tools"))
import check_screen_jpeg_allocator as check

TEST_OUTPUT = Path(tempfile.gettempdir()) / "rodakos-jpeg-allocator-unittest"
TEST_OUTPUT.mkdir(parents=True, exist_ok=True)


class AuditTests(unittest.TestCase):
    def test_cfg_follows_both_paths_but_never_decodes_jump_padding(self):
        listing = {
            0x5000: "5000: 004136 entry a1, 32\n5003: 000000 bnez a2, 5010 <codec+0x10>\n",
            0x5006: "5006: 000005 call8 2000 <allocator>\n5009: 000046 j 5020 <codec+0x20>\n500c: 000000 ill\n",
            0x5010: "5010: 000081 l32r a8, 3000 <literal>\n5013: 0008e0 callx8 a8\n5016: 000046 j 5020 <codec+0x20>\n",
            0x5020: "5020: f01d retw.n\n",
        }
        edges, unknown = check.codec_cfg("codec", {"address": 0x5000, "size": 0x22},
                                         lambda start, end: listing[start], lambda address: 0x2100)
        self.assertEqual(sorted(edge["target"] for edge in edges), [0x2000, 0x2100])
        self.assertEqual(unknown, [])

    def test_cfg_rejects_unknown_jumps_opcodes_gaps_and_out_of_range_branches(self):
        for listing in ("5000: 000000 jx a8", "5000: 000000 unfamiliar a2, a3", "5000: 00 .byte 00",
                        "5000: 000046 j 4000 <outside>", "5000: 004136 entry a1, 32\n5004: f01d retw.n",
                        "5000: 004136 entry a1, 32\n5003: 000046 j 5001 <overlap>"):
            with self.subTest(listing=listing), self.assertRaises(ValueError):
                check.codec_cfg("codec", {"address": 0x5000, "size": 0x30},
                                lambda start, end: listing, lambda address: 0)

    def elf_fixture(self, path):
        names = b"\0.text\0.xt.prop\0.shstrtab\0.symtab\0.strtab\0"
        data = bytearray(0x300)
        data[:7] = b"\x7fELF\x01\x01\x01"
        struct.pack_into("<HHIIIIIHHHHHH", data, 16, 2, 94, 1, 0x5000, 52, 0x200, 0, 52, 32, 1, 40, 6, 3)
        struct.pack_into("<8I", data, 52, 1, 0x100, 0x5000, 0x5000, 4, 4, 5, 4)
        data[0x100:0x104] = b"\x36\x41\0\0"
        data[0x130:0x130 + len(names)] = names
        data[0x180:0x188] = b"\0codec\0\0"
        struct.pack_into("<IIIBBH", data, 0x1a0, 1, 0x5000, 4, 0x12, 0, 1)
        def section(index, name, kind, flags, address, offset, size, link=0, entsize=0):
            struct.pack_into("<10I", data, 0x200 + index * 40,
                             names.index(name.encode()), kind, flags, address, offset, size, link, 0, 1, entsize)
        section(1, ".text", 1, 6, 0x5000, 0x100, 4)
        section(2, ".xt.prop", 1, 0, 0, 0x110, 12)
        section(3, ".shstrtab", 3, 0, 0, 0x130, len(names))
        section(4, ".symtab", 2, 0, 0, 0x190, 32, 5, 16)
        section(5, ".strtab", 3, 0, 0, 0x180, 8)
        path.write_bytes(data)
        return check.Elf32(path)

    def test_analysis_view_changes_only_metadata_and_rejects_load_or_symbol_drift(self):
        with tempfile.TemporaryDirectory(dir=TEST_OUTPUT) as directory:
            root = Path(directory)
            elf = self.elf_fixture(root / "original.elf")
            view = root / "analysis-only.elf"
            check.make_analysis_view(elf, view)
            data = view.read_bytes()
            self.assertEqual(data.count(b".no.prop"), 1)
            self.assertEqual(sum(a != b for a, b in zip(data, elf.data)), 2)
            for offset in (0x100, 52 + 20, 0x1a0 + 4):
                changed = bytearray(data)
                changed[offset] ^= 1
                view.write_bytes(changed)
                with self.assertRaises(ValueError):
                    check.verify_analysis_view(elf, check.Elf32(view))

    def symbols(self):
        result = {"jpeg_enc_open": {"address": 0x5000}, "heap_caps_calloc": {"address": 0x8000},
                  "heap_caps_aligned_calloc": {"address": 0x8100}}
        for index, name in enumerate(check.ALLOCATORS):
            result[name] = {"address": 0x1000 + index * 0x100}
            result["__wrap_" + name] = {"address": 0x2000 + index * 0x100}
        return result

    def edges(self):
        values = []
        for index, name in enumerate(check.ALLOCATORS):
            values.append({"caller": "jpeg_enc_open", "address": 0x5000 + index * 4,
                           "target": 0x2000 + index * 0x100, "kind": "call8"})
            values.append({"caller": "__wrap_" + name, "address": 0x2000 + index * 0x100 + 4,
                           "target": 0x1000 + index * 0x100, "kind": "call8"})
            values.append({"caller": "__wrap_" + name, "address": 0x2000 + index * 0x100 + 8,
                           "target": 0x8100 if "align" in name else 0x8000, "kind": "callx8"})
        return values

    def expected(self):
        return {"jpeg_enc_open": {name: 1 for name in check.ALLOCATORS}}

    def test_resolves_direct_and_literal_indirect_calls(self):
        text = """00005000 <jpeg_enc_open>:
    5000: 004136 entry a1, 32
    5003: 000081 l32r a8, 3c001000 <literal>
    5006: 0008e0 callx8 a8
    5009: 000005 call8 2100 <__wrap_jpeg_calloc_inner>
"""
        edges, unknown = check.linked_calls(text, lambda address: 0x2000 if address == 0x3c001000 else None)
        self.assertEqual([edge["target"] for edge in edges], [0x2000, 0x2100])
        self.assertEqual(unknown, [])

    def test_iram_literal_pool_label_is_not_a_function(self):
        # Actual 021 final ELF 79ca1660...: .iram0.text begins with data at
        # _iram_text_start; objdump misreads it as this out-of-range l32r.
        text = """40374404 <_iram_text_start>:
40374462: 2c0001 l32r a0, 4033f464 <esp_rom_opiflash_read_raw+0x2f1a90>
42000000 <__wrap_jpeg_calloc>:
42000000: 000005 call8 42100000 <jpeg_calloc>
"""
        functions = {"_iram_text_start": {"type": 0, "address": 0x40374404, "size": 0},
                     "__wrap_jpeg_calloc": {"type": 2, "address": 0x42000000, "size": 3}}
        def invalid_word(address):
            self.fail("attempted to read a literal in a non-function data pool")
        edges, unknown = check.linked_calls(text, invalid_word, functions)
        self.assertEqual(len(edges), 1)
        self.assertEqual(unknown, [])

    def test_real_wrapper_branch_entry_skips_misdecoded_alignment_padding(self):
        # Actual 021 wrapper starts its PSRAM branch at ...ddd after padding
        # at ...ddb; a full linear objdump misses heap_caps_aligned_calloc.
        listing = {
            0x42049dbc: "42049dbc: 004136 entry a1, 32\n42049dbf: e38e70 rur.threadptr a8\n42049dc2: e16891 l32r a9, 42042364 <tls>\n42049dc5: 02ad mov.n a10, a2\n42049dc7: 889a add.n a8, a8, a9\n42049dc9: 000882 l8ui a8, a8, 0\n42049dcc: d8cc bnez.n a8, 42049ddd <scope>\n",
            0x42049dce: "42049dce: 03bd mov.n a11, a3\n42049dd0: e16681 l32r a8, 42042368 <original>\n42049dd3: 0008e0 callx8 a8\n42049dd6: 0a2d mov.n a2, a10\n42049dd8: 000486 j 42049dee <exit>\n42049ddb: 200000 or a0, a0, a0\n",
            0x42049ddd: "42049ddd: 20c220 or a12, a2, a2\n42049de0: 04a4d2 movi a13, 0x404\n42049de3: 01a0b2 movi a11, 1\n42049de6: 20a330 or a10, a3, a3\n42049de9: bb16a5 call8 42004f54 <heap_caps_aligned_calloc>\n42049dec: 0a2d mov.n a2, a10\n42049dee: f01d retw.n\n",
            0x42049dee: "42049dee: f01d retw.n\n",
        }
        edges, unknown = check.codec_cfg("__wrap_jpeg_calloc_align", {"address": 0x42049dbc, "size": 0x34},
            lambda start, end: listing[start], lambda address: {0x42042364: 0x21, 0x42042368: 0x421acf4c}[address],
            check.CODEC_OPCODES | {"rur.threadptr"})
        self.assertEqual(sorted(edge["target"] for edge in edges), [0x42004f54, 0x421acf4c])
        self.assertEqual(unknown, [])

    def test_unreadable_literal_remains_a_rejected_critical_unknown(self):
        text = """00002000 <__wrap_jpeg_calloc>:
2000: 000081 l32r a8, 4033f464 <outside>
2003: 0008e0 callx8 a8
"""
        def missing_word(address):
            raise ValueError("outside readable ELF")
        edges, unknown = check.linked_calls(text, missing_word)
        self.assertEqual(edges, [])
        self.assertEqual(unknown[0]["kind"], "unreadable-literal")
        with self.assertRaisesRegex(ValueError, "unresolved call"):
            check.verify_edges(self.symbols(), self.expected(), self.edges(), unknown)

    def test_clobbered_register_does_not_reuse_stale_literal(self):
        text = """00005000 <jpeg_enc_open>:
    5000: 000081 l32r a8, 3c001000 <literal>
    5003: 080c movi.n a8, 0
    5005: 0008e0 callx8 a8
"""
        edges, unknown = check.linked_calls(text, lambda address: 0x2000)
        self.assertEqual(edges, [])
        self.assertEqual(len(unknown), 1)

    def test_expected_calls_and_passthrough_are_required(self):
        self.assertEqual(check.verify_edges(self.symbols(), self.expected(), self.edges(), []), self.expected())

    def test_retained_codec_unknown_call_is_rejected_even_after_all_expected_calls(self):
        with self.assertRaisesRegex(ValueError, "unresolved call inside retained codec"):
            check.verify_edges(self.symbols(), self.expected(), self.edges(),
                               [{"caller": "jpeg_enc_open", "address": 0x5060}])

    def test_wrapper_symbol_presence_without_call_is_rejected(self):
        with self.assertRaisesRegex(ValueError, "missing/unrecognized wrapped call"):
            check.verify_edges(self.symbols(), self.expected(), self.edges()[1:], [])

    def test_unwrapped_original_call_is_rejected(self):
        edges = self.edges()
        edges[0]["target"] = 0x1000
        with self.assertRaisesRegex(ValueError, "allocator bypass"):
            check.verify_edges(self.symbols(), self.expected(), edges, [])

    def test_missing_wrap_symbol_is_rejected(self):
        symbols = self.symbols()
        del symbols["__wrap_jpeg_calloc_inner"]
        with self.assertRaisesRegex(ValueError, "missing linked"):
            check.verify_edges(symbols, self.expected(), self.edges(), [])

    def test_recursive_or_unresolved_wrapper_is_rejected(self):
        edges = self.edges()
        edges[1]["target"] = 0x2000
        with self.assertRaisesRegex(ValueError, "missing real passthrough"):
            check.verify_edges(self.symbols(), self.expected(), edges, [])
        with self.assertRaisesRegex(ValueError, "unresolved call"):
            check.verify_edges(self.symbols(), self.expected(), self.edges(),
                               [{"caller": "__wrap_jpeg_calloc", "address": 0x2004}])

    def test_expected_counts_reject_partial_interception(self):
        expected = self.expected()
        expected["jpeg_enc_open"]["jpeg_calloc_inner"] = 2
        with self.assertRaisesRegex(ValueError, "expected 2, got 1"):
            check.verify_edges(self.symbols(), expected, self.edges(), [])

    def test_missing_psram_call_or_extra_wrapper_call_is_rejected(self):
        with self.assertRaisesRegex(ValueError, "missing exact PSRAM"):
            check.verify_edges(self.symbols(), self.expected(), self.edges()[:-1], [])
        with self.assertRaisesRegex(ValueError, "unexpected call"):
            check.verify_edges(self.symbols(), self.expected(), self.edges() + [{
                "caller": "__wrap_jpeg_calloc", "address": 0x2010, "target": 0x9000}], [])

    def test_only_gc_removed_functions_may_be_absent(self):
        expected = self.expected()
        expected["jpeg_dec_open"] = {"jpeg_calloc_inner": 1}
        check.verify_edges(self.symbols(), expected, self.edges(), [])
        symbols = self.symbols()
        symbols["jpeg_dec_open"] = {"address": 0x6000}
        with self.assertRaisesRegex(ValueError, "jpeg_dec_open"):
            check.verify_edges(symbols, expected, self.edges(), [])

    def test_relocation_counter_ignores_duplicate_literal_and_debug_references(self):
        value = """RELOCATION RECORDS FOR [.literal.jpeg_enc_open]:
00000030 R_XTENSA_32 jpeg_calloc_inner
RELOCATION RECORDS FOR [.text.jpeg_enc_open]:
0000006b R_XTENSA_ASM_EXPAND jpeg_calloc_inner
RELOCATION RECORDS FOR [.debug_info]:
00000030 R_XTENSA_32 jpeg_calloc_inner
"""
        self.assertEqual(check.relocation_calls(value), {"jpeg_enc_open": {"jpeg_calloc_inner": 1}})

    def test_archive_function_owners_are_unambiguous(self):
        value = """esp_jpeg_memory.c.obj:     file format elf32-xtensa-le
RELOCATION RECORDS FOR [.text.jpeg_calloc_inner]:
00000012 R_XTENSA_ASM_EXPAND heap_caps_calloc_prefer
esp_jpeg_enc.c.obj:     file format elf32-xtensa-le
RELOCATION RECORDS FOR [.text.jpeg_enc_open]:
0000006b R_XTENSA_ASM_EXPAND jpeg_calloc_inner
"""
        self.assertEqual(check.relocation_owners(value), {
            "jpeg_calloc_inner": "esp_jpeg_memory.c.obj", "jpeg_enc_open": "esp_jpeg_enc.c.obj"})
        with self.assertRaisesRegex(ValueError, "ambiguous"):
            check.relocation_owners(value + "RELOCATION RECORDS FOR [.text.jpeg_calloc_inner]:\n")

    def map_fixture(self):
        archive = TEST_OUTPUT / "libesp_new_jpeg.a"
        symbols = self.symbols()
        names = ("jpeg_enc_open", *check.ALLOCATORS)
        owners = {name: "esp_jpeg_memory.c.obj" for name in names}
        owners["jpeg_enc_open"] = "esp_jpeg_enc.c.obj"
        text = ""
        for name in names:
            symbols[name]["size"] = 0x1c
            # GNU ld wraps long input section names onto the next line.
            text += f" .text.{name}\n   0x{symbols[name]['address']:08x} 0x1c {archive.resolve()}({owners[name]})\n"
        return archive, symbols, owners, text

    def test_map_must_match_function_bytes_and_archive_member(self):
        archive, symbols, owners, text = self.map_fixture()
        result = check.verify_map(text, symbols, self.expected(), owners, archive)
        self.assertEqual(len(result), 5)
        mutations = [text.replace("0x00005000", "0x00005004"), text.replace("0x1c", "0x20", 1),
                     text.replace("esp_jpeg_enc.c.obj", "other.obj"), text.replace("libesp_new_jpeg.a", "replacement.a"),
                     text + text]
        for mutation in mutations:
            with self.subTest(mutation=mutation), self.assertRaises(ValueError):
                check.verify_map(mutation, symbols, self.expected(), owners, archive)

    def scope_fixture(self):
        symbols = {
            "g_screen_jpeg_scope_active": {"type": 6, "size": 1, "address": 0, "section": 1},
            "_ZN7rodakos25ScreenJpegAllocationScopeC1Ev": {"type": 2, "address": 0x6000},
            "_ZN7rodakos25ScreenJpegAllocationScopeD1Ev": {"type": 2, "address": 0x6100},
        }
        for name, value in (("data_start", 0x3c000000), ("data_end", 0x3c000000),
                            ("bss_start", 0x3c000000), ("bss_end", 0x3c000001)):
            symbols["_thread_local_" + name] = {"address": value}
        elf = SimpleNamespace(symbols=symbols, sections=[{}, {
            "flags": 0x403, "type": 8, "address": 0x3c000000, "size": 1}])
        edges = [{"caller": "_ZN14DisplayService10EncodeJpegEv", "address": 0x7000, "target": 0x6000},
                 {"caller": "_ZN14DisplayService10EncodeJpegEv", "address": 0x7050, "target": 0x6100}]
        return elf, edges

    def test_native_tls_scope_records_real_calls_and_alignment(self):
        elf, edges = self.scope_fixture()
        self.assertEqual(check.verify_scope(elf, edges)["per_task_aligned_bytes"], 16)

    def test_merged_progbits_tls_requires_zero_flag(self):
        elf, edges = self.scope_fixture()
        elf.sections[1].update(type=1, offset=0)
        elf.data = b"\0"
        check.verify_scope(elf, edges)
        elf.data = b"\1"
        with self.assertRaisesRegex(ValueError, "constant-zero"):
            check.verify_scope(elf, edges)

    def test_native_tls_requires_properties_and_instruction_access(self):
        elf = SimpleNamespace(symbols={"__wrap_jpeg_calloc": {"address": 0x2000}},
                              sections=[{"name": ".xt.prop", "size": 12}])
        disassembly = "00002000 <__wrap_jpeg_calloc>:\n  2000: e38e70 rur.threadptr a8\n"
        self.assertEqual(check.verify_native_tls(elf, disassembly, []), [0x2000])
        with self.assertRaisesRegex(ValueError, "thread-pointer"):
            check.verify_native_tls(elf, disassembly.replace("rur.threadptr", "movi"), [])
        with self.assertRaisesRegex(ValueError, "unresolved TLS"):
            check.verify_native_tls(elf, disassembly, [{"caller": "__wrap_jpeg_calloc"}])
        elf.sections = []
        with self.assertRaisesRegex(ValueError, "instruction properties"):
            check.verify_native_tls(elf, disassembly, [])

    def test_scope_rejects_wrong_caller_missing_lifetime_and_global_flag(self):
        for mutation in ("wrong-caller", "missing-exit", "global-flag", "dynamic-tls", "bad-bounds", "outside-tls", "not-tls-section"):
            with self.subTest(mutation=mutation):
                elf, edges = self.scope_fixture()
                if mutation == "wrong-caller": edges[0]["caller"] = "CameraService"
                elif mutation == "missing-exit": edges.pop()
                elif mutation == "global-flag": elf.symbols["g_screen_jpeg_scope_active"]["type"] = 1
                elif mutation == "dynamic-tls":
                    elf.symbols["__emutls_get_address"] = {"address": 0x8000}
                    elf.symbols["innocent_alias"] = {"address": 0x8000}
                    edges.append({"caller": "__wrap_jpeg_calloc", "address": 0x2004, "target": 0x8000})
                elif mutation == "bad-bounds": elf.symbols["_thread_local_data_end"]["address"] += 16
                elif mutation == "outside-tls": elf.symbols["g_screen_jpeg_scope_active"]["address"] = 8
                elif mutation == "not-tls-section": elf.sections[1]["flags"] = 3
                with self.assertRaises(ValueError):
                    check.verify_scope(elf, edges)

    def test_branch_or_prior_call_cannot_reuse_stale_literal(self):
        for instruction in ("bnez a2, 5010", "call8 1234", "jx a3"):
            text = f"""00005000 <jpeg_enc_open>:
    5000: 000081 l32r a8, 3c001000 <literal>
    5003: 000000 {instruction}
    5006: 0008e0 callx8 a8
"""
            edges, unknown = check.linked_calls(text, lambda address: 0x2000)
            self.assertEqual(len(unknown), 1)
            self.assertFalse(any(edge["target"] == 0x2000 for edge in edges))

    def fixture_identity(self, root):
        component = root / "managed_components/espressif__esp_new_jpeg"
        pins = {}
        for name in check.PINS:
            path = component / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(name.encode())
            pins[name] = check.digest(path)
        (root / "main").mkdir()
        (root / "main/idf_component.yml").write_text('  espressif/esp_new_jpeg: "0.6.1"\n')
        (root / "dependencies.lock").write_text('dependencies:\n  espressif/esp_new_jpeg:\n    component_hash: ' + check.COMPONENT_HASH + '\n    version: 0.6.1\n')
        (root / "sdkconfig").write_text('CONFIG_IDF_TARGET="esp32s3"\nCONFIG_IDF_TARGET_ESP32S3=y\n')
        return component, pins

    def test_identity_mutations_fail_closed(self):
        for mutation in ("target", "idf", "archive", "header", "manifest", "lock", "sdkconfig", "heap-abort"):
            with self.subTest(mutation=mutation), tempfile.TemporaryDirectory(dir=TEST_OUTPUT) as directory:
                root = Path(directory)
                component, pins = self.fixture_identity(root)
                target, idf = "esp32s3", "6.0.2"
                if mutation == "target": target = "esp32p4"
                elif mutation == "idf": idf = "6.0.3"
                elif mutation == "archive": (component / "lib/esp32s3/libesp_new_jpeg.a").write_bytes(b"drift")
                elif mutation == "header": (component / "include/esp_jpeg_common.h").write_bytes(b"drift")
                elif mutation == "manifest": (root / "main/idf_component.yml").write_text('  espressif/esp_new_jpeg: "0.6.2"\n')
                elif mutation == "lock": (root / "dependencies.lock").write_text('dependencies: {}\n')
                elif mutation == "sdkconfig": (root / "sdkconfig").write_text('CONFIG_IDF_TARGET="esp32p4"\n')
                else:
                    with (root / "sdkconfig").open("a") as config:
                        config.write('CONFIG_HEAP_ABORT_WHEN_ALLOCATION_FAILS=y\n')
                with patch.object(check, "PINS", pins), self.assertRaises(ValueError):
                    check.check_identity(root, target, idf)


if __name__ == "__main__":
    unittest.main()

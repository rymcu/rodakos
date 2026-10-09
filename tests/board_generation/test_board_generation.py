"""Exercise board ownership and path normalization without an IDF build."""

import contextlib
import io
import logging
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest

import yaml


REPO_ROOT = Path(__file__).resolve().parents[2]
BMGR_ROOT = REPO_ROOT / 'components' / 'esp_board_manager'
BOARD_COMPONENT = 'rodakos_hal_boards'
sys.path.insert(0, str(BMGR_ROOT))

from gen_bmgr_config_codes import BoardConfigGenerator
from generators.utils.logger import get_global_log_level, set_global_log_level


class BoardGenerationTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix='rodak board generation ')
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        self.component = self.root / 'components' / BOARD_COMPONENT
        shutil.copytree(REPO_ROOT / 'components' / BOARD_COMPONENT, self.component)
        self.board = self.component / 'boards' / 'rymcu' / 'rymcu_bigsmart'
        self.generated = self.root / 'components' / 'gen_bmgr_codes'
        resources = self.root / 'board manager resources'
        resources.mkdir()
        # 构造器会创建 gen_codes；使用临时资源根，避免触碰仓库内的生成目录。
        self.addCleanup(set_global_log_level, get_global_log_level())
        set_global_log_level(logging.WARNING)
        with contextlib.redirect_stdout(io.StringIO()):
            self.generator = BoardConfigGenerator(resources, project_dir=str(self.root))

    def generate(self, board=None, selected_board='rymcu_bigsmart'):
        board = board or self.board
        dependencies = {}
        device_file = board / 'board_devices.yaml'
        if device_file.is_file():
            devices = yaml.safe_load(device_file.read_text(encoding='utf-8'))['devices']
            for device in devices:
                dependencies.update(device.get('dependencies', {}))
        self.assertTrue(self.generator.setup_gen_bmgr_codes_component(
            str(self.root), str(board), dependencies, selected_board
        ))

    def evaluate_sources(self, include_board=True):
        cmake = shutil.which('cmake')
        if cmake is None:
            self.skipTest('需要 cmake 以脚本模式读取真实组件注册；不执行构建')
        harness = self.root / 'inspect registration.cmake'
        report = self.root / 'registration.txt'
        # 只替换 IDF 的注册入口；变量展开、引号及 include 仍由真实 CMake 解释。
        harness.write_text(r'''
cmake_minimum_required(VERSION 3.16)
file(WRITE "${REPORT}" "")
set(CMAKE_SOURCE_DIR "${PROJECT_ROOT}")
function(idf_component_register)
    cmake_parse_arguments(ARG "" "" "SRCS;SRC_DIRS;INCLUDE_DIRS;REQUIRES" ${ARGN})
    if(ARG_SRCS)
        foreach(source IN LISTS ARG_SRCS)
            get_filename_component(source "${source}" ABSOLUTE BASE_DIR "${COMPONENT_DIR}")
            file(APPEND "${REPORT}" "source|${COMPONENT_NAME}|${source}\n")
        endforeach()
    else()
        foreach(directory IN LISTS ARG_SRC_DIRS)
            get_filename_component(directory "${directory}" ABSOLUTE BASE_DIR "${COMPONENT_DIR}")
            file(GLOB sources "${directory}/*.c" "${directory}/*.cpp" "${directory}/*.cc" "${directory}/*.S")
            foreach(source IN LISTS sources)
                file(APPEND "${REPORT}" "source|${COMPONENT_NAME}|${source}\n")
            endforeach()
        endforeach()
    endif()
    foreach(dependency IN LISTS ARG_REQUIRES)
        file(APPEND "${REPORT}" "requires|${COMPONENT_NAME}|${dependency}\n")
    endforeach()
endfunction()
function(idf_component_set_property)
endfunction()
if(INCLUDE_BOARD)
    set(COMPONENT_NAME rodakos_hal_boards)
    set(COMPONENT_DIR "${PROJECT_ROOT}/components/${COMPONENT_NAME}")
    include("${COMPONENT_DIR}/CMakeLists.txt")
endif()
set(COMPONENT_NAME gen_bmgr_codes)
set(COMPONENT_DIR "${PROJECT_ROOT}/components/${COMPONENT_NAME}")
include("${COMPONENT_DIR}/CMakeLists.txt")
''', encoding='utf-8')
        result = subprocess.run([
            cmake, f'-DPROJECT_ROOT={self.root.as_posix()}',
            f'-DREPORT={report.as_posix()}', f'-DINCLUDE_BOARD={"ON" if include_board else "OFF"}',
            '-P', str(harness)
        ], cwd=self.root, capture_output=True, text=True, encoding='utf-8', timeout=30)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        return [line.split('|', 2) for line in report.read_text(encoding='utf-8').splitlines()]

    def normalize_paths(self):
        pwsh = shutil.which('pwsh')
        if pwsh is None:
            self.skipTest('路径集成测试需要 PowerShell 7 (pwsh)')
        script = self.root / 'fix_gen_paths.ps1'
        shutil.copyfile(REPO_ROOT / 'fix_gen_paths.ps1', script)
        result = subprocess.run([
            pwsh, '-NoLogo', '-NoProfile', '-NonInteractive', '-File', str(script)
        ], cwd=self.root, capture_output=True, text=True, encoding='utf-8', timeout=30)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def portable_snapshot(self):
        return {name: (self.generated / name).read_bytes()
                for name in ('CMakeLists.txt', 'idf_component.yml')}

    def test_bigsmart_setup_has_one_source_owner_and_generated_tables_remain(self):
        self.generate()
        table = self.generated / 'gen_board_device_config.c'
        table.write_text('const int fixture_generated_table = 1;\n', encoding='utf-8')
        registrations = self.evaluate_sources()
        setup = (self.board / 'setup_device.c').resolve()
        self.assertEqual([
            owner for kind, owner, value in registrations
            if kind == 'source' and Path(value).resolve() == setup
        ], [BOARD_COMPONENT])
        self.assertEqual([
            owner for kind, owner, value in registrations
            if kind == 'source' and Path(value).resolve() == table.resolve()
        ], ['gen_bmgr_codes'])
        self.assertIn(['requires', 'gen_bmgr_codes', BOARD_COMPONENT], registrations)
        self.assertIn(['requires', 'gen_bmgr_codes', 'esp_board_manager'], registrations)
        for kind, _, value in registrations:
            if kind == 'source':
                self.assertTrue(Path(value).is_file(), value)
            self.assertNotIn('brookesia_hal_boards', value)

    def test_repeated_generation_is_identical(self):
        self.generate()
        first = self.portable_snapshot()
        self.generate()
        self.assertEqual(first, self.portable_snapshot())

    def test_external_board_source_is_still_registered(self):
        board = self.root / 'external boards' / 'fixture_board'
        board.mkdir(parents=True)
        source = board / 'setup_fixture.c'
        source.write_text('void fixture_board_setup(void) {}\n', encoding='utf-8')
        self.generate(board, 'fixture_board')
        registrations = self.evaluate_sources(include_board=False)
        self.assertEqual([
            owner for kind, owner, value in registrations
            if kind == 'source' and Path(value).resolve() == source.resolve()
        ], ['gen_bmgr_codes'])

    def test_generated_paths_resolve_after_normalization_and_remain_idempotent(self):
        self.generate()
        self.normalize_paths()
        first = self.portable_snapshot()
        for contents in first.values():
            text = contents.decode('utf-8-sig')
            self.assertNotIn(str(self.root), text)
            self.assertNotIn(self.root.as_posix(), text)
            self.assertNotIn('brookesia_hal_boards', text)
        manifest = yaml.safe_load(first['idf_component.yml'].decode('utf-8-sig'))
        dependency = manifest['dependencies']['esp_io_expander_pca9557']['override_path']
        self.assertFalse(Path(dependency).is_absolute())
        resolved = (self.generated / dependency.replace('\\', '/')).resolve()
        self.assertEqual(resolved, (self.board / 'components' / 'esp_io_expander_pca9557').resolve())
        self.assertTrue((resolved / 'idf_component.yml').is_file())
        self.evaluate_sources()
        self.normalize_paths()
        self.assertEqual(first, self.portable_snapshot())
        self.generate()
        self.normalize_paths()
        self.assertEqual(first, self.portable_snapshot())


if __name__ == '__main__':
    unittest.main()

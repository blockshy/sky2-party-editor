"""在临时假游戏目录验证安装边界；不读取、不写入真正的游戏或玩家存档。"""

import hashlib
import json
import os
from pathlib import Path
import shutil
import struct
import subprocess
import tempfile
import unittest
import zipfile


PROJECT = Path(__file__).resolve().parents[2]
POWERSHELL = os.environ.get("PARTY_POWERSHELL") or shutil.which("pwsh") or ""
SUPPORTED_EXE = "d8b2911d1576216bdc22d070550e4f531e105de7ed2981885849669f4acf8aaf"


def synthetic_pe(exports):
    """生成只有结构和导出名字的不可执行测试PE，不复制任何游戏或第三方二进制。"""
    binary = bytearray(4096)
    binary[:2] = b"MZ"
    struct.pack_into("<I", binary, 0x3C, 0x80)
    struct.pack_into("<I", binary, 0x80, 0x4550)
    struct.pack_into("<HH", binary, 0x84, 0x8664, 1)
    struct.pack_into("<HH", binary, 0x94, 0xF0, 0x2000)
    struct.pack_into("<H", binary, 0x98, 0x20B)
    struct.pack_into("<I", binary, 0x98 + 108, 16)
    struct.pack_into("<II", binary, 0x98 + 112, 0x1000, 0x400)
    section = 0x98 + 0xF0
    binary[section:section + 8] = b".edata\0\0"
    struct.pack_into("<IIII", binary, section + 8, 0xE00, 0x1000, 0xE00, 0x200)
    # 导出目录和名字指针表的RVA均位于该单一节中；没有机器码，测试绝不加载它。
    struct.pack_into("<I", binary, 0x200 + 24, len(exports))
    struct.pack_into("<I", binary, 0x200 + 32, 0x1040)
    cursor = 0x300
    for index, name in enumerate(exports):
        struct.pack_into("<I", binary, 0x240 + index * 4, cursor - 0x200 + 0x1000)
        encoded = name.encode("ascii") + b"\0"
        binary[cursor:cursor + len(encoded)] = encoded
        cursor += len(encoded)
    return bytes(binary)

# 仅把假游戏的 exe 校验替换为期望值；其余载荷、归属和路径检查使用生产脚本。
# 运行状态也由测试明确指定，测试机器是否启动游戏不会影响隔离结果。
RUNNER = r"""
param([string]$FixtureFile)
$ErrorActionPreference = 'Stop'
$global:partyTestFixture = Get-Content -Raw -LiteralPath $FixtureFile | ConvertFrom-Json
function Get-FileHash {
    param([string]$LiteralPath, [string]$Algorithm)
    $result = Microsoft.PowerShell.Utility\Get-FileHash -LiteralPath $LiteralPath -Algorithm $Algorithm
    if ([IO.Path]::GetFullPath($LiteralPath) -eq (Join-Path $global:partyTestFixture.game 'sora_2nd.exe')) {
        $result.Hash = $global:partyTestFixture.exe_hash
    }
    return $result
}
function Get-Process {
    param([string]$Name, [string]$ErrorAction)
    if ($global:partyTestFixture.running) { return [PSCustomObject]@{ Id=12345; Name='sora_2nd' } }
}
& (Join-Path $global:partyTestFixture.package ('tools/' + $global:partyTestFixture.action + '-Mod.ps1')) `
    -GamePath $global:partyTestFixture.game -PackageRoot $global:partyTestFixture.package -WhatIf:$global:partyTestFixture.what_if
"""


def invoke_ps(*arguments):
    """使用隐藏的非交互 PowerShell，不启动游戏、不显示额外终端窗口。"""
    result = subprocess.run(
        [POWERSHELL, "-NoProfile", "-NonInteractive", *map(str, arguments)],
        capture_output=True, timeout=45,
        creationflags=getattr(subprocess, "CREATE_NO_WINDOW", 0),
    )
    # PowerShell 的控制台编码可能继承本机中文代码页；保留原始进程结果再解码。
    for name in ("stdout", "stderr"):
        value = getattr(result, name)
        try:
            decoded = value.decode("utf-8")
        except UnicodeDecodeError:
            decoded = value.decode("mbcs", errors="replace")
        setattr(result, name, decoded)
    return result


@unittest.skipUnless(os.name == "nt" and POWERSHELL and Path(POWERSHELL).is_file(), "需要 Windows 与 PATH中的pwsh或PARTY_POWERSHELL")
class InstallationBoundaryTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        # 所有路径均建立在自行分配的临时根目录中；从未使用真实游戏路径。
        cls.temporary = tempfile.TemporaryDirectory(prefix="sky2-party-installer-tests-")
        cls.root = Path(cls.temporary.name).resolve()
        cls.asi = cls.root / "fixture.asi"
        cls.asi.write_bytes(synthetic_pe(["InitializeASI"]))
        cls.dll = cls.root / "fixture.dll"
        cls.dll.write_bytes(synthetic_pe([
            "XInputGetState", "XInputSetState", "XInputGetCapabilities", "XInputEnable",
            "XInputGetBatteryInformation", "XInputGetKeystroke", "XInputGetAudioDeviceIds",
        ]))
        cls.output = cls.root / "output"
        result = invoke_ps("-File", PROJECT / "tools/Package-Mod.ps1", "-BinaryPath", cls.asi,
                           "-Distribution", "ASI", "-CompanionBinaryPath", cls.dll,
                           "-OutputDirectory", cls.output)
        if result.returncode:
            raise RuntimeError(result.stdout + result.stderr)
        cls.package = cls.root / "package"
        cls.archive = next(cls.output.glob("*-ASI.zip"))
        with zipfile.ZipFile(cls.archive) as archive:
            archive.extractall(cls.package)
        result = invoke_ps("-File", PROJECT / "tools/Package-Mod.ps1", "-BinaryPath", cls.dll,
                           "-Distribution", "Standalone", "-CompanionBinaryPath", cls.asi,
                           "-OutputDirectory", cls.output)
        if result.returncode:
            raise RuntimeError(result.stdout + result.stderr)
        cls.standalone_package = cls.root / "standalone-package"
        cls.standalone_archive = next(cls.output.glob("*-Standalone.zip"))
        with zipfile.ZipFile(cls.standalone_archive) as archive:
            archive.extractall(cls.standalone_package)
        cls.runner = cls.root / "fixture-runner.ps1"
        cls.runner.write_text(RUNNER, encoding="utf-8-sig")

    @classmethod
    def tearDownClass(cls):
        cls.temporary.cleanup()

    def setUp(self):
        self.case = Path(tempfile.mkdtemp(prefix="case-", dir=self.root)).resolve()
        self.assertTrue(self.case.is_relative_to(self.root))
        self.game = self.case / "game"
        self.game.mkdir()
        (self.game / "sora_2nd.exe").write_bytes(b"Fake executable used only by isolated tests.")
        (self.game / "xinput1_4.dll").write_bytes(b"Existing public loader must remain unchanged.")
        self.fixture = dict(game=str(self.game), package=str(self.package), action="Install",
                            what_if=False, running=False, exe_hash=SUPPORTED_EXE)

    def run_action(self, action="Install", success=True, **overrides):
        values = {**self.fixture, "action": action, **overrides}
        # ASI脚本对根XInput一律只读，兼容未知版本的公共Loader且不得替换它。
        preserve_root = (Path(values["package"]) / "dist/plugins/Sky2PartyEditor.asi").is_file()
        root_dll = self.game / "xinput1_4.dll"
        root_before = root_dll.read_bytes() if root_dll.is_file() else None
        settings = self.case / "fixture.json"
        settings.write_text(json.dumps(values), encoding="utf-8")
        result = invoke_ps("-File", self.runner, "-FixtureFile", settings)
        message = result.stdout + result.stderr
        if success:
            self.assertEqual(result.returncode, 0, message)
        else:
            self.assertNotEqual(result.returncode, 0, message)
        if preserve_root:
            self.assertEqual(root_dll.read_bytes() if root_dll.is_file() else None, root_before)
        return message

    def manual_install(self, package=None):
        shutil.copytree((package or self.package) / "dist", self.game, dirs_exist_ok=True)

    def make_upgrade_packages(self, old_version="0.1.0", new_version="0.2.0", source_package=None):
        """构造内容不同的两个可信测试包，只向新包加入旧包的真实载荷哈希。

        不依赖真实游戏或已发布二进制；历史文件必须先被包内白名单认可，不能靠
        游戏目录里的收据授权。两个许可也使用不同内容，确保升级覆盖完整载荷。
        """
        old_package = self.case / "old-package"
        new_package = self.case / "new-package"
        for directory, version in ((old_package, old_version), (new_package, new_version)):
            shutil.copytree(source_package or self.package, directory)
            manifest_path = directory / "tools/manifest.json"
            manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
            manifest["version"] = version
            for entry in manifest["files"]:
                payload = directory / "dist" / entry["path"]
                payload.write_bytes(payload.read_bytes() + f"\nFixture version {version}\n".encode("ascii"))
                entry["sha256"] = hashlib.sha256(payload.read_bytes()).hexdigest()
                entry["known_sha256"] = []
            manifest_path.write_text(json.dumps(manifest), encoding="utf-8")
        previous = json.loads((old_package / "tools/manifest.json").read_text(encoding="utf-8"))
        known = {entry["path"]: entry["sha256"] for entry in previous["files"]}
        manifest_path = new_package / "tools/manifest.json"
        successor = json.loads(manifest_path.read_text(encoding="utf-8"))
        for entry in successor["files"]:
            entry["known_sha256"] = [known[entry["path"]]]
        manifest_path.write_text(json.dumps(successor), encoding="utf-8")
        return old_package, new_package

    def create_user_files(self):
        """创建用户配置、诊断日志和其他 Mod，逐步核对升级/卸载没有改动这些内容。"""
        expected = {
            "plugins/Sky2PartyEditor/settings.ini": b"[Party]\r\nEnabled=0\r\nUnlockFixedMembers=0\r\nAnywhereFormation=1\r\nUnlockUnavailableMembers=0\r\nAllowUnjoinedMembers=1\r\n",
            "plugins/Sky2PartyEditor/party.log": b"User diagnostic history\r\n",
            "plugins/OtherMod.asi": b"Other independent ASI plugin",
        }
        for relative, content in expected.items():
            path = self.game / relative
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(content)
        return expected

    def assert_user_files(self, expected):
        for relative, content in expected.items():
            self.assertEqual((self.game / relative).read_bytes(), content, relative)

    def assert_installed_package(self, package):
        manifest = json.loads((package / "tools/manifest.json").read_text(encoding="utf-8"))
        for relative in (entry["path"] for entry in manifest["files"]):
            self.assertEqual((self.game / relative).read_bytes(), (package / "dist" / relative).read_bytes(), relative)

    def test_package_contains_only_declared_player_files(self):
        expected = {
            "README.md", "CHANGELOG.md", "LICENSE", "THIRD_PARTY_NOTICES.md",
            "docs/INSTALLATION.md", "docs/USAGE.md", "docs/TESTING.md", "docs/HUB_MODULE.md", "tools/Common.ps1",
            "tools/Install-Mod.ps1", "tools/Uninstall-Mod.ps1", "tools/manifest.json",
            "dist/plugins/Sky2PartyEditor.asi", "dist/plugins/Sky2PartyEditor/LICENSES.txt",
        }
        actual = {p.relative_to(self.package).as_posix() for p in self.package.rglob("*") if p.is_file()}
        self.assertEqual(actual, expected)
        recorded = Path(str(self.archive) + ".sha256").read_text(encoding="utf-8").split()[0]
        self.assertEqual(recorded, hashlib.sha256(self.archive.read_bytes()).hexdigest())

    def test_package_records_verified_historical_fingerprints(self):
        # 0.1.0、0.2.0 和 0.2.1 指纹均通过只读计算实际安装文件核实；相同许可指纹只保留一次。
        # 自动化仅验证发行包同时携带所有历史指纹，不访问玩家安装目录。
        expected = {
            "plugins/Sky2PartyEditor.asi": [
                "a2f40e5dc4d8831cb8fb04eef9919639279069693fb892ef8b8ec29a1433bfc2",
                "890a63237ea1eeb54623b2b50e764b5341976af44f8e167c5259542b7daf6681",
                "392bbd0bead0f2a93c3c5ba63d398b4a5e0cb96dd4287bd30424250275d75681",
                "8eb1ee10f9649e0c561ef6d6bea6ef97ba6745fdf6f15c025a31f7357eb393cf",
                "dfdfbb64efaf0569dde8752a7e54dc78aa7a99220b67475b43f53af593382348",
                "cdf490b747f1d8037c896b8c26c3d3ed2dde664efbeb2ee4812c2bdcefced989",
                "2adcd6b11194b4ee15e1bd14918b0323fca72529f3add54f2f00b52e58909ffa",
                "a50e87980a19fa72f2e214d58251b71d8802eb85a6baab245268ec1c88d7de1f",
            ],
            "plugins/Sky2PartyEditor/LICENSES.txt": [
                "5fb71140635e1232e9214d38c910111b23fa5c4d6267390c6038b2c61afa244a",
                "5dc96665e0a320adb75ced999877cf5f5d2d57c9d217aa9836d2b899b5fe5d82",
                "40af3ff7e294d7170c902deb49b1afb4689f718deed8b84b05003a137c9da9fe",
            ],
        }
        manifest = json.loads((self.package / "tools/manifest.json").read_text(encoding="utf-8"))
        for entry in manifest["files"]:
            known = [value.lower() for value in entry["known_sha256"]]
            for historical_hash in expected[entry["path"]]:
                self.assertIn(historical_hash, known)

    def test_package_preserves_complete_minhook_and_hde_license(self):
        # 检查完整上游许可而非只查标题，防止精简安装包时遗漏 HDE32/HDE64 的条款。
        original = (PROJECT / "licenses/MinHook.txt").read_text(encoding="utf-8-sig")
        combined = (self.package / "dist/plugins/Sky2PartyEditor/LICENSES.txt").read_text(encoding="utf-8")
        self.assertIn(original, combined)
        self.assertIn("Hacker Disassembler Engine 32 C", original)
        self.assertIn("Hacker Disassembler Engine 64 C", original)

    def test_package_preserves_complete_imgui_license(self):
        # 控制面板新增静态依赖必须带完整许可；字体仍从用户 Windows 中加载，不打包分发。
        original = (PROJECT / "licenses/ImGui.txt").read_text(encoding="utf-8-sig")
        combined = (self.package / "dist/plugins/Sky2PartyEditor/LICENSES.txt").read_text(encoding="utf-8")
        self.assertIn(original, combined)
        self.assertIn("Permission is hereby granted", original)

    def test_script_upgrade_then_uninstall_preserves_user_files(self):
        old_package, new_package = self.make_upgrade_packages()
        self.run_action(package=str(old_package))
        expected = self.create_user_files()
        self.run_action(package=str(new_package))
        self.assert_installed_package(new_package)
        self.assert_user_files(expected)
        self.run_action("Uninstall", package=str(new_package))
        self.assertFalse((self.game / "plugins/Sky2PartyEditor.asi").exists())
        self.assertFalse((self.game / "plugins/Sky2PartyEditor/LICENSES.txt").exists())
        self.assert_user_files(expected)

    def test_020_manual_install_upgrade_to_021_preserves_user_files(self):
        # 当前已装版本为 0.2.0；复核新包脚本可接管原样手动安装并保留现有禁用配置。
        old_package, new_package = self.make_upgrade_packages("0.2.0", "0.2.1")
        self.manual_install(old_package)
        expected = self.create_user_files()
        self.run_action(package=str(new_package))
        self.assert_installed_package(new_package)
        self.assert_user_files(expected)
        self.run_action("Uninstall", package=str(new_package))
        self.assertFalse((self.game / "plugins/Sky2PartyEditor.asi").exists())
        self.assertFalse((self.game / "plugins/Sky2PartyEditor/LICENSES.txt").exists())
        self.assert_user_files(expected)

    def test_manual_old_install_script_upgrade_partial_manual_uninstall(self):
        old_package, new_package = self.make_upgrade_packages()
        self.manual_install(old_package)
        expected = self.create_user_files()
        self.run_action(package=str(new_package))
        self.assert_installed_package(new_package)
        self.assert_user_files(expected)
        # 模拟用户先手动删除 ASI，再使用新包脚本清理残留许可；只删除临时测试文件。
        (self.game / "plugins/Sky2PartyEditor.asi").unlink()
        self.run_action("Uninstall", package=str(new_package))
        self.assertFalse((self.game / "plugins/Sky2PartyEditor/LICENSES.txt").exists())
        self.assert_user_files(expected)

    def test_new_uninstaller_recognizes_manual_old_install(self):
        old_package, new_package = self.make_upgrade_packages()
        self.manual_install(old_package)
        expected = self.create_user_files()
        self.run_action("Uninstall", package=str(new_package))
        self.assertFalse((self.game / "plugins/Sky2PartyEditor.asi").exists())
        self.assertFalse((self.game / "plugins/Sky2PartyEditor/LICENSES.txt").exists())
        self.assert_user_files(expected)

    def test_old_uninstaller_rejects_new_version_without_partial_deletion(self):
        old_package, new_package = self.make_upgrade_packages()
        self.manual_install(new_package)
        expected = self.create_user_files()
        self.run_action("Uninstall", success=False, package=str(old_package))
        self.assert_installed_package(new_package)
        self.assert_user_files(expected)

    def test_modified_old_binary_blocks_upgrade_without_changing_license(self):
        old_package, new_package = self.make_upgrade_packages()
        self.manual_install(old_package)
        expected = self.create_user_files()
        binary = self.game / "plugins/Sky2PartyEditor.asi"
        changed = binary.read_bytes() + b"Unknown local changes"
        binary.write_bytes(changed)
        self.run_action(success=False, package=str(new_package))
        self.assertEqual(binary.read_bytes(), changed)
        self.assertEqual((self.game / "plugins/Sky2PartyEditor/LICENSES.txt").read_bytes(),
                         (old_package / "dist/plugins/Sky2PartyEditor/LICENSES.txt").read_bytes())
        self.assert_user_files(expected)

    def test_install_is_idempotent_and_preserves_other_mod(self):
        self.run_action()
        other = self.game / "plugins/OtherMod.asi"
        other.write_bytes(b"Other plugin")
        self.run_action()
        self.assertEqual(other.read_bytes(), b"Other plugin")
        self.assertEqual((self.game / "plugins/Sky2PartyEditor.asi").read_bytes(), self.asi.read_bytes())

    def test_manual_install_then_script_uninstall_preserves_user_files(self):
        self.manual_install()
        runtime = self.game / "plugins/Sky2PartyEditor"
        (runtime / "settings.ini").write_bytes(b"Enabled=1")
        (runtime / "runtime.log").write_bytes(b"User log")
        self.run_action("Uninstall")
        self.assertFalse((self.game / "plugins/Sky2PartyEditor.asi").exists())
        self.assertFalse((runtime / "LICENSES.txt").exists())
        self.assertEqual((runtime / "settings.ini").read_bytes(), b"Enabled=1")
        self.assertEqual((runtime / "runtime.log").read_bytes(), b"User log")

    def test_unknown_asi_blocks_entire_install_and_uninstall(self):
        self.manual_install()
        target = self.game / "plugins/Sky2PartyEditor.asi"
        target.write_bytes(b"Unknown plugin")
        for action in ("Install", "Uninstall"):
            self.run_action(action, success=False)
            self.assertEqual(target.read_bytes(), b"Unknown plugin")
            self.assertTrue((self.game / "plugins/Sky2PartyEditor/LICENSES.txt").exists())

    def test_unknown_license_prevents_partial_install(self):
        directory = self.game / "plugins/Sky2PartyEditor"
        directory.mkdir(parents=True)
        (directory / "LICENSES.txt").write_bytes(b"Unknown license")
        self.run_action(success=False)
        self.assertFalse((self.game / "plugins/Sky2PartyEditor.asi").exists())
        self.assertEqual((directory / "LICENSES.txt").read_bytes(), b"Unknown license")

    def test_whatif_does_not_create_payload(self):
        self.run_action(what_if=True)
        self.assertFalse((self.game / "plugins").exists())

    def test_game_running_blocks_mutation(self):
        self.run_action(success=False, running=True)
        self.assertFalse((self.game / "plugins").exists())

    def test_wrong_game_build_blocks_install_but_allows_known_uninstall(self):
        self.run_action(success=False, exe_hash="0" * 64)
        self.manual_install()
        self.run_action("Uninstall", exe_hash="0" * 64)
        self.assertFalse((self.game / "plugins/Sky2PartyEditor.asi").exists())

    def test_modified_package_payload_rejected(self):
        modified = self.case / "modified-package"
        shutil.copytree(self.package, modified)
        (modified / "dist/plugins/Sky2PartyEditor.asi").write_bytes(b"Modified payload")
        self.run_action(success=False, package=str(modified))
        self.assertFalse((self.game / "plugins").exists())

    def test_reparse_directory_is_rejected(self):
        outside = self.case / "other-directory"
        outside.mkdir()
        # 只在测试根目录内创建联接；生产脚本必须拒绝经其写入的操作。
        junction_script = self.case / "junction.ps1"
        junction_script.write_text(
            "param([string]$Link,[string]$Target)\nNew-Item -ItemType Junction -Path $Link -Target $Target | Out-Null\n",
            encoding="utf-8-sig",
        )
        result = invoke_ps("-File", junction_script, "-Link", self.game / "plugins", "-Target", outside)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.run_action(success=False)
        self.assertEqual(list(outside.iterdir()), [])

    def test_forged_game_receipt_does_not_authorize_unknown_file(self):
        self.manual_install()
        target = self.game / "plugins/Sky2PartyEditor.asi"
        target.write_bytes(b"Unrelated binary")
        (self.game / "plugins/Sky2PartyEditor/install.json").write_text(
            json.dumps({"product": "Sky2PartyEditor", "sha256": hashlib.sha256(target.read_bytes()).hexdigest()}),
            encoding="utf-8",
        )
        self.run_action("Uninstall", success=False)
        self.assertEqual(target.read_bytes(), b"Unrelated binary")

    def test_standalone_package_has_only_own_two_payload_files(self):
        payload = self.standalone_package / "dist"
        self.assertEqual({p.relative_to(payload).as_posix() for p in payload.rglob("*") if p.is_file()},
                         {"xinput1_4.dll", "Sky2PartyEditor/LICENSES.txt"})
        self.assertEqual((payload / "xinput1_4.dll").read_bytes(), self.dll.read_bytes())
        self.assertFalse((self.standalone_package / "dist/plugins").exists())
        self.assertNotIn("local", self.standalone_archive.name)
        recorded = Path(str(self.standalone_archive) + ".sha256").read_text(encoding="utf-8").split()[0]
        self.assertEqual(recorded, hashlib.sha256(self.standalone_archive.read_bytes()).hexdigest())

    def test_each_package_declares_other_entry_without_shipping_it(self):
        for package, expected_type, other_path, binary in (
            (self.package, "asi-plugin", "xinput1_4.dll", self.dll),
            (self.standalone_package, "standalone-proxy", "plugins/Sky2PartyEditor.asi", self.asi),
        ):
            with self.subTest(distribution=expected_type):
                manifest = json.loads((package / "tools/manifest.json").read_text(encoding="utf-8"))
                self.assertEqual((manifest["schema"], manifest["type"]), (2, expected_type))
                self.assertEqual(len(manifest["files"]), 2)
                self.assertEqual(manifest["conflicts"][0]["path"], other_path)
                self.assertIn(hashlib.sha256(binary.read_bytes()).hexdigest(),
                              manifest["conflicts"][0]["known_sha256"])
                self.assertFalse((package / "dist" / other_path).exists())

    def test_packager_rejects_reversed_binary_distribution(self):
        for distribution, binary, companion in (
            ("Standalone", self.asi, self.asi), ("ASI", self.dll, self.dll),
            ("ASI", self.asi, self.asi), ("Standalone", self.dll, self.dll),
        ):
            with self.subTest(distribution=distribution, binary=binary.name, companion=companion.name):
                output = self.case / "bad-output"
                result = invoke_ps("-File", PROJECT / "tools/Package-Mod.ps1", "-BinaryPath", binary,
                                   "-Distribution", distribution, "-CompanionBinaryPath", companion,
                                   "-OutputDirectory", output)
                self.assertNotEqual(result.returncode, 0)
                self.assertFalse(output.exists())

    def test_packager_rejects_malformed_pe_before_creating_output(self):
        for kind in ("machine", "section", "names", "rva"):
            with self.subTest(kind=kind):
                binary = bytearray(self.asi.read_bytes())
                if kind == "machine":
                    struct.pack_into("<H", binary, 0x84, 0x14C)
                elif kind == "section":
                    struct.pack_into("<H", binary, 0x86, 65535)
                elif kind == "names":
                    struct.pack_into("<I", binary, 0x200 + 24, 65537)
                else:
                    struct.pack_into("<I", binary, 0x200 + 32, 0xFFFFFFF0)
                source = self.case / f"{kind}.asi"
                source.write_bytes(binary)
                output = self.case / f"bad-{kind}"
                result = invoke_ps("-File", PROJECT / "tools/Package-Mod.ps1", "-BinaryPath", source,
                                   "-Distribution", "ASI", "-CompanionBinaryPath", self.dll,
                                   "-OutputDirectory", output)
                self.assertNotEqual(result.returncode, 0)
                self.assertFalse(output.exists())

    def test_standalone_never_replaces_public_loader_or_other_mod(self):
        dll = self.game / "xinput1_4.dll"
        for content in (b"Unknown public Ultimate ASI Loader", b"Other chest standalone Mod"):
            with self.subTest(content=content):
                dll.write_bytes(content)
                for action in ("Install", "Uninstall"):
                    self.run_action(action, success=False, package=str(self.standalone_package))
                    self.assertEqual(dll.read_bytes(), content)
                    self.assertFalse((self.game / "Sky2PartyEditor").exists())

    def test_standalone_manual_install_script_uninstall_is_narrow(self):
        (self.game / "xinput1_4.dll").unlink()
        self.manual_install(self.standalone_package)
        runtime = self.game / "Sky2PartyEditor"
        (runtime / "settings.ini").write_bytes(b"Player preference")
        (runtime / "party.log").write_bytes(b"Diagnostic history")
        nested = runtime / "user-added-folder"
        nested.mkdir()
        (nested / "keep.txt").write_bytes(b"Unknown personal file")
        other = self.create_user_files()
        self.run_action("Uninstall", package=str(self.standalone_package))
        self.assertFalse((self.game / "xinput1_4.dll").exists())
        self.assertFalse((runtime / "LICENSES.txt").exists())
        self.assertEqual((runtime / "settings.ini").read_bytes(), b"Player preference")
        self.assertEqual((runtime / "party.log").read_bytes(), b"Diagnostic history")
        self.assertEqual((nested / "keep.txt").read_bytes(), b"Unknown personal file")
        self.assert_user_files(other)

    def test_standalone_script_upgrade_recognizes_manual_history(self):
        (self.game / "xinput1_4.dll").unlink()
        old_package, new_package = self.make_upgrade_packages("0.5.0", "0.5.1", self.standalone_package)
        self.manual_install(old_package)
        runtime = self.game / "Sky2PartyEditor"
        (runtime / "settings.ini").write_bytes(b"Original standalone preference")
        self.run_action(package=str(new_package))
        self.assert_installed_package(new_package)
        self.run_action(package=str(new_package))
        self.assert_installed_package(new_package)
        self.run_action("Uninstall", package=str(new_package))
        self.assertFalse((self.game / "xinput1_4.dll").exists())
        self.assertEqual((runtime / "settings.ini").read_bytes(), b"Original standalone preference")

    def test_unknown_license_blocks_entire_uninstall_for_both_distributions(self):
        for package, license_path, binary_path in (
            (self.package, "plugins/Sky2PartyEditor/LICENSES.txt", "plugins/Sky2PartyEditor.asi"),
            (self.standalone_package, "Sky2PartyEditor/LICENSES.txt", "xinput1_4.dll"),
        ):
            with self.subTest(package=package.name):
                self.manual_install(package)
                license_file = self.game / license_path
                binary_before = (self.game / binary_path).read_bytes()
                license_file.write_bytes(b"Unrecognized local document")
                self.run_action("Uninstall", success=False, package=str(package))
                self.assertEqual((self.game / binary_path).read_bytes(), binary_before)
                self.assertEqual(license_file.read_bytes(), b"Unrecognized local document")

    def test_standalone_unknown_license_prevents_any_payload_write(self):
        (self.game / "xinput1_4.dll").unlink()
        runtime = self.game / "Sky2PartyEditor"
        runtime.mkdir()
        (runtime / "LICENSES.txt").write_bytes(b"Other document")
        self.run_action(success=False, package=str(self.standalone_package))
        self.assertFalse((self.game / "xinput1_4.dll").exists())
        self.assertEqual((runtime / "LICENSES.txt").read_bytes(), b"Other document")

    def test_asi_refuses_same_product_standalone_before_writing(self):
        self.manual_install(self.standalone_package)
        self.run_action(success=False)
        self.assert_installed_package(self.standalone_package)
        self.assertFalse((self.game / "plugins").exists())

    def test_standalone_refuses_known_or_unknown_asi_without_deleting_it(self):
        (self.game / "xinput1_4.dll").unlink()
        plugin = self.game / "plugins/Sky2PartyEditor.asi"
        plugin.parent.mkdir()
        for content in (self.asi.read_bytes(), b"Unknown same-name plugin"):
            plugin.write_bytes(content)
            self.run_action(success=False, package=str(self.standalone_package))
            self.assertEqual(plugin.read_bytes(), content)
            self.assertFalse((self.game / "xinput1_4.dll").exists())
            self.assertFalse((self.game / "Sky2PartyEditor").exists())

    def test_distribution_conversion_requires_explicit_old_uninstall(self):
        # 只在假游戏目录模拟迁移；不替用户迁移配置，也不由脚本删除公共Loader。
        (self.game / "xinput1_4.dll").unlink()
        self.run_action(package=str(self.standalone_package))
        standalone_settings = self.game / "Sky2PartyEditor/settings.ini"
        standalone_settings.write_bytes(b"Standalone retained preferences")
        self.run_action(success=False)
        self.run_action("Uninstall", package=str(self.standalone_package))
        self.run_action()
        asi_settings = self.game / "plugins/Sky2PartyEditor/settings.ini"
        asi_settings.write_bytes(b"ASI retained preferences")
        self.run_action(success=False, package=str(self.standalone_package))
        self.run_action("Uninstall")
        self.run_action(package=str(self.standalone_package))
        self.assert_installed_package(self.standalone_package)
        self.assertEqual(standalone_settings.read_bytes(), b"Standalone retained preferences")
        self.assertEqual(asi_settings.read_bytes(), b"ASI retained preferences")

    def test_partial_manual_standalone_removal_can_finish_without_directory_deletion(self):
        self.manual_install(self.standalone_package)
        (self.game / "xinput1_4.dll").unlink()
        self.run_action("Uninstall", package=str(self.standalone_package))
        self.assertTrue((self.game / "Sky2PartyEditor").is_dir())
        self.assertFalse((self.game / "Sky2PartyEditor/LICENSES.txt").exists())

    def test_standalone_whatif_and_running_do_not_create_payload(self):
        (self.game / "xinput1_4.dll").unlink()
        self.run_action(what_if=True, package=str(self.standalone_package))
        self.run_action(success=False, running=True, package=str(self.standalone_package))
        self.assertFalse((self.game / "xinput1_4.dll").exists())
        self.assertFalse((self.game / "Sky2PartyEditor").exists())

    def test_manifest_cannot_redirect_files_or_conflict_probe(self):
        for corrupt in ("file-path", "conflict-path", "conflict-hash", "type", "duplicate"):
            with self.subTest(corrupt=corrupt):
                modified = self.case / f"manifest-{corrupt}"
                shutil.copytree(self.package, modified)
                path = modified / "tools/manifest.json"
                manifest = json.loads(path.read_text(encoding="utf-8"))
                if corrupt == "file-path":
                    manifest["files"][0]["path"] = "../external-file"
                elif corrupt == "conflict-path":
                    manifest["conflicts"][0]["path"] = "../external-file"
                elif corrupt == "conflict-hash":
                    manifest["conflicts"][0]["known_sha256"] = ["invalid"]
                elif corrupt == "type":
                    manifest["type"] = "standalone-proxy"
                else:
                    manifest["files"][1] = dict(manifest["files"][0])
                path.write_text(json.dumps(manifest), encoding="utf-8")
                self.run_action(success=False, package=str(modified))
                self.assertFalse((self.game / "plugins").exists())


if __name__ == "__main__":
    unittest.main()

"""验证第三分发的固定载荷边界，使用不可执行的合成 PE，绝不安装游戏。"""

from pathlib import Path
import tempfile
import unittest
import zipfile

from test_installation import PROJECT, POWERSHELL, invoke_ps, synthetic_pe


@unittest.skipUnless(POWERSHELL, "PowerShell 7 is required")
class HubModulePackageTests(unittest.TestCase):
    def test_hub_module_whitelist_and_manifest(self):
        """新包只含模块、清单、许可与说明，不携带原安装器或玩家设置。"""
        with tempfile.TemporaryDirectory(prefix="party-hub-package-") as temporary:
            root = Path(temporary)
            binary = root / "module.dll"
            binary.write_bytes(synthetic_pe(["Sky2Module_Query"]))
            output = root / "out"
            result = invoke_ps(
                "-File", PROJECT / "tools/Package-Mod.ps1", "-Distribution", "HubModule",
                "-BinaryPath", binary, "-OutputDirectory", output,
            )
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            archives = list(output.glob("*-HubModule.zip"))
            self.assertEqual(len(archives), 1)
            with zipfile.ZipFile(archives[0]) as archive:
                names = {name.replace("\\", "/") for name in archive.namelist() if not name.endswith("/")}
                self.assertEqual(names, {
                    "README.md",
                    "dist/plugins/Sky2ModHub/modules/Sky2PartyEditor.module.dll",
                    "dist/plugins/Sky2ModHub/modules/Sky2PartyEditor.module.ini",
                    "dist/plugins/Sky2ModHub/licenses/Sky2PartyEditor-LICENSES.txt",
                })
                manifest_name = next(name for name in archive.namelist() if name.endswith("Sky2PartyEditor.module.ini"))
                manifest = archive.read(manifest_name).decode("utf-8")
                self.assertIn("Id=party\n", manifest)
                self.assertIn("Binary=Sky2PartyEditor.module.dll\n", manifest)
                self.assertIn("Abi=1\n", manifest)
                self.assertIn("Enabled=1\n", manifest)
            self.assertTrue(Path(str(archives[0]) + ".sha256").is_file())

    def test_legacy_or_mixed_entry_is_rejected(self):
        """不能把旧 ASI 或同时含旧入口的 DLL 错标为 Hub 模块。"""
        with tempfile.TemporaryDirectory(prefix="party-hub-reject-") as temporary:
            root = Path(temporary)
            for exports in (["InitializeASI"], ["Sky2Module_Query", "XInputGetState"]):
                binary = root / "wrong.dll"
                binary.write_bytes(synthetic_pe(exports))
                result = invoke_ps(
                    "-File", PROJECT / "tools/Package-Mod.ps1", "-Distribution", "HubModule",
                    "-BinaryPath", binary, "-OutputDirectory", root / "out",
                )
                self.assertNotEqual(result.returncode, 0)


if __name__ == "__main__":
    unittest.main()

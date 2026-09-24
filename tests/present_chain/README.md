# 双插件绘制挂钩回归

这些测试使用隐藏 Win32 窗口、真实 D3D11 WARP 交换链及两个各自静态链接 MinHook 的测试 DLL；不加载游戏，不访问玩家存档。

主工程的 Windows x64 构建会注册六个 `present_chain_*` 测试。也可单独在 MSVC 开发者终端构建：

```powershell
cmake -S tests/present_chain -B build-present-chain
cmake --build build-present-chain --config Release
ctest --test-dir build-present-chain -C Release --output-on-failure
```

依赖需先通过项目的 `tools/Setup-Dependencies.ps1` 准备；也可传入 `-DPRESENT_TEST_MINHOOK=锁定版本的源码目录`。

| 场景 | 预期 |
| --- | --- |
| `race-ab` / `race-ba` | 确定性复现双方先 Create、后 Enable 时，先启用的回调被跳过 |
| `serial-ab` / `serial-ba` | 每个 DLL 完整安装后再安装另一个，两层回调都执行 |
| `ready-ab` / `ready-ba` | 并行初始化，但后加入者在 Create 前等待可证实的就绪信号，两层回调都执行 |

每个场景独立进程运行，检查三次 Present、两份 MinHook 的独立性、逆序移除后的入口字节恢复及原生转发。隐藏窗口的 `DXGI_STATUS_OCCLUDED` 是成功结果。

该回归验证真实挂钩的时序；生产代码的同伴识别、超时降级及游戏内两个面板显示仍需分别验证。不能把“两个 DLL 都加载成功”当作完整绘制链已经保留。

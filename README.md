# Browser Tamer 中文版

> [!NOTE]
> 本项目是 **[aloneguid/bt](https://github.com/aloneguid/bt)（Browser Tamer）** 的汉化分支。
> Browser Tamer 由 **[aloneguid](https://github.com/aloneguid)** 编写并维护，全部核心功能都来自原项目。
> 本仓库只做界面本地化，以及为显示中文所必需的少量改动。

- 原项目仓库：<https://github.com/aloneguid/bt>
- 原项目主页与文档：<https://www.aloneguid.uk/projects/bt/>
- 原项目下载：<https://github.com/aloneguid/bt/releases>
- 支持原作者：<https://www.aloneguid.uk/sponsor/>

如果你喜欢这个软件，请到原项目点个 Star，或者赞助原作者。
与汉化无关的功能问题，请优先向[原项目反馈](https://github.com/aloneguid/bt/issues)。

原项目的英文说明见 [docs/README.md](docs/README.md)。

## 这是什么

Browser Tamer 是一个"浏览器路由器"：把它设为系统默认浏览器后，点击任何链接时，
它会按你配置的规则，把链接交给合适的浏览器或浏览器配置文件（Profile）打开，
也可以弹出选择器让你手动挑选。

## 本分支相对原项目的改动

- 新增界面多语言支持（`app/i18n.*`），翻译文件位于 [`i18n/`](i18n/) 目录，编译时嵌入程序。
  - 目前提供：简体中文（`zh_CN`）。
  - 默认跟随系统界面语言；也可以在"常规 → 语言"菜单中手动切换。
  - 缺少译文的条目会回退为英文原文。
- 加载系统中文字体（Windows 上是微软雅黑/微软正黑体，Linux 通过 fontconfig 查找，macOS 用苹方），
  使中文界面以及中文浏览器名、配置文件名能正常显示。
- MSVC 以 `/utf-8` 编译源码。
- 将依赖库 `grey` 固定到具体的提交。

除上述内容外，程序行为与原项目保持一致。

## 参与翻译

翻译文件是简单的 JSON 键值对，键为英文原文，值为译文：

```json
{
  "Exit": "退出",
  "Discovered {} browser(s)": "发现了 {} 个浏览器"
}
```

- `{}` 占位符的个数必须和原文一致；中文语序需要调整时，可以改用带序号的写法，比如 `{1}…{0}`。
  同一条译文里不要混用 `{}` 和 `{0}`。译文格式写错时，程序会回退显示英文，不会崩溃。
- 以 `@` 开头的键是元数据：`@name` 是语言菜单里显示的名称，`@fallback` 指定缺词时改查哪种语言。
- 想新增一种语言（例如繁体中文），只需新建 `i18n/zh_TW.json`，写上 `"@name": "繁體中文"`、
  `"@fallback": "zh_CN"`，再重新编译即可，不用改代码。系统语言为 zh-TW/zh-HK/zh-MO 时会自动选用它，
  字体也会优先用微软正黑体。

## 构建

需要 CMake ≥ 3.31、vcpkg，Windows 上还需要 Visual Studio（或 Build Tools）的 C++ 工具链。

```bash
cmake --preset x64-release
cmake --build out/build/x64-release
```

预设通过环境变量 `VCPKG_ROOT` 找到 vcpkg。

### 打包安装程序（Windows x64）

本分支用 [Inno Setup 6](https://jrsoftware.org/isinfo.php) 打包，安装脚本是 [`installer/bt.iss`](installer/bt.iss)。
一条命令完成编译、单元测试和打包：

```bash
pwsh scripts/package-win.ps1
```

- 找不到 vcpkg 时加 `-VcpkgRoot <vcpkg 目录>`；`-SkipTests` 跳过单元测试，`-SkipBuild` 只打包现有的编译结果。
- 安装程序输出到 `out/installer/BrowserTamer-zh-<版本>-win64-setup.exe`。
- 安装到当前用户目录（`%LOCALAPPDATA%\BrowserTamer`），不需要管理员权限；写入的注册表项
  与原项目的 MSI 安装包（`resources/wix/patch.xml`）一致。

## 许可证

本项目沿用原项目的 [Apache License 2.0](LICENSE)。
原项目版权归 aloneguid 所有；本分支修改过的文件以 Git 历史记录为准。

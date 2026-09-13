# DSH-Environment 项目说明

> 一个基于 **Qt 6（Widgets，无 QML）** 的 Windows 桌面工具，用于配合 **DeepSeek Harness（DSH）** 的开发与运维。
> 本文档总结当前工程的类结构、功能，以及开发过程中约定的规则。

---

## 1. 项目概述

DSH-Environment 是一个面向本机的「DSH 开发/运维控制台」，核心能力包括：

- **左侧导航栏 + 右侧内容区**（`CUINavBar` + `MainWindow`）
- **环境配置**：Git / Node 目录、插件市场镜像（`CEnvSettingDlg`）
- **DSH 源码管理**：源码条目管理、Profile/服务管理（`CDSHSrcManager`）
- **类似 CMD 的终端**：基于 Windows ConPTY 的伪控制台（`CDSCmdView` + `CDSConPty`）
- **内嵌 WebView 打开 DSH Web 客户端**：用带 token 的网址登录（`CDSWebViewWindow`）
- **DSH 客户端（对话）**：通过 HTTP RPC + WebSocket 事件流访问本机 DSH 服务（`CDSHChatWindow` + `CDSHCommander`）
- **说明（已移除的功能）**：原先导航栏上的「DeepSeek」「今日头条」「GitHub/shgaol」三个按钮与
  「网页小程序」（网页快捷方式管理）功能**均已移除**（网页小程序已迁移到另一个项目）。
  `configure` 下遗留的 `deepseek-web` / `toutiao-web` / `github-shgaol-web` / `webapplet-web` /
  `webapplets` 等目录**本程序不再读写**，可自行删除。

---

## 2. 技术栈 / 构建（见 `CMakeLists.txt`）

- **Qt 6.11.2**（Widgets / Network / WebSockets / WebEngineWidgets），**C++17**，MSVC 2022，`/utf-8`。
- 网页后端探测（按优先级）：Qt **WebEngine**（`DSH_HAVE_WEBENGINE`，首选）→ Qt6 **WebView**（`DSH_HAVE_WEBVIEW`）→ 都没有则文本占位。
- exe 输出到 `DSH-Environment-bin/`；部署时 `windeployqt` 自动拷贝 Qt 运行时：
  - Qt dll + `QtWebEngineProcess.exe` 留在 `DSH-Environment-bin` 根目录（`QtWebEngineProcess.exe` 必须与 `Qt6WebEngineCore.dll` 同目录）。
  - `qml / resources / translations / plugins` 进 `DSH-Environment-bin/Resource`，由启动时 `setupResourceDirByWinApi()` 重定位工作目录与插件/翻译路径。
- 单实例：用 `QLocalServer`/`QLocalSocket`（`DSH-Environment-SingleInstance`）检测，已运行时发 `show` 激活既有实例。

---

## 3. 类结构与功能

> 命名约定：类名 **`C` + 名字**（如 `CApplication`、`CDSHCommander`）；文件名去掉前导 `C`（如 `CDSHCommander` → `DSHCommander.h/.cpp`）。
> 例外：`MainWindow` 未加 `C` 前缀。

### 3.1 入口 / 全局（main.cpp、Application）

| 类 | 文件 | 说明 |
|----|------|------|
| `CApplication` | `Application.h/.cpp` | 继承 `QApplication`；持有全局配置（GitDir/NodeDir/PluginMirror）；环境设置 JSON 持久化（`文档/DSH-Environment/configure`）；**服务端口管理**：`registerServicePort / killServicePort / killAllServices`（程序退出时按端口关闭已启动的 DSH 服务）。 |
| `main.cpp` | — | 程序入口：单实例检测、资源目录重定位（`setupResourceDirByWinApi`）、翻译安装、深色 ToolTip 样式、创建 `MainWindow`、注册导航栏按钮（环境 / DSH 客户端 / 终端）、**侧边栏默认展开**（`navBar()->setExpanded(true)`，宽度经 `expandedChanged` → `MainWindow::updateNavBarWidth` 同步为主窗口宽的 1/6）。 |

### 3.2 界面框架

| 类 | 文件 | 说明 |
|----|------|------|
| `MainWindow` | `MainWindow.h/.cpp` | 主窗口。左侧 `CUINavBar`，右侧 `QTabWidget` 切换两张页：**「DSH源码管理」** 页（`CDSHSrcManager`）和 **「应用」** 页（`QMdiArea`，Tab 模式、带关闭按钮、Fusion 主题）。维护所有 MDI 子窗口映射 `m_webWindows`。关键方法：`openWebView`、`openDshChat` / `openDshChatAt(address, tokenUrl, autoConnect)`、`openCmdView` / `openCmdTerminal`、`closeCmdTerminal`。导航栏宽度：`updateNavBarWidth()`（展开 = 主窗口宽 1/6，收起 72px），由 `expandedChanged` 与 `resizeEvent` 触发。另有两处**网页视图健壮性**处理（非小程序专用）：`kickWebEngineRenders()` 在子窗口被激活时让其中的网页视图重新合成一帧（并给 MDI 设了 `DontMaximizeSubWindowOnActivation`，减少切标签时对网页视图的反复隐藏/显示）。 |
| `CUINavBar` | `NavBar/UINavBar.h/.cpp` | 左侧导航栏。第0部分（图标+三行文本）、第1部分按钮（环境 / DSH 客户端 / 终端）、第2部分树形菜单、第3部分按钮。 |
| `CUINavBarItem` | `NavBar/UINavBarItem.h/.cpp` | 导航栏项（含顶部按钮、树形菜单等）。`setExpanded(bool)` / `isExpanded()` + `expandedChanged`；默认收起（72px 窄条），启动时由 main.cpp 调 `setExpanded(true)` 展开。 |
| `CMdiArea` | `MdiArea.h/.cpp` | 继承 `QMdiArea`，提供 `openWindow(content, title)` 按标题打开/激活子窗口。 |
| `CTableView` | `TableView.h/.cpp` | 继承 `QTableView` 的自定义表格视图。 |

### 3.3 环境 / 源码管理

| 类 | 文件 | 说明 |
|----|------|------|
| `CEnvSettingDlg` | `EnvSettingDlg.h/.cpp` | 环境设置对话框（Git / Node 目录、插件市场镜像）。 |
| `CDSHSrcManager` | `DSHSrcManager.h/.cpp` | **DSH 源码管理页**。表1 管理源码/资源条目（增删改、`DHSSrcDlg` 录入、JSON 持久化）；表2 管理 **Profile/服务**（每行：profile、端口、工作目录、dshHome、服务状态列）。表2 右键菜单：打开 DSH 客户端、打开网页、开启服务（`pnpm dsh --profile %1 --port %2 --no-open`，webMode 用 `--profile web --port 8888`，已移除 robocopy）、关闭服务、打开 cmd 等。通过 `recordWebUrl(url)` 记录服务启动后终端打印的带 token 网址（`m_tokenUrlByPort` 按端口），提供 `tokenUrlForPort(port)`；轮询端口维护「服务是否启动」状态列。 |
| `CDHSSrcDlg` | `DHSSrcDlg.h/.cpp` | 源码条目录入对话框。 |
| `CDHSScrCtrlDlg` | `DHSScrCtrlDlg.h/.cpp` | 源码管理控制对话框（profile 管理、服务控制、终端）。 |
| `CProfileDlg` | `ProfileDlg.h/.cpp` | 「增加实例」/ 双击配置表行编辑用的对话框（DSH_HOME 目录 / **Profile** / **端口**）。**Profile 是可直接输入的文本框**（`QLineEdit`，**默认为 `web`**，可输入任意自定义值如 `test1`；右侧「web」「desktop」两个按钮一键填入预置值，文本框带清空按钮）。**端口默认 `8001`，范围 1 ~ 65535**。点确定时校验：① Profile 不能为空；② **Profile 值不能是 `profile`**（不区分大小写——把值转成大写后与 `PROFILE` 比较，相同则弹「Profile的内容不能输入值不能为profile」并阻止保存，因为 `profile` 是 `pnpm dsh` 的参数名而不是配置名）；③ **端口不能为 0**；④ **端口不能与实例表里已有行的端口重复**（`setUsedPorts()`：由 `CDHSScrCtrlDlg::usedPorts()` 传入实例表第 7 列 + `profiles.json` 的端口，编辑已有行时排除本行自身）。 |
| `CDSHSettingsDlg` | `SettingsDlg.h/.cpp` | 设置对话框。 |

### 3.4 终端（ConPTY）

| 类 | 文件 | 说明 |
|----|------|------|
| `CDSConPty` | `DSConPty.h/.cpp` | Windows **ConPTY** 封装：`CreatePseudoConsole / ResizePseudoConsole`；读写管道；保留一份「列宽 500」的伪控制台尺寸以**避免长 URL 被换行截断**（否则 `dsh web: ...?token=<43字符>` 会断在 42 字符）。 |
| `CDSCmdView` | `DSCmdView.h/.cpp` | 类似 CMD 的终端窗口；用 `QPlainTextEdit` 输出 + `QLineEdit` 输入，无任务栏。ConPTY 输出解码（UTF-8 优先、GBK 兜底）、去 ANSI/VT 序列、CRLF 归一、折叠连续空行；注入 `chcp 65001 >nul`（显示时过滤）与任务完成哨兵 `__DSH_TASK_DONE_7F3B__`；`checkWebUrl` 识别 `dsh web: <url>` 行并发出 `webUrlCaptured`（供上层 `openWebView`）；`eventFilter` 拦截回车避免关闭父对话框。 |
| `CDSTerminal` | `CDSTerminal.h/.cpp` | **空文件**（曾为 Plan B 的格子终端），已从 CMakeLists 移除，保留占位。 |

### 3.5 WebView（DSH Web 客户端 / 外部网站：DeepSeek、今日头条、GitHub/shgaol）

| 类 | 文件 | 说明 |
|----|------|------|
| `CDSWebViewWindow` | `DSWebViewWindow.h/.cpp` | 网页窗口（`QMainWindow`）。顶部**地址栏**（`QLineEdit` 可输入回车跳转）+ **刷新**按钮（外部网站窗口另有**登录数据**按钮，用资源管理器打开数据目录；「刷新」= `rebuildView()` 重建视图 + 重新加载，见下方说明）。网页用 `QWebEngineView`，按 `CDSWebProfileKind` 选 profile：**`DshService`**（默认）用共享持久化 profile `dsh-web`（`ForcePersistentCookies`）+ **token→cookie 登录交换**（对 `http://127.0.0.1:port/?token=X` 发起 GET（`ManualRedirectPolicy`、`NoProxy`、强制 HTTP/1.1、浏览器 UA），读取 `Set-Cookie`，注入 `QWebEngineCookieStore`（放宽为 `SameSite=Lax`），再加载干净的 `/`；失败自动重试；成功后地址栏/标题从带 token 网址切换为干净网址）；**外部网站窗口**（`isExternalSite`：`DeepSeek` = `dsh-deepseek`、`Toutiao` = `dsh-toutiao`、`GitHub` = `dsh-github`）用各自的专属 profile，数据显式落在 `文档/DSH-Environment/configure/<deepseek-web\|toutiao-web\|github-shgaol-web>`（`storage/` 存 cookie·localStorage·站点权限，`cache/` 存 HTTP 缓存）、不做 token 交换，用 `CDSWebEnginePage`（站内链接窗口内导航、**站外链接交给 Edge**）+ `CDSWebEngineView`（右键可交默认浏览器打开）。这三个类型是**内置站点 profile 预设**：原导航栏按钮已移除，**目前没有调用方**（预留给以后再用）。**站点信息表驱动**（`DSWebViewWindow.cpp` 里的 `kSiteInfos`：标题 / profile 名 / 数据目录名 / 站内域名后缀），新增类型只需加枚举值 + 一行。对外提供 `defaultTitle(kind)` / `isExternalSite(kind)`。`urlChanged` 刷新地址栏并发出 `urlChanged` 信号（**外部网站窗口的 MDI 标题固定为站点名、不跟随网址**；DSH 服务窗口仍由 `openWebView` 让标题跟随网址）；`QWebEngineView::iconChanged`（网页图标/favicon）转成 `faviconChanged(QIcon)` 信号发出，供外层把 MDI 子窗口图标（=「应用」页 tab 图标）设成网页自身的图标。 |
| `CDSWebEnginePage` | `DSWebEnginePage.h/.cpp` | 仅 WebEngine 后端编译（整份 `#ifdef DSH_HAVE_WEBENGINE`，无自定义信号/槽故不声明 `Q_OBJECT`）。**站外链接交给外部浏览器（Edge）打开**：① 用户点击的站外链接（主框架、`NavigationTypeLinkClicked`）→ `acceptNavigationRequest` 返回 `false` 并 `openInExternalBrowser()`，内嵌窗口保持当前页面；② `target="_blank"`/`window.open`/中键/右键“在新标签页打开”（`newWindowRequested`）→ 站外同样交给 Edge，站内用 `QTimer::singleShot(0)` 就地 `setUrl()` 导航（Qt 默认会把这类请求直接丢弃）。站内/站外按站点信息表的域名后缀判定（`deepseek.com` / `toutiao.com` / `github.com`）；`javascript:/data:/blob:/about:` 等页面内部协议放行，`mailto:/tel:` 交系统默认程序。`openInExternalBrowser()` 先查注册表 `App Paths\msedge.exe`（HKCU→HKLM）再查常见安装目录，找不到 Edge 或启动失败则回退 `QDesktopServices::openUrl`。 |

> 与 DSH Web 客户端鉴权：DSH 服务端 `browser-auth` 用「进程令牌（token）→ 303 + 签名 cookie」做登录；cookie 绑定 authority（Host 头）。本窗口在不改服务端的前提下，由应用自己完成 token→cookie 交换并注入 WebEngine。
>
> 与外部网站登录（内置站点预设 DeepSeek / 今日头条 / GitHub）：登录态是站点自己的 cookie（+ localStorage），因此**不动站点、也不动系统浏览器**，只把承载它的 WebEngine profile 的持久化目录指到 `configure` 下。这样「清理 Edge 缓存/Cookie」与「本程序的登录态」互不影响；备份 `configure/deepseek-web`（或 `toutiao-web` / `github-shgaol-web`）目录即可迁移/恢复登录信息（`ForcePersistentCookies` 让会话 cookie 也落盘）。页面主题/暗色直接用**站点自带的主题功能**，程序不做任何强制配色。
>
> 与外部网站的外链：站内的引用跳转等**站外链接**改由外部浏览器（Edge）打开（`CDSWebEnginePage`），内嵌窗口保持当前页面；地址栏手动输入的网址仍在内嵌窗口打开（那是显式的站内浏览）；只拦 `NavigationTypeLinkClicked`，登录/重定向等其它导航类型一律放行。
>
> 与 MDI 标签切换（切几次后切回来变空白）：`QWebEngineView` 内部是 `QQuickWidget`（离屏渲染到 FBO），而 `QMdiArea` 的 TabbedView 只显示当前子窗口，切走的标签会把子窗口隐藏（Qt 在 `QWebEngineView::hideEvent` 里把 page 置为不可见，见 `qwebengineview.cpp`）。反复隐藏/显示后有两种坏情况：page 停在“不可见”（渲染暂停）、或该视图的离屏渲染表面失效 —— 后者表现为画面一直空白，**连 `reload()` 也刷不出来**（页面内容还在，只是没有被合成到窗口上），只有重建视图才能恢复（与手动“关掉这一页再重新打开”等价）。对策：
> ① `main.cpp` 在创建 `QApplication` **之前**设置 `Qt::AA_ShareOpenGLContexts`（多个网页视图共享 OpenGL 上下文）；
> ② `MainWindow::kickWebEngineRenders()`：子窗口被激活时（切 MDI 标签、或从「DSH源码管理」切回「应用」页）恢复 page 可见性 + 解除冻结 + `update()` + 1px 尺寸微调强制重新合成（**刻意不做 hide()/show()**：反复重建表面会让失效来得更快）；
> ③ `CDSWebViewWindow::reload()`（「刷新」按钮）= **重建网页视图** + 按当前网址重新加载 —— 即手动“关掉这页再打开”的等价操作，保证空白窗口能就地救回；
> ④ 渲染进程异常结束时（`renderProcessTerminated`）同样自动重建视图（同一窗口最多 3 次）。
> 视图的创建与信号接线统一在 `CDSWebViewWindow::createWebView()`，构造函数与 `rebuildView()` 共用，保证重建出的视图与原视图一致。
>
> 与右键「使用默认浏览器打开链接」：`CDSWebEngineView::contextMenuEvent()` 里这一项**始终存在**。优先用 WebEngine 给的链接地址（`linkUrl()`，即 Chromium 的 *unfiltered* 链接地址；`filteredLinkUrl()` 是 Qt 私有 API 用不了），拿不到时不再像最早那样直接不加菜单项 —— 因为 Chromium 只对它自己识别出的链接给地址，**被浮层（透明覆盖层/自定义控件）盖住的链接、SVG 里的 `<a>`、JS 跳转的元素都拿不到**，于是表现为“有的页面/目标没有这一项”。兜底办法：`createWebView()` 在每次 `loadFinished` 后向页面注入 `kLinkHookScript`（捕获阶段监听 `contextmenu`/右键 `mousedown`，用 `document.elementsFromPoint` 从最上层元素逐个向上找 `<a>`，把绝对地址记到 `window.__dshLinkUnderCursor`）；菜单项被触发而 `linkUrl` 不可用时，就用 `runJavaScript` 读这个变量再交 `QDesktopServices::openUrl()`。只认链接（不用 `mediaUrl`，免得右键普通图片也被当成打开链接），且只有 `http/https` 才打开（`javascript:`/`data:` 忽略）；真正没有链接时该项点了没有反应。注意 `createStandardContextMenu()` 已设 `WA_DeleteOnClose`，**不能**再 `delete menu`（会重复释放）。

### 3.6 DSH 客户端（对话）

| 类 | 文件 | 说明 |
|----|------|------|
| `CDSHCommander` | `DSHChatWindow/DSHCommander.h/.cpp` | 与 DSH 服务的通信封装（`QObject`）。**HTTP 命令通道**：POST `/api/<namespace/method>`（**斜杠**端点），请求体 `{type:"client-request", rpcId, method, payload:{args:{request:{...}}}}`（无参方法用 `{args:{}}`），统一带登录 cookie；带参方法参数名是 `request`（如 `follow(request)`、`prompt(request)`）。**WebSocket 事件流**：连 `/api/remote.mux`（带 cookie）多路复用流，打开逻辑流 `$events`（登录/转发事件）、`workspace/follow`（工作区树）、`session/follow`（会话历史+实时事件）。**token 登录**：对带 token 网址发 GET 交换 cookie（`NoProxy`），存 `m_cookieHeader` 用于 HTTP/WS 鉴权。常用命令封装：`session.list/create/prompt/page/models/selectModel/cancel/rename`、`workspace.create/rename/delete/archiveSession`、`agentPresets.list`、`settings.*`。`followSession(sessionId)` 打开 `session/follow` 流，快照即历史、之后为实时事件。 |
| `CDSHChatWindow` | `DSHChatWindow/DSHChatWindow.h/.cpp` | DSH 客户端窗口（`QMainWindow`）。左侧工作区/会话两级树（数据来自 `workspace/follow` + `session.list`）；右侧：地址栏（加宽）+ 连接/断开、思考栏（地址栏下方）、会话标题、模型下拉、权限下拉、消息区（气泡、问题记录表）、输入区（`CDSDropTextEdit`，支持拖入图片附件）。支持提问面板、审批行内按钮、附件 chip、归档会话、导出 PDF、系统托盘完成提示、折叠任务进度面板、会话标题本地缓存。点开会话用 `session.follow` 快照渲染历史 + 流式显示实时事件。 |
| `CDSDropTextEdit` | `DSHChatWindow/DSDropTextEdit.h/.cpp` | 支持拖入文件（图片附件）的输入编辑框。 |
| `DSHImageAttachment` | `DSHChatWindow/DSHCommander.h`（struct） | 图片附件结构：`mediaType / dataBase64 / name`。 |

---

## 4. 关键流程

### 4.1 WebView 打开 DSH Web 客户端
「开启服务」命令带 `--no-open`（服务不自动开浏览器）→ 终端 `CDSCmdView` 输出 `dsh web: http://127.0.0.1:port/?token=X` → `checkWebUrl` 识别 → `MainWindow::openWebView` → `CDSWebViewWindow` 做 **token→cookie 交换** → 注入 cookie 并加载干净 `/`。
要点：ConPTY 列宽 500 保证 43 字符 token 不被截断；cookie 放宽为 `Lax`；`NoProxy` + HTTP/1.1。

### 4.2 外部网站窗口与内置站点 profile 预设（登录信息落在 configure）
- **窗口行为**（所有外部网站窗口共用）：专属持久化 profile → `CDSWebEnginePage`（站内链接窗口内导航、站外链接交给 Edge）+ `CDSWebEngineView`（右键“使用默认浏览器打开链接”）+ 地址栏/刷新/「登录数据」按钮；MDI 标题固定（站点名）、不跟随网址。
- **内置站点 profile 预设**（`kSiteInfos` 里的 `DeepSeek` / `Toutiao` / `GitHub`）：原来各有导航栏按钮（`openDeepSeekWeb` / `openToutiaoWeb` / `openGithubWeb` → `openSiteWindow(kind)` → `CDSWebViewWindow(kind)` → `homeUrl(kind)`），**按钮与这些入口方法已移除**；预设只保留 profile 名、数据目录名与「站内域名后缀」，**目前没有调用方**（预留给以后再用）。`homeUrl` 字段与静态查询、以及 `kindForUrl(url)` / `profileFor(kind)` 都已随按钮一并删除。
- 要点：profile 的 `persistentStoragePath = 文档/DSH-Environment/configure/<站点目录>/storage`、`cachePath = .../cache`（都在使用前 `set`，否则 WebEngine 会回退到默认数据目录）；`ForcePersistentCookies` + `DiskHttpCache` + 站点权限 `StoreOnDisk`；UA 去掉 `QtWebEngine/x.y.z` 标记避免被站点降级/风控。主题/暗色用站点自带功能，程序不强制配色。
- 外链：页对象 `CDSWebEnginePage(<站内域名后缀>)`（预设 DeepSeek = `deepseek.com`、今日头条 = `toutiao.com`、GitHub = `github.com`）→ 点击站外链接（含 `target="_blank"` / `window.open`）交给 Edge 打开；Edge 定位见 3.5 节。

### 4.3 DSH 客户端登录 / 连接
从「DSH源码管理」表2 右键打开客户端 → `MainWindow` 取该端口的带 token 网址并 `autoConnect=true` → `CDSHCommander`：**token 交换拿 cookie** → 连 `/api/remote.mux`（带 cookie）→ 打开 `$events` + `workspace/follow` 逻辑流 → 加载会话树 / Agent 预设；点开会话 → 打开 `session/follow` 流，`snapshot` 渲染历史 + 后续实时事件。
要点：所有 HTTP 命令端点用**斜杠** `namespace/method`，带参方法 `payload={args:{request:{...}}}`；`session.list` / `session.modelCatalog` / `agentPresets.list` / `workspace.archiveSession` 等均返回 200。`.page` 用于分页（需 `throughSeq`），冷启动历史改用 `session/follow` 快照。

---

## 5. 开发规则（用户约定）

- **技术**：Qt 6.11.2；**不用 QML**；**只写代码、不编译**（由用户编译验证）。
- **命名**：新类名 **`CXXXX`**（如 `CDSHCommander`），文件 `XXXX.h/.cpp`。
- **内存**：**不用智能指针**（用裸指针 + `new` / 对象树父子关系管理）。
- **路径**：Qt 源码 `Z:\QTSource\6.11.2\Src`；Qt 构建目录 `Z:\QTSource\6.11.2\msvc2022_64`；DSH 源码 `D:\DSH\deepseekharness`（**只读，不修改**；外部改动需专门授权）。
- **界面**：遵循 UI 字体/背景对比度；**不做部分显示**（放不下用滚动条）。
- **交互**：问题中含「确认」→ **只分析、不操作**；用**中文**回复结果/选择/审批；未明确要求操作时只做确认。
- **DSH 服务端**：不修改；在 DSH 客户端/WebView 侧用「token→cookie 交换 + 带 cookie 的 HTTP/WS」配合服务端浏览器会话鉴权。

---

## 6. 备注

- **未构建 / 死文件**：`CDSTerminal.h/.cpp`（已清空，移出 CMakeLists）；`DSTerminalView.*`、`DSTerminalEmulator.*`（备用终端实现，未在 CMakeLists 中）。
- **已删除的目录**：`WebApplet/`（`DSWebAppletStore` / `DSWebAppletDlg` / `DSWebAppletIconFetcher` / `DSWebAppletPage` 的 .h/.cpp）—— 「网页小程序」功能已迁移到另一个项目，文件与目录都已删除、并已移出 CMakeLists。
- **已移除的功能**：导航栏「DeepSeek」「今日头条」「GitHub/shgaol」三个按钮、`MainWindow::openSiteWindow` / `openDeepSeekWeb` / `openToutiaoWeb` / `openGithubWeb`、以及「网页小程序」（表页 / 快捷方式 / 图标读取 / 数据存取）。`configure` 下遗留的 `deepseek-web` / `toutiao-web` / `github-shgaol-web` / `webapplet-web` / `webapplets` 目录本程序不再读写。
- **保留但暂无调用方**：`CDSWebProfileKind` 里的 `DeepSeek` / `Toutiao` / `GitHub` 三个**内置站点 profile 预设**、`CDSWebEnginePage`（站外链接交给 Edge）、`CDSWebEngineView`（右键“使用默认浏览器打开链接”+ 注入的 `kLinkHookScript` 链接兜底）—— 都为「外部网站窗口」准备的通用能力，以后要再开外部站点窗口时可直接用。
- **DSH 源码写入** 被沙箱禁止；如需改 DSH 源码需用户批准（`danger-full-access`），且用户已明确「不改 DSH 源码」。
- 客户端/WebView 的**日志显示框**（调试用 `QTextBrowser`）已按需求移除；`DSHCommander` 中遗留的 `logMessage` 信号发射不再被界面接收，属无害死代码。

# DSH-Environment
dsh源码编译以及一些与ai开发有关的功能

# DSH-Environment 类功能说明

> 本文件是 **Qt 6.11.2（mingw / msvc2022，CMake）** 版 DSH-Environment 的**版本终结说明**。
>
> 项目目标：用纯 Widgets（**不使用 QML**）复刻 DeepSeek Harness（DSH）桌面端外观与交互，并让界面与
> 本地运行的 DSH 服务通信。通信协议见 `CDSHCommander` 一节。
>
> 约定：类名带 `C` 前缀（`CXXXX`），每个类一对 `XXXX.h` / `XXXX.cpp`。

---

## 一、全局与入口

### `CApplication`（`Application.h` / `Application.cpp`）—— 应用类
继承 `QApplication`，持有全局环境配置并负责程序级生命周期。

| 职责 | 说明 |
| --- | --- |
| 唯一实例 | `CApplication::instance()`，`main` 中创建后全局可用 |
| 环境配置 | Git 目录、Node 目录、插件市场镜像的读写（`gitDir / nodeDir / pluginMirror`） |
| 持久化 | 环境设置 JSON 保存/读取（`saveEnvSettings / loadEnvSettings`），配置目录为 `文档/DSH-Environment/configure` |
| 服务端口 | 登记/注销运行中的服务端口，程序退出时按端口关闭对应服务（`killAllServices`） |

### `main.cpp`—— 程序入口（函数，非类）
- 用 Win32 API 获取 exe 目录，并在 QApplication 创建前把工作目录、插件、翻译等指向 `bin/Resource`。
- 加载 `qt_zh_CN.qm` 翻译；屏蔽 Qt 内部噪音日志。
- **单实例**：用命名管道（`QLocalServer`/`QLocalSocket`，key=`DSH-Environment-SingleInstance`）检测，
  已有实例则发送 `show` 让其显示主窗口，本实例退出。
- 给 `CApplication` 设置窗口图标（`:/app.ico`），连接 `aboutToQuit → killAllServices()`。
- 创建 `MainWindow`，往左侧导航栏（`CUINavBar`）填“环境”“DSH 客户端”“终端”“网页小程序”四个顶部按钮并连接点击行为；
  设置信息区（图标 + DSH / DSH-Environment / 作者:shgaol）；**侧边栏默认展开**（`navBar()->setExpanded(true)`），最后最大化显示。
  （原先的“DeepSeek”“今日头条”“GitHub/shgaol”三个按钮已移除，这三个站点改由“网页小程序”打开。）

---

## 二、通信层

### `CDSHCommander`（`DSHCommander.h` / `DSHCommander.cpp`）—— DSH 服务通信封装
对 DSH（DeepSeek Harness）的 RPC 通信，协议来自 `dsh-client-connection` / `dsh-host-apiproxy`。

- **命令通道**：`HTTP POST /api/<method>`，JSON-RPC 信封
  `{type:"client-request", rpcId, method, payload}` → 响应
  `{type:"server-response", rpcId, result:{ok:true,value} | {ok:false,error}}`。
- **事件通道**：WebSocket 下行流（仅收不发，发送会被服务端以 1008 断开）。
  - `ws://host:port/api/events.mux`：会话事件流
  - `ws://host:port/api/events.host`：主机事件流
  - 帧格式 `{type:"server-request", rpcId, method, payload}`。
- **不自动重连、不使用定时器**；断开后由界面手动 `connectToServer()`。

封装的主要命令：`session.list / create / prompt(queue) / rename / cancel / history / models / selectModel`，
`workspace.list / create / rename / delete / archiveSession`，`agentPreset.list`，
`settings.describe / update / mutate`，`host.describe`；提问答案经 `respond()` 走 `/api/respond` 回传。

信号：`connectionChanged / commandFinished / muxEventReceived / hostEventReceived /
sessionEventReceived / questionRequested / errorOccurred`。

---

## 三、主窗口与导航

### `MainWindow`（`MainWindow.h` / `MainWindow.cpp`）—— 主窗口
结构：左侧 `CUINavBar` 导航 + 右侧 `QTabWidget`（“DSH源码管理” / “应用”）。

- “应用”页内嵌 `QMdiArea`（Tab 模式、带关闭按钮、Fusion 主题）。
- `openWebView(url)`：按网址去重，同一网址只开一个 `CDSWebViewWindow`，重复则激活。
- `openDshChatAt(address)` / `openDshChat()`：按 `客户端: address` 去重打开/激活 `CDSHChatWindow`。
- `openWebApplets()`：在 MDI 打开/激活标题为“网页小程序”的表页（已存在则激活，不重复新建）；
  `openWebAppletWindow(name, url)`：双击表页里的快捷方式时打开网页窗口——按网址取 profile
  （`CDSWebViewWindow::kindForUrl(url)`：属于内置站点预设则沿用其 profile，否则用网页小程序自己的），
  按小程序名称去重，MDI 标题为小程序名称、不跟随网址。
  （原 `openSiteWindow(kind)` 与 `openDeepSeekWeb()` / `openToutiaoWeb()` / `openGithubWeb()` 已随
  三个导航栏按钮一并移除，这三个站点改由网页小程序打开。）
- `updateNavBarWidth()`：导航栏宽度 = 展开时主窗口宽 1/6、收起时 72px（`expandedChanged` 与 `resizeEvent` 触发）；
  侧边栏默认展开由 main.cpp 在显示前 `setExpanded(true)` 完成。
- 关闭/最小化 → 隐藏到系统托盘；托盘右键“显示 / 退出”，仅“退出”真正退出。
- `makeDIcon(bg)`：生成 蓝色圆角方块 + 白色 D 字 的应用图标（此处为全局函数）。
- `m_srcManager` 的 `webViewRequested` → `openWebView`，`dshChatRequested` → `openDshChatAt("127.0.0.1:"+port)`。

### `CUINavBar`（`NavBar/UINavBar.h` / `.cpp`）—— 整体导航工作区
左侧导航栏 + （右侧内容区由 MainWindow 提供）。对外提供按钮/树的增删与信息区接口，
信号 `topbtnClicked / botbtnClicked / treeItemClicked / infoClicked / infoRightClicked`。

### `CUINavBarItem`（`NavBar/UINavBarItem.h` / `.cpp`）—— 左侧导航栏
仿 DSH 工作区侧栏，分四部分：**0. 任务属性区**（图标 + 三行文本）、**1. 顶部图标按钮列**（可滚动）、
**2. 树形菜单**、**3. 底部图标按钮列**（可滚动）。可展开/收起（收起时窄条只显图标）；
`setExpanded(bool)` / `isExpanded()` 配 `expandedChanged` 信号，默认收起，启动时由 main.cpp 展开。
图标由程序内置绘制（`enum class Icon` + `makeLetterIcon`），无需外部图片资源。

### `CDSWebViewWindow`（`DSWebViewWindow.h` / `.cpp`）—— 内嵌网页窗口
`QMainWindow`，顶部地址栏 + 刷新（有专属 profile 的窗口另有「登录数据」按钮）。
按 `CDSWebProfileKind` 选 profile：`DshService`（共享 `dsh-web` + token→cookie 交换）、
`DeepSeek`（`dsh-deepseek` → `configure/deepseek-web`）、`Toutiao`（`dsh-toutiao` → `configure/toutiao-web`）、
`GitHub`（`dsh-github` → `configure/github-shgaol-web`）、`WebApplet`（`dsh-webapplet` → `configure/webapplet-web`，网页小程序）。
后四种都是“外部网站窗口”（`isExternalSite`），数据目录独立于 Edge：用专属 profile +
`CDSWebEnginePage`（站外链接交给 Edge）+ `CDSWebEngineView`（右键可交默认浏览器打开），
行为完全一致，区别只在“站内域名后缀”的来源——前三种取站点信息表，`WebApplet` 由构造参数按小程序网址给出。
其中 `DeepSeek` / `Toutiao` / `GitHub` 是**内置站点 profile 预设**：原先各有导航栏按钮（已移除），
保留预设是为了让指向这些站点的小程序沿用它们原有的 profile 与数据目录，登录状态不丢。
站点信息表驱动（`kSiteInfos`：标题 / profile 名 / 数据目录名 / 站内域名后缀），
对外提供 `defaultTitle(kind)` / `isExternalSite(kind)` 与
`kindForUrl(url)`（按站内域名后缀把网址匹配到内置站点预设，用于网页小程序沿用该预设的 profile）、
`profileFor(kind)`（供网页图标读取复用同一 profile）、`dataDir()` / `openDataDir()`。
（原静态查询 `homeUrl(kind)` 与 `MainWindow::openSiteWindow` 已随三个按钮移除。）

### `CDSWebEnginePage`（`DSWebEnginePage.h` / `.cpp`）—— 站外链接交给外部浏览器（Edge）
仅在 WebEngine 后端编译（整份 `#ifdef DSH_HAVE_WEBENGINE`；无自定义信号/槽，故不声明 `Q_OBJECT`）。
- 主框架里**用户点击的站外链接**（`NavigationTypeLinkClicked`）→ `acceptNavigationRequest` 返回 `false`，
  用 `openInExternalBrowser()` 交给 Edge，内嵌窗口保持当前页面（网页小程序窗口里点到的站外来源链接等）。
- **新窗口请求**（`target="_blank"`、`window.open`、中键、右键“在新标签页打开”）→ 站外交给 Edge；
  站内用 `QTimer::singleShot(0)` 就地 `setUrl()`（Qt 默认会把这类请求直接丢弃，表现为“点了没反应”）。
- 站内/站外按构造传入的域名后缀判定（内置预设 DeepSeek = `deepseek.com`、今日头条 = `toutiao.com`、GitHub = `github.com`；
  网页小程序窗口由 `CDSWebAppletPage::siteHostSuffix(url)` 从网址推出，如 `chat.deepseek.com` → `deepseek.com`）；
  `javascript:/data:/blob:/about:` 等页面内部协议放行，`mailto:/tel:` 交系统默认程序。
- `openInExternalBrowser()`：先查注册表 `App Paths\msedge.exe`（HKCU → HKLM），再查
  `ProgramFiles(x86)/ProgramFiles/LOCALAPPDATA` 下的 Edge 安装路径；找不到或启动失败回退 `QDesktopServices::openUrl`。

---

## 四、DSH 对话窗口（核心）

### `CDSHChatWindow`（`DSHChatWindow.h` / `DSHChatWindow.cpp`）—— DSH 对话/客户端窗口
仿 DSH 桌面端对话界面，通过 `CDSHCommander` 与本地 DSH 服务交互。可指定地址构造，多个实例连接同一 DSH。

- **左侧树**：工作区 → 会话两级；会话名取 `projections.values.title`；隐藏空白/子代理/归档会话；右键可改名、新建、归档。
- **右侧**：顶部（服务器地址、连接状态、会话标题、模型选择）+ 消息流 + 输入区。
- **消息气泡**：用户消息 5/6 宽右对齐（绿 `#1F6F43`），助手消息 5/6 宽左对齐；折叠思考块、下拉工具参数、上下文注入卡片；官方深色配色（`#151517` 背景等）。
- **提问交互面板**：收到 `question/requested` 帧展示问题列表，答案经 `respond()` 回传。
- **右侧问题索引**：仅收含中文的提问（取首行），双击滚动到对应气泡。
- **输入区**：Enter 发送 / Shift+Enter 换行（`eventFilter`）；输入框高度自适应，发送后重置。
- **导出 PDF**：`QPdfWriter` 打包消息；默认名 = `工作区名_会话名`，用户消息绿色。
- model 选择、会话历史加载（`session.history` 事件回放）、会话归档。

---

## 五、DSH 源码管理（含子对话框）

### `CDSHSrcManager`（`DSHSrcManager.h` / `DSHSrcManager.cpp`）—— DSH 源码管理页
`QWidget` 页签，内含两张 `CTableView`（各带按钮区）。

- **表 1**：DSH 源码记录（名称/源码目录/版本/说明），支持增、删（`CDHSSrcDlg` 录入），数据持久化到 `srcmanger.json`。
- **表 2**：按 `profiles.json` 中与选中记录相同 `mainid` 的配置行（DSH_HOME / Profile / 端口）。
- **右键菜单**（选中表 2 行）：打开cmd / 打开网页（`webViewRequested`）/ 打开DSH客户端（`dshChatRequested`）/ 重启服务 / 关闭服务 / 停止·开启服务。
- **服务管理**：用 `npx pnpm dsh --port <port> --no-open` 启动后台进程，500ms 轮询端口（最多 1 分钟），
  运行成功即 `markServiceRunning`（标记“已经启动”并在 MDI 打开该端口网页）；每 1 分钟健康检查，连不上则杀掉。
- **服务开启成功后仅打开网站（`webViewRequested`），不打开 DSH 客户端**；打开 DSH 客户端只能由右键菜单手动触发。

### `CDHSSrcDlg`（`DHSSrcDlg.h` / `.cpp`）—— 源码录入对话框
录入 名称（蓝色）/ 源码目录 / 版本（三者必填）/ 说明（只读）。选目录时若名称、版本为空，用目录名自动填充。

### `CDHSScrCtrlDlg`（`DHSScrCtrlDlg.h` / `.cpp`）—— 源码记录查看/执行对话框
字段与 `CDHSSrcDlg` 一致但全部只读；下方提供“是否执行 install / build”复选框 + 执行按钮，右侧命令输出区
（QProcess 实时显示，可继续输入命令）；“确定”把执行状态同步到 `profiles.json`。

### `CProfileDlg`（`ProfileDlg.h` / `.cpp`）—— 实例配置对话框
录入 DSH_HOME 目录 / Profile（**可直接输入的文本框，默认 `web`**；右侧「web」「desktop」按钮一键填入预置值）/ 端口（**默认 `8001`，范围 1 ~ 65535**），供配置表行使用（“增加”按钮 / 双击配置表行）。
点“确定”时校验：
- Profile 不能为空，且**值不能是 `profile`**（转成大写后与 `PROFILE` 比较，相同则弹「Profile的内容不能输入值不能为profile」并阻止保存）；
- 端口**不能为 0**、**不能与实例表里已有行的端口重复**（`setUsedPorts()` 由 `CDHSScrCtrlDlg::usedPorts()` 传入：第 7 列 + `profiles.json`，编辑时排除本行自身端口）。

### `CEnvSettingDlg`（`EnvSettingDlg.h` / `.cpp`）—— 环境设置对话框
设置 Git 目录 / Node 目录 / 插件市场镜像，点确定保存到 `CApplication`（并触发 `saveEnvSettings` 落盘）。

---

## 六、网页小程序（WebApplet）

### `CDSWebAppletPage`（`WebApplet/DSWebAppletPage.h` / `.cpp`）—— “网页小程序”表页
在「应用」页的 MDI 中以子窗口形式打开（标题“网页小程序”，已存在则激活）。

- **按钮区**：增加 / 修改 / 删除。**快捷方式区**：`QListWidget` 图标模式（48×48 图标 + 名称，自动换行）。
- **双击**（或选中项上回车）→ `openRequested(name, url)` → `MainWindow::openWebAppletWindow`：
  打开 `CDSWebViewWindow(kindForUrl(url), nullptr, siteHostSuffix(url))`，**行为与内置站点预设窗口完全一致**，
  MDI 标题为小程序名称、按名称去重。
- **profile 按网址复用（登录状态共用，不用重新登录）**：网址属于内置站点预设时，`kindForUrl(url)` 返回该预设类型，
  profile 与**原来侧边栏按钮打开的窗口**是同一份（DeepSeek → `configure/deepseek-web`、今日头条 → `configure/toutiao-web`、
  GitHub → `configure/github-shgaol-web`）；其它网址才用 `configure/webapplet-web`。
- **右键某个小程序** → 菜单「修改 / 删除」（右键会先选中被点中的小程序；空白处右键不弹菜单）。
- 校验规则由 `CDSWebAppletDlg` 保证：名称/网址必填、名称不可重复（忽略大小写，修改时排除自己）。
- 保存后异步读取该网页的图标，取到后把快捷方式刷新为“网页图标 + 名称”。
- 静态工具 `siteHostSuffix(url)`：由网址推出“站内域名后缀”（`chat.deepseek.com` → `deepseek.com`、
  `www.163.com` → `163.com`、`www.abc.com.cn` → `abc.com.cn`），供 `CDSWebEnginePage` 判定站内/站外链接。

### `CDSWebAppletDlg`（`WebApplet/DSWebAppletDlg.h` / `.cpp`）—— 网页小程序录入对话框
增加/修改共用。名称必填且不能重复、网址必填并在缺协议时自动补 `https://`（`normalizeUrl`），
另校验网址格式（`QUrl` 合法且带主机名）。

### `CDSWebAppletIconFetcher`（`WebApplet/DSWebAppletIconFetcher.h` / `.cpp`）—— 网页图标读取器
异步读取网页图标并落盘（64×64 PNG），取值顺序：Qt6 已解码图标（`iconChanged`）→ 网页声明的
图标地址（`iconUrl`，`data:` 内联图标就地解码）→ `<站点>/favicon.ico` → 都失败则不发信号（界面用名称首字图标兜底）。
探测页面用的 profile 与双击打开小程序窗口的一致（`profileFor(kindForUrl(url))`）。
父对象挂在 `qApp` 上并自行 `deleteLater`，因此表页关闭后图标仍能取回并写回 JSON；取到后发 `iconReady`。

### `CDSWebAppletStore`（`WebApplet/DSWebAppletStore.h` / `.cpp`）—— 小程序数据存取
条目 `DSWebApplet{name, url, iconFile}`；JSON 落在 `文档/DSH-Environment/configure/webapplets/webapplets.json`，
图标落在 `.../webapplets/icons/<uuid>.png`。`setIconFile(name, url, iconFile)` 只在名称与网址都一致时写回，
避免改名/改址后把图标记到别处；图标文件名用 UUID 避开名称里的文件名非法字符。

---

## 七、基础控件

### `CMdiArea`（`MdiArea.h` / `.cpp`）—— MDI 多文档区域
继承 `QMdiArea`，提供 `openWindow(content, title)`：按标题打开或激活已有子窗口。

### `CTableView`（`TableView.h` / `.cpp`）—— 通用表格视图
继承 `QTableView` 的薄封装，供 `CDSHSrcManager`、`CDHSScrCtrlDlg` 使用。

---

## 八、遗留（未编译进当前版本，保留在磁盘）

- **`CDSHSettingsDlg`**（`SettingsDlg.h` / `.cpp`）：曾开发的 DSH 设置界面（官方 通用设置/模型/插件/Agent 预设 四栏布局），
  因用户取消 `CDSHChatWindow` 的设置入口而停用，**已从 `CMakeLists.txt` 移除**，不再参与构建。
- **旧 `WebViewWindow`**：已重命名为 `CDSWebViewWindow`（`DSWebViewWindow.h/.cpp`），旧文件不再存在；
  `build` 目录中残留的历史 `moc_WebViewWindow.cpp` 属旧构建产物，可忽略。

---

## 九、本版本要点回顾

- 纯 Widgets，Qt 6.11.2，CMake 构建；输出 `bin/`（exe + Qt DLL）、插件到 `bin/Resource`，并生成 `qt.conf`。
- 与 DSH 服务通过 HTTP POST `/api/<method>` + WebSocket 下行流通信，支持会话/工作区/模型/预设/设置/提问等。
- 会话名取自 `projections.values.title`，权限从 `projections.values.permissions`；归档仅隐藏。
- 表 2 服务启动成功后只打开网站；打开 DSH 客户端由右键菜单手动触发，按 `127.0.0.1:<port>` 去重。
- `CDSWebViewWindow` 支持五种 profile（`CDSWebProfileKind`）：`DshService`（共享 `dsh-web` + token→cookie 交换）、
  `DeepSeek`（`dsh-deepseek` → `configure/deepseek-web`）、`Toutiao`（`dsh-toutiao` → `configure/toutiao-web`）、
  `GitHub`（`dsh-github` → `configure/github-shgaol-web`）、`WebApplet`（`dsh-webapplet` → `configure/webapplet-web`）；
  后四种数据独立于 Edge，清 Edge 缓存不影响登录；站点信息表驱动（`kSiteInfos`），新增类型只需加枚举值 + 一行。
  `DeepSeek` / `Toutiao` / `GitHub` 现为**内置站点 profile 预设**（原导航栏按钮已移除）。
  站点的主题/暗色使用网站自带功能，程序不强制配色。
- 外部网站窗口（含网页小程序窗口）用 `CDSWebEnginePage`：页面里的**站外链接**（含 `target="_blank"`/`window.open` 的新窗口请求）
  点击后交给 Edge 打开，内嵌窗口留在当前页面；站内由窗口的“站内域名后缀”决定（预设 DeepSeek/Toutiao/GitHub 分别为
  `deepseek.com` / `toutiao.com` / `github.com`，小程序窗口按网址推出）。
- **网页小程序**：导航栏「网页小程序」按钮 →「应用」页 MDI 中的“网页小程序”表页（已存在则激活）。
  表页支持增加/修改/删除/双击打开，名称与网址必填、名称不可重复；保存后自动读取网页图标作为快捷方式图标
  （`CDSWebAppletPage` + `CDSWebAppletDlg` + `CDSWebAppletIconFetcher` + `CDSWebAppletStore`）；
  数据在 `configure/webapplets/webapplets.json` + `configure/webapplets/icons/`。
  双击打开的小程序窗口按网址复用 profile：属于内置站点预设时用它们的 profile
  （`configure/deepseek-web` 等，与原侧边栏按钮共用登录状态，不用重新登录），其它网址用
  `configure/webapplet-web`；按小程序名称去重。
- 导航栏顶部按钮：环境 / DSH 客户端 / 终端 / 网页小程序（原 DeepSeek / 今日头条 / GitHub-shgaol 三个按钮已移除）；
  **侧边栏启动时默认展开**（宽度 = 主窗口宽 1/6）。
- 导出 PDF 默认名 = `工作区名_会话名`。

- # 联系方式
- shgaol@126.com
- ## ❤️ 支持本项目

如果您觉得这个项目对您有帮助，欢迎请我喝杯咖啡：

| 微信 | 支付宝 |
| :---: | :---: |
|<img width="435" height="444" alt="878c5d80-c668-452b-ab84-ca8d1386b947" src="https://github.com/user-attachments/assets/5a6c2e93-e6b3-4e8b-a93c-327a5de719a1" />|<img width="358" height="370" alt="4f99919b-5fe6-42a2-9ba1-06e9b37d6048" src="https://github.com/user-attachments/assets/da31772f-dd43-4a43-a36b-a94bce6b2460" />
|

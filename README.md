# CameraMonitor 摄像头监控

一个 Windows 桌面工具，实时监控摄像头是否被任意程序占用，并在占用/停止时执行自定义脚本。支持定时退出、开机自启、键盘录制等功能。

## 功能

- **摄像头占用监控**：基于 Windows Media Foundation 的 `IMFSensorActivityMonitor`，实时检测任意程序对摄像头的占用。
- **脚本触发**：摄像头开始/停止占用时，执行用户定义的脚本动作，支持：
  - 执行 CMD 命令
  - 播放声音（可设置是否等待播放完毕）
  - 模拟按键（支持录制键盘组合，含 Win 键）
  - 弹出窗口
  - 自定义托盘通知
  - 等待、重复执行、条件判断
  - 退出程序
- **定时退出**：可设置多个时间段，程序在指定时间段内不运行，支持按星期选择。
- **开机自启**：注册到 `HKEY_CURRENT_USER\Software\Microsoft\Windows\CurrentVersion\Run`。
- **系统托盘**：程序启动后常驻托盘，右键菜单可打开设置、切换自启、退出。
- **设置保存**：所有配置保存到注册表 `HKEY_CURRENT_USER\Software\CameraMonitor`。

## 系统要求

- Windows 10 版本 1703 或更高
- 摄像头驱动正常

## 编译

1. 安装 Visual Studio 2022（含 C++ 桌面开发工作负载）
2. 打开 `CameraMonitor.slnx`
3. 平台选 `x64`，配置选 `Release`
4. 生成 → 生成解决方案

## 使用

1. 运行 `CameraMonitor.exe`，程序出现在系统托盘。
2. 右键托盘图标 → **设置**。
3. **摄像头页**：点“刷新列表”查看可用摄像头。
4. **脚本页**：为“开始占用”和“停止占用”分别添加动作。
   - 动作类型选“模拟按键”时，可从“单键”下拉框选择，或点“录制”按下键盘组合。
5. **定时与自启页**：添加“退出时间段”，选择星期，设置开机自启。
6. 点“保存”。

## 配置存储

所有设置保存在注册表：`HKEY_CURRENT_USER\Software\CameraMonitor`


## 许可证

MIT License

## 其他
本程序使用 DeepSeek 开发

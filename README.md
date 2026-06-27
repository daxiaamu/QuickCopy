# QuickCopy

轻量级 Windows 剪贴板快捷发送工具。

一键复制常用文本/链接到剪贴板。按钮宽度自适应内容，支持 DPI 缩放、GBK/UTF-8 自动识别。

## 功能

- 从 `links.json` 异步加载常用文本/链接，显示为按钮
- 点击按钮自动复制内容到剪贴板
- 按钮宽度自适应文字长度
- 鼠标悬停高亮效果
- 高 DPI 支持（微软雅黑 10pt）
- 单实例运行
- Ctrl+W 关闭窗口

## 编译

需要 MinGW-w64：

```bash
x86_64-w64-mingw32-gcc -O2 -mwindows -o QuickCopy.exe quick_copy.c json_helper.c -luser32 -lkernel32 -lgdi32 -lcomctl32 -lcomdlg32
```

## 使用

在 exe 同目录下放 `links.json`，运行 QuickCopy.exe 即可。

### links.json 格式

```json
[
    {
        "name": "仅提供远程服务",
        "content": "抱歉我这只提供远程服务，你联系我的页面上有事先说明。"
    },
    {
        "name": "修改UA",
        "content": "pan.baidu.com"
    },
    {
        "name": "Winrar",
        "content": "https://www.win-rar.com/fileadmin/winrar-versions/winrar/winrar-x64-712.exe"
    },
    {
        "name": "一加全能工具箱",
        "content": "https://api.optool.daxiaamu.com/optool/pctool_latest.php"
    },
    {
        "name": "打开USB调试",
        "content": "两个方法二选一：1）长按电源键打开小布对话，告诉它打开USB调试。2）手动方法：看视频的第20~45秒：https://www.bilibili.com/video/BV1gm421G7jB/"
    },
    {
        "name": "禁用更新",
        "content": "系统设置——应用管理——右上角显示系统应用，搜索找到【软件更新】——流量消耗——【禁用移动数据】打开、【禁用WLAN】打开、【可在后台使用数据】关闭。"
    }
]
```

支持 UTF-8 和 GBK 编码（自动检测）。可以放任意多条，按钮会自适应排列。

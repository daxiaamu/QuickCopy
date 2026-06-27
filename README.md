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

在 exe 同目录下放 `links.json`（格式见下方），运行 QuickCopy.exe 即可。

### links.json 格式

```json
[
    {
        "name": "按钮名称",
        "content": "点击后复制的内容"
    }
]
```

支持 UTF-8 和 GBK 编码（自动检测）。

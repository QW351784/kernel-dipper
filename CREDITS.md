# 致谢与借鉴来源 / Credits & Acknowledgements

本仓库（`kernel-dipper`，小米 8「dipper」的内核移植工作）建立在以下开源项目之上。
下面逐条写明**借鉴了谁、他的位置（仓库地址）、借鉴的具体内容，以及致谢**。

---

## 一、内核基线

### 1. Android Common Kernel（android17-6.18）
- **作者 / 位置**：https://github.com/aosp-mirror/kernel_common（分支 `android17-6.18`）
- **借鉴内容**：整棵内核源码基线。这是 Google 面向 Android 17 的 6.18 内核，带 Android 所需的 binder、vendor hook、KMI 等补丁；纯 `torvalds/linux` 6.18 不具备这些。
- **致谢**：感谢 Google 与 Android 内核团队。没有这条分支，"6.18 上跑 Android"这件事在版本与接口上根本对不上。

### 2. 上游 Linux（torvalds）
- **作者 / 位置**：https://github.com/torvalds/linux
- **借鉴内容**：6.18 的上游代码与全部 SDM845 平台支持（`sdm845.dtsi`、`camss`、`cci`、`stmfts.c`、`qcom_smbb` 等）。
- **致谢**：感谢 Linus 与全体内核贡献者，尤其是把 SDM845 各机型带上主线的那些开发者。

### 3. sdm845-mainline
- **作者 / 位置**：https://gitlab.com/sdm845-mainline/linux
- **借鉴内容**：小米 8（dipper）的设备树支持，来自该项目的 Merge Request #102（`.github/workflows/build.yml` 里的 `MR_DIFF`）。
- **致谢**：感谢 sdm845-mainline 项目与 MR 作者。dipper 的 DTS 主体是他们写的，本仓库只在其上补齐驱动节点（触摸、电池、显示供电）。

---

## 二、硬件驱动

### 4. imx363 相机驱动
- **作者 / 位置**：https://github.com/GH4NG/meizu-m1882-mainline-linux（`kernel/drivers/media/i2c/imx363.c`）
- **借鉴内容**：`sony,imx363` 的 mainline 版 V4L2 子设备驱动（1515 行），本仓库直接引入。
- **致谢**：感谢 GH4NG 与魅族 16（同样是 SDM845）mainline 项目的作者。小米 8 的主摄正是 IMX363，而 6.18 上游完全没有这颗 sensor 的驱动——这份驱动是目前唯一可用的来源。

### 5. 小米官方内核源码（dipper-q-oss）
- **作者 / 位置**：https://github.com/MiCode/Xiaomi_Kernel_OpenSource（分支 `dipper-q-oss`）
- **借鉴内容**：指纹驱动 `drivers/input/fingerprint/goodix/`（`gf_spi.c` / `netlink.c` / `platform.c`）；以及全部硬件连接参考（触摸 `fts@49`、指纹 GPIO、相机供电与 MCLK、`disp_vddio_vreg`、电池参数）。
- **致谢**：感谢小米公开这份 4.9 内核源码。本项目的引脚号、regulator 名、时钟配置，绝大多数是从这份源码里一条条挖出来的——没有它，6.18 上的 DTS 只能靠猜。

### 6. 显示面板驱动
- **位置**：本仓库 `drivers/gpu/drm/panel/panel-samsung-ea8074.c`（来源为 sdm845-mainline 项目）
- **借鉴内容**：三星 EA8074 面板的 DRM panel 驱动。
- **致谢**：感谢原作者，小米 8 的屏幕能亮起来靠它。

---

## 三、本项目自身

- **作者**：QW351784 · https://github.com/QW351784
- **本仓库**：https://github.com/QW351784/kernel-dipper
- **主要改动**：DTS 补齐触摸（`st,stmfts`）、电池（`simple-battery`）、显示供电（`disp_vddio_vreg`）；引入 imx363 相机驱动与 Goodix 指纹驱动
- **构建流程**：`.github/workflows/build.yml`

---

## 四、最后

以上每一位作者的工作都是公开可见的。如果这里漏了谁、或者署名写错了，请提 issue，我会立刻更正并补上致谢——**该谢的人一个都不能少**。

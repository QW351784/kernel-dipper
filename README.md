# kernel-dipper

Xiaomi Mi8 (dipper / SDM845) 的 mainline 6.18 编译仓库。

- 基础内核：torvalds/linux `v6.18`
- 设备支持：sdm845-mainline `sdm845/7.1-dev` 的 dipper MR 补丁
- 本仓库 `dts/` 下是修过 DTC 报错后的 DTS 文件，构建时覆盖到内核树

构建方式：GitHub Actions（`ubuntu-24.04` runner）。产物为 `sdm845-xiaomi-mi8-dipper.dtb`。

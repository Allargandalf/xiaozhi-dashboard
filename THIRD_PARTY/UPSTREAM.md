# 上游来源

`firmware/` 是 [78/xiaozhi-esp32](https://github.com/78/xiaozhi-esp32) 的完整源码副本，并在此仓库中直接维护，不是 Git submodule。

- 导入 commit：`d395220a83fb7a16ea5dc761c08a4816177cc880`
- 上游许可证：MIT，见项目根目录与 `firmware/LICENSE`
- 本项目改动：独立的立创实战派 Dashboard 板卡身份、设备端 Dashboard/MCP、配套电脑服务、测试与发布流程

更新上游基线时，应同时更新本文、`scripts/package_release.py` 的 `UPSTREAM_REVISION`、兼容性测试和发布说明。

# 大模型辅助使用记录

## 基本信息
- **模型名称**：例如以下开源/可验证模型
  - **OpenAI 系列**：gpt-5
- **提供方 / 访问方式**：
  - OpenAI API (api.openai.com), Azure OpenAI
- **使用日期**：2025年10月28日到10月30日
- **项目名称**：SHA-256 L1 算子优化

---

## 使用场景 1

### 主要用途
	1.代码重构 / 性能优化建议（HLS 架构级）
	2.HLS pragma 指导

### 完整 Prompt 内容
```
我们在 Vitis Libraries L1 的 SHA-256 优化中，当前测试路径 security/L1/tests/sha224_256/sha256/。
基线：Target=15ns, Estimated=13.846ns, Cosim Latency≈809（T_exec≈11201ns）。
目标：在保持接口与功能正确性的前提下，最小化执行时间（Estimated × Latency），并满足资源/时序约束。

现有实现存在：
- W 调度独立为 generateMsgSchedule（有 FIFO 握手与移位扇出开销）
- 64 轮压缩核单独消费 W 流（额外空拍，周期大）
- T1/T2 的组合路径较长（Σ/Ch/Maj 与多次加法串联）

请给出：
1) “融合内核 + 环形 wbuf[16] + II=1 + rewind”的重构建议与可直接覆盖的代码；
2) T1/T2 的加法树平衡与浅布尔表达式（Ch/Maj）来缩短关键路径；
3) 频点扫频建议（15→12→10→8ns），并说明如何计算 Slack 与执行时间。```

### 模型输出摘要
架构重构
	1.用融合内核：preProcessing（块流）→ fusedDigest（块内 64 轮），去掉 W 流与 dup 流。
	2.在 64 轮中按需计算 Wi：环形 wbuf[16]，wbuf/K/H/M 完全分割，RoundLoop II=1 + rewind。

### 人工审核与采纳情况
被采纳并应用到代码中的建议：
1.融合内核 + 环形 wbuf[16] + II=1 + rewind 
2.K/wbuf/H/M 完全分割 

过验证后未采纳的建议：
	两轮/拍（2-round/iter）低延迟模式
	原因：
　　周期下降但 Estimated升高，Slack 易负

二次验证或仿真测试：
csim/cosim 通过；后续在 compress 样例上获取 Estimated、Latency 等

---

## 使用场景 2

### 主要用途
1.调试协助：脚本/AP-1 & CSIM 错误定位
2.功能一致性修复（K 常量、pragma/return、ROTR18 缺失）

### 完整 Prompt 内容
```
Vitis HLS 报错：
- CSIM: no return statement / 一些旋转函数缺失 / golden 不一致

请根据日志定位：
1) 为什么会出现 no return（pragma 与 return 写在同一行），如何修复？
2) golden 不一致是否与 K 常量表错误有关？请列出需修正的项并给出可用代码。
```

### 模型输出摘要
1. CSIM 编译错误：pragma 与 return 同行会被前端吞掉；所有 pragma 独立一行，补齐 ROTR18。
2. golden 不一致：K 常量表错（如 0x4ed8aa4a），修正后 HMAC/SHA256 一致。
### 人工审核与采纳情况
被采纳并应用到代码中的建议：
	修复脚本名、规范 pragma 行、补齐 ROTR18、K 常量表校正。
二次验证或仿真测试：
	HMAC+SHA256 csim PASS，SHA-256 单测各档指标可复现。
---

## 总结

### 整体贡献度评估
大模型在本项目中的总体贡献占比：约 50%
	架构级建议：融合内核 + 环形缓冲 + II=1 + rewind
	调试与流程：AP-1/脚本、CSIM 编译、K 常量、频点扫频与指标对齐
人工介入与修正比例：约 50%
	细节实现、仿真对齐、资源/时序权衡、提交策略（违例扣分 vs Pass）

### 学习收获
	1.数据流融合 vs 模块拆分：独立 W 流会引入 FIFO/移位开销；融合+环缓可显著降周期。
	2.工程规范：pragma 独立行、K 常量校验、脚本名/路径全英文，能大幅减少“非算法”失败。

---

## 附注

- 请确保填写真实、完整的使用记录
- 如未使用大模型辅助，请在此文件中注明"本项目未使用大模型辅助"
- 评审方将参考此记录了解项目的独立性与创新性


# 大模型辅助使用记录

## 基本信息

- **模型名称**：例如以下开源/可验证模型
  - **OpenAI 系列**：gpt-5
- **提供方 / 访问方式**：
  - OpenAI API (api.openai.com), Azure OpenAI
- **使用日期**：2025年10月22日到2025年10月24日
- **项目名称**：Cholesky L1 算子优化

---

## 使用场景 1

### 主要用途
1.代码重构与架构优化（从Basic/Alt切换到Alt2）
2.HLS pragma 指导（PIPELINE、UNROLL、ARRAY_PARTITION）
3.功能/综合稳定性修复（解决 Pre-synthesis failed、csim fail）

### 完整 Prompt 内容
```
我们在 Vitis Libraries L1 的 Cholesky 优化中，当前测试路径 solver\L1\tests\cholesky\complex_fixed_arch0。
基线：Target=7ns, Estimated=6.276ns, Cosim Latency≈4919（T_exec≈30871.6ns）。
请你围绕 HLS 优化策略（PIPELINE、UNROLL、ARRAY_PARTITION、接口等）进行优化，目标：
- 切换到更高性能架构（Alt2）并保证可综合、可仿真；
- 保持原接口不变；
- 修复预综合失败（Pre-synthesis failed）；
- 标注关键 pragma 的放置位置和作用；
```

### 模型输出摘要
• 建议使用 Alt2（ARCH=2）作为高性能架构入口，顶层接口保持不变。
• 最内层 k-loop 强制 PIPELINE II=1；小数组（如累加数组、行平方和数组）使用 ARRAY_PARTITION complete 转寄存器，消除 BRAM 访存延迟。
• 引入“行方向并行 IU（i 维块化）”，避免在同一循环上同时使用 PIPELINE 与 UNROLL，防止 HLS 忽略/冲突。
• 顶层 I/O 使用“单层扁平循环 + II=1”读写，避免嵌套循环带来的额外开销。
• 去除易引发调度矛盾的 DEPENDENCE pragma、避免可变界限合并导致的调度困难。
• 指出 ap_fixed / complex<ap_fixed> 类型下资源风险，建议保守（必要时仍可强制 Alt2 测试）。

### 人工审核与采纳情况
被采纳并应用到代码中的建议：
1. 架构切换到 Alt2（ARCH=2，增加可选宏强制）；
2. k-loop 保持 II=1；
3. 小数组 complete 分区（product_sum_array / square_sum_array / diag_sum 等）；
4. 顶层 I/O 扁平化 + II=1；
5. 移除 DATAFLOW / DEPENDENCE 等冲突 pragma

未采纳的建议：
• 过度 UNROLL未采用；
• 对 ap_fixed 场景保持 ARCH=1 的保守特化。
   原因：
　　导致时序恶化/资源压力

二次验证或仿真测试：
csim/cosim 通过；后续在 compress 样例上获取 Estimated、Latency 等

---

## 使用场景 2

### 主要用途
1. 并行块化与数据路径重构，显著降低 Cosim Latency
2. 对角计算改为 rsqrt + 乘法，缩短关键路径

### 完整 Prompt 内容
```
　　在上一版可综合的 Alt2 基础上，继续降低 Cosim Latency（cycles）并保持 II=1。
要求：
- k 环 II=1；
- 沿 i 方向并行（一次处理多行），但避免与 PIPELINE 冲突；
- 正确维护每行对角累加（diag_sum），防止 sqrt 负数；
- 如果 sqrt 路径较慢，尝试 rsqrt + mul 替代；
- 给出完整可运行的 cholesky.hpp。```

### 模型输出摘要
• 在 traits 中新增 IU（行方向并行路数，默认 2）；
• Alt2 采用“i 按 IU 块化 + k 环 II=1”的结构：每拍处理 IU 个 i，避免 UNROLL/PIPELINE 冲突；
• 显式初始化并维护 diag_sum[ii] += |L[ii][j]|^2，下一次对角直接使用；
• 对角 L[j][j] 采用 rsqrt(real(A[j][j]-diag_sum[j])) × real(A...)；写入复数输出时虚部置 0；

### 人工审核与采纳情况
被采纳并应用到代码中的建议：
1. Alt2 行方向 IU 并行（初始 IU=2）+ k-loop II=1；
2. rsqrt + 乘法替代对角 sqrt/div；
3. 完全分区 psum/diag_sum 等小数组；
4. 预计算列顶部项（-conj(L[j][k])）以减轻 k-loop 组合路径。

未采纳的建议：
• 过度 UNROLL未采用；
• 对 ap_fixed 场景保持 ARCH=1 的保守特化。
   原因：
　　导致时序恶化/资源压力

二次验证或仿真测试：
　　csim/cosim 通过；Cosim Latency: 约 631 → 471 cycles；
---

## 总结

### 整体贡献度评估
大模型在本项目阶段的总体贡献占比：约50%
　　主要帮助领域：
1. 架构重构（Alt2 + IU 并行 + k-loop II=1）；
2. 对角路径优化（rsqrt + mul）；
3. 时序/Tcl 约束与综合策略（高努力、DSP 绑定、禁共享、双解对照）；
4. 文档化与实验策略制定。
人工介入与修正比例：约50%
　　编译/仿真循环、类型/器件落地细节、参数扫点与最终方案选择

### 学习收获
1. PIPELINE 与 UNROLL 不应同时作用于同一循环；采用“外层块化 + 内层流水线”结构更稳；
2. 将小型累加/状态数组 complete 分区能显著缩短关键路径（寄存器实现）；
3. 对角 sqrt 改为 rsqrt + 乘法往往更利于 Fmax；
4. Slack 扣分与 T_exec 的权衡：在评分公式下，需要同时优化 Estimated 与 Latency（而非单侧极端）。

---

## 附注

- 请确保填写真实、完整的使用记录
- 如未使用大模型辅助，请在此文件中注明"本项目未使用大模型辅助"
- 评审方将参考此记录了解项目的独立性与创新性


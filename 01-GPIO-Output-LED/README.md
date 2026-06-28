# 01 — GPIO输出：点亮板载 LED

## 📌 实验概述

配置 STM32F103C8T6 的 **PC13** 引脚为推挽输出模式，输出低电平以点亮板载 LED（Blue Pill 板 LED 为**低电平有效**）。

## 🔌 硬件连接

**最小系统板 Blue Pill (STM32F103C8T6)** — 纯板载外设实验，无需额外接线。

| 器件 | 对应引脚 | 说明 |
|------|---------|------|
| 板载 LED | PC13 | 低电平点亮（Active Low） |
| 调试器 | ST-Link V2 (SWD) | PA13(SWDIO)、PA14(SWCLK)、GND、3.3V |

> **注意**：PC13 在 GPIO 复位状态为浮空输入，此时 LED 不会亮；必须配置为推挽输出并输出低电平才能点亮。

## 🧩 核心代码

```c
#include "stm32f10x.h"                  // 标准外设库头文件

int main(void)
{
    // 1. 使能 GPIOC 端口的时钟
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOC, ENABLE);

    // 2. 配置 PC13 为推挽输出，50MHz 速率
    GPIO_InitTypeDef GPIO_Initstructure;
    GPIO_Initstructure.GPIO_Mode = GPIO_Mode_Out_PP;
    GPIO_Initstructure.GPIO_Pin = GPIO_Pin_13;
    GPIO_Initstructure.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(GPIOC, &GPIO_Initstructure);

    // 3. 输出低电平，点亮 LED
    GPIO_ResetBits(GPIOC, GPIO_Pin_13);

    while (1)
    {
        // 保持
    }
}
```

### 代码要点

| 行/段 | 说明 |
|-------|------|
| `RCC_APB2PeriphClockCmd` | **APB2 总线**上的外设时钟使能。GPIOC 挂载在 APB2 上，必须开启时钟才能读写其寄存器。 |
| `GPIO_Mode_Out_PP` | **推挽输出**（Push-Pull）：输出级由一对互补的 P-MOS 和 N-MOS 构成，可以主动输出高/低电平，驱动能力强。 |
| `GPIO_SetBits` / `GPIO_ResetBits` | 分别将指定引脚置为高电平 / 低电平。注意 Blue Pill 的板载 LED 是 **Active Low**，`ResetBits` = 亮。 |

## 💡 关键知识点

### 1. GPIO 推挽输出（Push-Pull Output）

- **原理**：输出寄存器通过一对互补的 MOSFET 驱动引脚。写 1 时上管导通、下管关闭，输出高电平；写 0 时相反，输出低电平。
- **特点**：可以主动输出高和低，相比开漏输出不需要外部上拉电阻。
- **面试易问**：推挽和开漏的区别是什么？什么时候用哪个？
- **易错点**：推挽输出不能直接将两个推挽输出引脚短接，否则一个输出高一个输出低时会短路。

### 2. RCC 时钟使能

- **为什么需要**：STM32 的外设时钟默认是关闭的（为了省电），读写外设寄存器前必须先开启对应时钟。
- **总线关系**：GPIOA/B/C 挂在 **APB2** 总线上，因此使用 `RCC_APB2PeriphClockCmd`。
- **面试易问**：如果不开启时钟直接操作 GPIO 寄存器会发生什么？→ 写操作无效，读返回默认值。

### 3. Blue Pill 板载 LED 的极性

- **Active Low**：GPIO 输出低电平时 LED 亮，高电平时灭。
- 这是因为 LED 正极通过限流电阻接 VCC（3.3V），负极接 PC13。
- **注意坑**：很多开发板（如 STM32F4 Discovery）的 LED 是 Active High，切换板型时要养成查原理图的习惯。

## 🧠 开发过程总结

### 🔧 问题：代码编译成功，但 LED 不亮

**现象**：Keil 编译 0 Error，烧录完成后 PC13 灯不亮。

**排查过程**：
1. 检查代码逻辑 → 配置和引脚没问题
2. 检查硬件 → 板子正常供电，ST-Link 正确连接
3. 发现勾选了 Keil 的 **Reset and Run** 但烧录后芯片未自动复位

**根因**：使用的 ST-Link V2（克隆版）的 SYSRESETREQ 信号未正确发出，导致芯片在烧录完成后仍处于复位状态，没有运行 main 函数。

**解决方案**：
- 方案一（立即解决）：取消 Reset and Run → 烧录完成后 **手动按一下板子的 Reset 按钮**
- 方案二（根治）：在 Keil 的 Debug → Settings → Reset 下拉中，从 `SYSRESETREQ` 改为 `Normal`

**启示**：硬件调试中，遇到"代码烧了但没反应"，先确认芯片是否真的在运行。最简单的判断：按下 Reset 键观察现象是否变化。

## 📎 结果展示

<!-- 插入硬件连接照片到 Hardware/ 目录，实验现象截图到 Results/ 目录 -->
<!-- 示例：
![烧录接线](Hardware/stlink-connection.jpg)
![LED点亮](Results/led-on.jpg)
-->

> **实验结果**：烧录代码后，按下 Reset 按钮，板载 PC13 LED 常亮红色 ✅

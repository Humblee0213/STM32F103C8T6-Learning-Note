# 6-1&6-2 TIM 定时中断 & 定时器外部时钟

> 本笔记基于江协科技 STM32 标准库课程 [6-2] 整理。由于无法直接获取视频完整字幕/画面，以下内容基于视频标题、课程上下文和 STM32 TIM 标准库知识编写。

## 📌 实验概述

本实验基于 STM32F103C8T6 标准外设库，通过配置 TIM2（通用定时器）实现两个核心功能：

1. **定时器定时中断**：配置 TIM2 以内部时钟（72MHz）驱动，通过设置预分频器 PSC 和自动重装载值 ARR，产生 1s 周期的更新中断，在中断服务函数中翻转 LED 电平，实现 LED 秒级闪烁。
2. **定时器外部时钟**：配置 TIM2 的 ETR（External Trigger）引脚（PA0）接收外部脉冲作为时钟源，每输入一个脉冲计数器加 1，计数到 ARR 阈值后触发更新中断，实现对外部事件的计数功能。

本节的核心目标是掌握 STM32 定时器的基本用法：

> 开启 TIM 时钟 → 配置时基单元（PSC + ARR）→ 选择时钟源 → 使能中断 → 配置 NVIC → 启动定时器 → 编写中断服务函数

## 🔌 硬件连接

以 STM32F103C8T6 最小系统板或江协科技配套实验板为例。

| 器件 | 对应引脚 | 说明 |
| --- | ---: | --- |
| LED1 | PA0 | 低电平点亮，用于定时中断翻转验证 |
| TIM2_ETR 输入 | PA0 | 外部时钟输入引脚（实验2），与 LED 共用同引脚需注意冲突 |
| ST-Link V2 | PA13、PA14、GND、3.3V | SWD 下载与调试 |

> 注意：实验1（定时中断）和实验2（外部时钟）**互斥**，因为 PA0 在实验1中作 GPIO 输出控制 LED，在实验2中作 ETR 输入。实际使用时每次只运行一个实验。

## 🗂️ 工程文件结构

```text
02-TIM-Interrupt-ExternalClock
├── Code
│   ├── main.c
│   └── Timer.c / Timer.h（模块化封装时建议拆分）
├── Hardware
│   └── TIM_Interrupt_ExternalClock.png
├── Results
│   ├── LED_Toggle_1s.gif
│   └── ETR_Counting.gif
└── README.md
```

## 🧩 核心代码

> 以下代码已按标准库规范做逐行注释。

```c
/**
  ******************************************************************************
  * @file    main.c
  * @brief   TIM2 定时中断 & 定时器外部时钟实验
  *          实验1：TIM2 产生 1s 定时中断，驱动 LED 交替闪烁
  *          实验2：TIM2 采用外部时钟（ETR 引脚）计数，到达阈值触发中断
  * @author  江协科技 STM32 标准库课程 [6-2]
  ******************************************************************************
  */

#include "stm32f10x.h"                  /* 包含 STM32F10x 标准外设库头文件 */

/**
  * @brief  定时器初始化（内部时钟 + 定时中断模式）
  * @note   定时周期 = (PSC+1) x (ARR+1) / TIM_CLK
  *         本实验 TIM_CLK = 72MHz，PSC = 7200-1，ARR = 10000-1
  *         中断频率 = 72MHz / 7200 / 10000 = 1Hz（1s 中断一次）
  * @retval 无
  */
void Timer_Init(void)
{
    /* 开启 TIM2 时钟（TIM2 挂载在 APB1 总线上） */
    RCC_APB1PeriphClockCmd(RCC_APB1Periph_TIM2, ENABLE);

    /* 选择内部时钟作为 TIM2 时钟源（默认即为内部时钟，该函数可省略） */
    TIM_InternalClockConfig(TIM2);

    /* 配置 TIM2 时基单元 */
    TIM_TimeBaseInitTypeDef TIM_TimeBaseInitStructure;          // 定义时基初始化结构体变量
    TIM_TimeBaseInitStructure.TIM_ClockDivision = TIM_CKD_DIV1; // 数字滤波器采样时钟分频，不分频
    TIM_TimeBaseInitStructure.TIM_CounterMode = TIM_CounterMode_Up;    // 计数器向上计数模式
    TIM_TimeBaseInitStructure.TIM_Period = 10000 - 1;           // ARR（自动重装载值），决定计数上限
    TIM_TimeBaseInitStructure.TIM_Prescaler = 7200 - 1;         // PSC（预分频器），决定计数步长
    TIM_TimeBaseInitStructure.TIM_RepetitionCounter = 0;        // 重复计数器（仅高级定时器有效）
    TIM_TimeBaseInit(TIM2, &TIM_TimeBaseInitStructure);         // 将配置写入 TIM2 时基寄存器

    /* 清除更新中断标志位，防止刚开启就进入中断 */
    TIM_ClearFlag(TIM2, TIM_FLAG_Update);

    /* 使能 TIM2 更新中断（UEV: Update Event） */
    TIM_ITConfig(TIM2, TIM_IT_Update, ENABLE);

    /* ========== 配置 NVIC 中断优先级 ========== */
    NVIC_PriorityGroupConfig(NVIC_PriorityGroup_2);             // 设置优先级分组为 2（2位抢占 + 2位响应）

    NVIC_InitTypeDef NVIC_InitStructure;                        // 定义 NVIC 初始化结构体变量
    NVIC_InitStructure.NVIC_IRQChannel = TIM2_IRQn;             // 指定中断通道为 TIM2
    NVIC_InitStructure.NVIC_IRQChannelCmd = ENABLE;             // 使能该中断通道
    NVIC_InitStructure.NVIC_IRQChannelPreemptionPriority = 1;   // 抢占优先级为 1
    NVIC_InitStructure.NVIC_IRQChannelSubPriority = 1;          // 响应优先级为 1
    NVIC_Init(&NVIC_InitStructure);                             // 将配置写入 NVIC 寄存器

    /* 启动 TIM2 计数器 */
    TIM_Cmd(TIM2, ENABLE);
}

/**
  * @brief  定时器外部时钟模式初始化（TIM2 ETR 引脚计数）
  * @note   使用 TIM2_ETR（PA0）引脚输入外部脉冲作为时钟源
  *         每来一个外部脉冲，TIM2 计数器加 1
  * @retval 无
  */
void Timer_ETR_Init(void)
{
    /* 开启 GPIOA 时钟（PA0 作为 TIM2_ETR 复用功能输入） */
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA, ENABLE);

    /* 开启 TIM2 时钟 */
    RCC_APB1PeriphClockCmd(RCC_APB1Periph_TIM2, ENABLE);

    /* 配置 PA0 为浮空输入模式（TIM2_ETR 复用功能） */
    GPIO_InitTypeDef GPIO_InitStructure;                        // 定义 GPIO 初始化结构体变量
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_IN_FLOATING;       // 浮空输入（ETR 外部时钟输入）
    GPIO_InitStructure.GPIO_Pin = GPIO_Pin_0;                   // 选择 PA0 引脚
    GPIO_Init(GPIOA, &GPIO_InitStructure);                      // 将配置写入 GPIOA 寄存器

    /* 配置 TIM2 ETR 外部时钟模式（模式2：ETR 引脚直接驱动计数器） */
    TIM_ETRClockMode2Config(TIM2,                              // TIM2
        TIM_ExtTRGPSC_OFF,                                     // 外部触发预分频器关闭
        TIM_ExtTRGPolarity_NonInverted,                        // 外部触发极性：上升沿有效
        0);                                                    // 数字滤波器配置（0 = 无滤波）

    /* 配置 TIM2 时基单元（外部时钟模式下 PSC 通常设为 0） */
    TIM_TimeBaseInitTypeDef TIM_TimeBaseInitStructure;          // 定义时基初始化结构体变量
    TIM_TimeBaseInitStructure.TIM_ClockDivision = TIM_CKD_DIV1; // 数字滤波器采样时钟分频，不分频
    TIM_TimeBaseInitStructure.TIM_CounterMode = TIM_CounterMode_Up;    // 计数器向上计数模式
    TIM_TimeBaseInitStructure.TIM_Period = 10 - 1;              // ARR = 9，每计数 10 次产生一次更新事件
    TIM_TimeBaseInitStructure.TIM_Prescaler = 1 - 1;            // PSC = 0，不分频
    TIM_TimeBaseInitStructure.TIM_RepetitionCounter = 0;        // 重复计数器（仅高级定时器有效）
    TIM_TimeBaseInit(TIM2, &TIM_TimeBaseInitStructure);         // 将配置写入 TIM2 时基寄存器

    /* 清除更新中断标志位 */
    TIM_ClearFlag(TIM2, TIM_FLAG_Update);

    /* 使能 TIM2 更新中断 */
    TIM_ITConfig(TIM2, TIM_IT_Update, ENABLE);

    /* 配置 TIM2 中断的 NVIC 优先级 */
    NVIC_PriorityGroupConfig(NVIC_PriorityGroup_2);

    NVIC_InitTypeDef NVIC_InitStructure;                        // 定义 NVIC 初始化结构体变量
    NVIC_InitStructure.NVIC_IRQChannel = TIM2_IRQn;             // 指定中断通道为 TIM2
    NVIC_InitStructure.NVIC_IRQChannelCmd = ENABLE;             // 使能该中断通道
    NVIC_InitStructure.NVIC_IRQChannelPreemptionPriority = 2;   // 抢占优先级为 2
    NVIC_InitStructure.NVIC_IRQChannelSubPriority = 1;          // 响应优先级为 1
    NVIC_Init(&NVIC_InitStructure);                             // 将配置写入 NVIC 寄存器

    /* 启动 TIM2 计数器 */
    TIM_Cmd(TIM2, ENABLE);
}

/**
  * @brief  LED GPIO 初始化
  * @note   配置 PA0 为推挽输出，初始状态为高电平（LED 灭）
  * @retval 无
  */
void LED_Init(void)
{
    /* 开启 GPIOA 时钟 */
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA, ENABLE);

    /* 配置 PA0 为推挽输出模式 */
    GPIO_InitTypeDef GPIO_InitStructure;                        // 定义 GPIO 初始化结构体变量
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_Out_PP;            // 推挽输出模式
    GPIO_InitStructure.GPIO_Pin = GPIO_Pin_0;                   // 选择 PA0 引脚
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;           // 输出速度为 50MHz
    GPIO_Init(GPIOA, &GPIO_InitStructure);                      // 将配置写入 GPIOA 寄存器

    /* 初始状态：关灯（推挽输出高电平，LED 熄灭） */
    GPIO_SetBits(GPIOA, GPIO_Pin_0);
}

/**
  * @brief  TIM2 中断服务函数
  * @note   定时器每产生一次更新事件（溢出），进入该函数一次
  *         在中断中翻转 PA0 输出电平，使 LED 按定时周期闪烁
  */
void TIM2_IRQHandler(void)
{
    /* 判断是否确实是 TIM2 更新中断触发了本次中断 */
    if (TIM_GetITStatus(TIM2, TIM_IT_Update) == SET)
    {
        /* 翻转 PA0 输出电平：亮的变灭，灭的变亮 */
        if (GPIO_ReadOutputDataBit(GPIOA, GPIO_Pin_0) == 0)     // 当前如果是低电平（LED 亮）
        {
            GPIO_SetBits(GPIOA, GPIO_Pin_0);                    // 输出高电平（LED 灭）
        }
        else                                                    // 当前如果是高电平（LED 灭）
        {
            GPIO_ResetBits(GPIOA, GPIO_Pin_0);                  // 输出低电平（LED 亮）
        }

        /* 清除 TIM2 更新中断标志位（必须，否则会不断触发中断） */
        TIM_ClearITPendingBit(TIM2, TIM_IT_Update);
    }
}

/**
  * @brief  主函数
  * @note   实验1：调用 Timer_Init() + LED_Init() 验证定时中断
  *         实验2：调用 Timer_ETR_Init() + LED_Init() 验证外部时钟计数
  *         两个实验互斥，使用时只保留一组初始化
  * @retval 无
  */
int main(void)
{
    /* ===== 实验1：TIM2 定时中断驱动 LED 闪烁 ===== */
    LED_Init();                                                 // 初始化 LED（PA0 推挽输出）
    Timer_Init();                                               // 初始化 TIM2 定时中断（1s 周期）

    /* ===== 实验2：定时器外部时钟计数 ===== */
    // LED_Init();                                              // 初始化 LED
    // Timer_ETR_Init();                                        // 初始化 TIM2 ETR 外部时钟模式

    /* 主循环：定时中断发生后自动跳转到 TIM2_IRQHandler */
    while (1)
    {
        /* 主循环可以处理其他任务，定时中断不受影响 */
    }
}
```

## 📊 代码要点

| 函数 / 段 | 说明 |
| --- | --- |
| RCC_APB1PeriphClockCmd(RCC_APB1Periph_TIM2, ENABLE) | 开启 TIM2 时钟。TIM2 挂载在 APB1 总线，**忘记开启时钟是 TIM 不工作的第一原因** |
| TIM_InternalClockConfig(TIM2) | 选择内部时钟（默认即可，此函数可省略，写出增强可读性） |
| TIM_Period = 10000 - 1 | ARR（自动重装载值），从 0 计数到该值溢出。减 1 是因为从 0 开始计数 |
| TIM_Prescaler = 7200 - 1 | PSC（预分频器），计数频率 = 72MHz / 7200 = 10kHz（0.1ms/步） |
| TIM_ClearFlag + TIM_ITConfig | 先清标志防误触发，再使能更新中断 |
| NVIC_PriorityGroupConfig(NVIC_PriorityGroup_2) | **整个项目只能调用一次**，一般放 main() 开头 |
| TIM_Cmd(TIM2, ENABLE) | 启动定时器。在所有配置完成后最后调用，**顺序不能错** |
| TIM2_IRQHandler | TIM2 中断服务函数名，必须与 startup 文件中的向量表名称一致 |
| TIM_GetITStatus | 判断中断来源。同一个 ISR 可能响应多个中断源 |
| TIM_ClearITPendingBit | **清除中断标志位**。ISR 结束前必须调用，否则 CPU 将反复进入中断 |
| GPIO_ReadOutputDataBit | 读取 ODR 寄存器值判断电平，实现翻转逻辑，无需额外变量 |
| TIM_ETRClockMode2Config | 配置 ETR 外部时钟模式2，将 PA0 脉冲作为计数时钟 |

## 💡 关键知识点

### 1. STM32 定时器（TIM）基本结构

#### 原理

STM32 的定时器本质是一个**计数器**，在时钟源驱动下不断累加，达到预设值后产生事件或中断。

定时器核心组成：

```text
时钟源 -> 预分频器（PSC）-> 计数器（CNT）-> 自动重装载寄存器（ARR）-> 更新事件 / 中断
```

STM32F103C8T6 的定时器分类：

| 类型 | 定时器 | 总线 | 特点 |
| --- | --- | --- | --- |
| 基本定时器 | TIM6、TIM7 | APB1 | 仅有定时功能，无外部 IO 引脚 |
| 通用定时器 | TIM2、TIM3、TIM4、TIM5 | APB1 | 定时 + 输入捕获 + 输出比较 + PWM |
| 高级定时器 | TIM1、TIM8 | APB2 | 通用定时器功能 + 互补输出 + 刹车功能 |

本实验使用 TIM2（通用定时器）。

#### 特点

- 16 位计数器，计数范围 0 ~ 65535
- 可配置向上计数、向下计数、中央对齐计数
- 支持多种时钟源：内部时钟、外部时钟模式1/2、编码器模式
- 具有 DMA 请求能力
- 通用定时器挂载在 APB1 总线，但定时器时钟可达 72MHz

#### 面试易问

**Q：STM32 定时器的三种类型有什么区别？**

A：基本定时器只有定时功能，无外部 IO。通用定时器增加输入捕获、输出比较、PWM 功能。高级定时器在通用定时器基础上增加互补输出和刹车功能，适合电机控制。

**Q：为什么 APB1 上的定时器可以跑到 72MHz？**

A：APB1 时钟最大为 36MHz，但只要 APB1 预分频系数不为 1，定时器时钟会被倍频。默认 APB1 预分频为 2，所以定时器时钟 = APB1 x 2 = 72MHz。

**Q：16 位计数器最大计数值是多少？**

A：16 位范围 0 ~ 65535，ARR 最大为 65535。更长定时周期需增大 PSC 或级联定时器。

#### 易错点

- 通用定时器在 APB1 总线，**不是 APB2**，用 RCC_APB1PeriphClockCmd()
- TIM1/TIM8 是高级定时器，挂载在 APB2 总线
- CNT 是 16 位，ARR 最大值不超 65535
- 混淆 PSC（分频）和 ARR（重装载）的作用

---

### 2. 定时中断配置流程

#### 原理

完整配置流程包括外设配置、NVIC 配置、ISR 编写三个层面：

```text
1. 开启 TIM 时钟 -> 2. 选择时钟源 -> 3. 配置 PSC 和 ARR
4. 使能更新中断 -> 5. 配置 NVIC 优先级 -> 6. 启动定时器
7. 编写中断服务函数 -> 8. 清除中断标志位
```

#### 特点

- ISR 由硬件触发，CPU 自动跳转到中断向量表指定地址执行
- ISR 执行完毕后自动返回主函数中断点
- 函数名由启动文件（startup_stm32f10x_md.s）定义，不可随意改动

#### 面试易问

**Q：定时中断和软件延时（Delay）实现 LED 闪烁有什么区别？（面试常问）**

A：软件延时是阻塞式，CPU 在延时期间什么都不能做。定时中断是非阻塞式，定时器独立运行，到达时间后触发中断，主循环可继续处理其他任务。**面试易问：项目用哪个？定时中断，因为实时性和 CPU 利用率更好。**

**Q：TIM_ClearFlag 和 TIM_ClearITPendingBit 有什么区别？**

A：`TIM_ClearFlag` 清状态寄存器标志位，用于初始化防误触发。`TIM_ClearITPendingBit` 清中断挂起位，在 ISR 中使用。ISR 中只清 Flag 不清 IT Pending 会导致不断进入中断。

#### 易错点

- ISR 函数名写错——**最常见坑**，与 startup 文件不一致
- ISR 中**没有清除中断标志位**→ CPU 卡死在中断中
- NVIC 使能漏配——TIM 中断使能了，但 NVIC 没使能，不会响应中断
- PSC 和 ARR 忘记减 1

---

### 3. PSC 和 ARR 的计算方法

#### 原理

```text
定时周期 T = (PSC + 1) x (ARR + 1) / TIM_CLK
中断频率 f = TIM_CLK / (PSC + 1) / (ARR + 1)
```

本实验参数：
TIM_CLK = 72MHz, PSC = 7200 - 1, ARR = 10000 - 1
计数频率 = 72MHz / 7200 = 10kHz（0.1ms 一步）
定时周期 = 10000 x 0.1ms = 1000ms = 1s

#### 特点

- PSC 决定计数精度，ARR 决定定时长度
- PSC 和 ARR 都是 16 位，最大值 65535
- 尽量让 ARR 接近整数值，方便调试

#### 面试易问

**Q：需要 1ms 定时中断，72MHz 时钟，PSC 和 ARR 怎么配？**

A：目标频率 1kHz。PSC = 72 - 1 = 71，计数频率 = 72MHz / 72 = 1MHz（1us 一步）。ARR = 1000 - 1 = 999，定时 = 1000 x 1us = 1ms。

**Q：PSC 和 ARR 为什么要减 1？**

A：PSC 和 ARR 从 0 开始计数。PSC = 7199 意味着从 0 计数到 7199，共 7200 个时钟周期。如果写 7200，实际计数步会多 1。

#### 易错点

- 忘记 PSC 和 ARR 减 1，实际周期与预期不符
- **PSC 决定速度，ARR 决定距离**
- 长周期时 ARR 超 65535，需增大 PSC
- 参数类型为 uint16_t，不能超过 65535

---

### 4. 定时器时钟源

#### 原理

STM32 通用定时器支持多种时钟源：

| 时钟源 | 来源 | 适用场景 |
| --- | --- | --- |
| 内部时钟（CK_INT） | 来自 RCC 的定时器时钟 | 普通定时（本实验使用） |
| 外部时钟模式1（TIx） | 外部 GPIO 引脚输入 | 测量外部信号频率 |
| 外部时钟模式2（ETR） | ETR 引脚输入 | 外部脉冲计数 |
| 内部触发（ITRx） | 其他定时器输出 | 定时器级联 |
| 编码器模式 | TI1/TI2 引脚 | 正交编码器解码 |

实验1用内部时钟，实验2用 ETR 外部时钟。

#### 特点

- 不同时钟源通过不同库函数配置，但时基单元配置一致
- 外部时钟模式下，定时器变成**脉冲计数器**
- ETR 引脚可配预分频和数字滤波，提高抗干扰能力

#### 面试易问

**Q：外部时钟模式1（TIx）和模式2（ETR）有什么区别？**

A：模式1使用 TIM 通道引脚（CH1~CH4），可配滤波器。模式2使用专门 ETR 引脚，配置更简洁。两者不可同时使用。

**Q：如何测量外部信号频率？**

A：两种方法：① 外部时钟模式，闸门时间内计数值即频率。② 输入捕获模式，测量两上升沿时间差取倒数。后者更精确，前者适合高频。

#### 易错点

- 外部时钟模式下 PSC 一般设为 0，否则计数失真
- ETR 引脚需先初始化 GPIO 为输入模式
- ETR 极性配错，计数方向与信号不符

---

### 5. NVIC 中断优先级

#### 原理

NVIC（Nested Vectored Interrupt Controller）是 Cortex-M3 内核的中断控制器。

| 分组 | 抢占位 | 响应位 | 说明 |
| --- | ---: | ---: | --- |
| PriorityGroup_0 | 0 | 4 | 无嵌套 |
| PriorityGroup_1 | 1 | 3 | 2 级抢占，8 级响应 |
| PriorityGroup_2 | 2 | 2 | 4 级抢占，4 级响应（本实验使用） |
| PriorityGroup_3 | 3 | 1 | 8 级抢占，2 级响应 |
| PriorityGroup_4 | 4 | 0 | 16 级抢占 |

> 数值越小，优先级越高。

#### 面试易问

**Q：抢占优先级和响应优先级有什么区别？**

A：抢占优先级决定能否打断另一个中断。响应优先级在多个中断同时请求时决定哪个先响应。抢占可嵌套，响应不能嵌套。

#### 易错点

- `NVIC_PriorityGroupConfig()` 只能调用一次
- 中断优先级数值越小越高——和直觉相反
- 中断必须在 NVIC 使能后才能响应

---

### 6. 中断服务函数（ISR）

#### 原理

ISR 是硬件中断触发后 CPU 自动跳转执行的函数。函数名必须与 startup 文件的向量表一致。TIM2 的向量名为 `TIM2_IRQHandler`。

ISR 标准模板：

```c
void TIM2_IRQHandler(void)
{
    if (TIM_GetITStatus(TIM2, TIM_IT_Update) == SET)  // 判断中断来源
    {
        // 用户代码
        TIM_ClearITPendingBit(TIM2, TIM_IT_Update);   // 清除中断标志位
    }
}
```

#### 面试易问

**Q：为什么必须清除中断标志位？（面试常问）**

A：不清除标志位，NVIC 认为中断请求仍有效，CPU 退出 ISR 后立即再次进入，导致卡死在 ISR 中。

**Q：ISR 中可以用 Delay 吗？**

A：技术上可以，但**强烈不建议**。ISR 应尽快执行，阻塞延时会影响其他中断响应。

**Q：什么是中断响应延迟？**

A：从中断请求到 CPU 执行 ISR 第一条指令的时间。Cortex-M3 通常为 12 个时钟周期。

#### 易错点

- ISR 函数名拼写错误——**最常见调试卡死原因**
- ISR 中未清除中断标志位——**第二常见原因**
- ISR 中复杂运算或浮点运算
- ISR 中访问主循环共享变量未加 volatile

---

### 7. ETR 外部时钟模式

#### 原理

ETR（External Trigger）是定时器特殊引脚，接收外部脉冲作为计数器时钟源。TIM2 的 ETR 引脚为 **PA0**。

配置流程：

```text
GPIO 输入模式 -> 开启 TIM 时钟
-> TIM_ETRClockMode2Config() 配置极性、分频、滤波
-> PSC + ARR -> 使能中断 + NVIC -> TIM_Cmd() 启动

#### 特点

- 定时器变成**可配置的脉冲计数器**
- ETR 引脚自带数字滤波器和预分频器
- 可配置上升沿或下降沿触发
- 适合测速传感器的脉冲计数
- 计数由硬件完成，不占 CPU 时间

#### 面试易问

**Q：ETR 和 EXTI 做脉冲计数有什么区别？**

A：EXTI 依赖 CPU 处理每个脉冲的中断，高频下 CPU 开销大。ETR 由硬件直接计数，不占 CPU 时间，计数频率更高。

**Q：ETR 输入的最大频率？**

A：理论上最大为 TIM 时钟的 1/4（约 18MHz），实际建议不超过 1/10。

#### 易错点

- ETR 引脚先配 GPIO 模式**再**配 TIM 外部时钟
- ETR 极性搞反
- 外部时钟模式下 PSC 设为 0
- 高频时需使能 ETR 预分频或滤波

---

### 8. 遗忘时钟——外设不工作的第一原因

#### 原理

STM32 所有外设时钟默认关闭。必须通过 RCC 手动开启。

```c
RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA, ENABLE);  // GPIOA (APB2)
RCC_APB1PeriphClockCmd(RCC_APB1Periph_TIM2, ENABLE);   // TIM2 (APB1)
```

| 总线 | 最高频率 | 常见外设 |
| --- | ---: | --- |
| APB2 | 72MHz | GPIO、AFIO、USART1、SPI1、TIM1、ADC |
| APB1 | 36MHz | TIM2~7、USART2~5、SPI2、I2C、PWR、BKP |

#### 面试易问

**Q：为什么外设时钟默认关闭？（面试常问）**

A：为了降低功耗。如果所有外设时钟默认都开启，功耗大幅增加。由程序员按需开启，是一种功耗管理策略。

**Q：忘记开启时钟会怎样？**

A：编译通过，下载正常，但外设完全不工作。**这是烧录后最容易踩的坑，且不报错不警告。**

#### 易错点

- GPIO（APB2）和 TIM（APB1）用不同的时钟开启函数
- 写了配置代码但没开时钟，外设不工作

---

## 🧠 本节核心记忆

```text
定时中断 = PSC（分频）+ ARR（重装载）+ NVIC（中断管理）+ ISR（服务函数）
```

```text
PSC 决定计数步长，ARR 决定计数上限
定时周期 = (PSC+1) x (ARR+1) / TIM_CLK
```

```text
通用定时器 → APB1 总线
高级定时器 → APB2 总线
```

```text
外部时钟模式2（ETR）：定时器 = 硬件脉冲计数器
```

```text
ISR 三要素：① 判断中断源 ② 执行用户代码 ③ 清除中断标志位
```

## 🧠 开发过程总结

### 问题 1：中断不触发，LED 不闪烁

现象：

- 程序编译通过，可以下载
- LED 保持常亮或常灭，没有闪烁

排查过程：

1. 检查 ISR 函数名是否与 startup 文件一致——写 `TIM2_IRQHandler`，不是 `TIM2_Handler` 等
2. 检查 NVIC 是否使能了 TIM2 中断通道
3. 检查 TIM_ITConfig() 是否已调用
4. 检查 TIM_Cmd() 是否启动定时器
5. 检查定时器时钟——TIM2 用 RCC_APB1PeriphClockCmd()

解决方案：

- 确认 startup 文件中 `TIM2_IRQHandler` 拼写正确
- 在 NVIC_Init 前调用 NVIC_PriorityGroupConfig()，且只调用一次
- 确保配置顺序：时钟 → 时基 → 中断 → NVIC → 启动

### 问题 2：定时周期不准

现象：

- LED 闪烁频率明显不是 1s（例如 0.5s 或 2s）

排查过程：

1. 检查系统时钟配置——72MHz 是否被修改
2. 检查 PSC 和 ARR 是否忘记减 1
3. 检查 APB1 预分频系数——若 APB1=36MHz，定时器时钟就是 36MHz

解决方案：

- 确认 `SystemInit()` 已在 main 开头调用
- 确认 PSC 和 ARR 公式计算正确
- 用逻辑分析仪或示波器测量实际波形

### 问题 3：外部时钟计数不准

现象：

- ETR 外部脉冲计数与预期不符

排查过程：

1. 检查 ETR 引脚极性——上升沿还是下降沿
2. 检查外部脉冲信号是否有抖动——需要加滤波或硬件去抖
3. 检查 ETR 预分频器是否误设
4. 检查 PA0 引脚是否被其他外设占用

解决方案：

- 用示波器观察 ETR 引脚信号
- 调整极性参数
- 开启 ETR 数字滤波器
- 两个实验共用 PA0，注意互斥

### 问题 4：ISR 中卡死

现象：

- 下载程序后，LED 状态变化一次后不再变化
- 程序似乎卡在某处

排查过程：

1. 检查 ISR 末尾是否调用 TIM_ClearITPendingBit()
2. 未清除标志位 → CPU 不断进入中断，无法执行主循环

解决方案：

- ISR 末尾加上 `TIM_ClearITPendingBit(TIM2, TIM_IT_Update);`
- 养成习惯：ISR 最后一步一定是清除相应中断标志位

## 📎 结果展示

> 实验 1：烧录程序并复位后，PA0 外接 LED 以 1s 周期（亮 1s，灭 1s）交替闪烁。✅

> 实验 2：从 TIM2_ETR（PA0）输入外部脉冲信号，每 10 个脉冲触发一次中断，LED 翻转一次。✅

> 注：由于 PA0 在两个实验中功能冲突（实验1 = 推挽输出，实验2 = ETR 输入），不能同时运行。实验2中如需观察计数，可将 LED 改接其他 GPIO（如 PB12）。

## 📝 本节小结

本节通过两个子实验，学习了 STM32 定时器的基本使用方法。

**实验1（定时中断）** 的核心价值在于掌握了通用定时器的完整配置流程——从时钟使能到时基计算，从 NVIC 配置到 ISR 编写。定时中断是嵌入式开发中最常用的非阻塞定时手段，后续的 PWM、输入捕获、编码器测速都建立在这个基础之上。

**实验2（外部时钟）** 展示了定时器的另一种重要用途——硬件脉冲计数器。ETR 外部时钟模式是学习编码器测速、霍尔传感器计数等应用的前置知识。

最重要的知识点是：

```text
定时周期 = (PSC + 1) x (ARR + 1) / 定时器时钟
```

后续课程中，定时器将作为核心外设出现在 PWM 驱动舵机/直流电机（P6-4）、TIM 输入捕获测频测占空比（P6-5/P6-6）、编码器接口测速（P6-7/P6-8）等章节中。

> 关联回顾：本节使用的 TIM2 中断系统，与 [01-GPIO-Output-LED] 中 GPIO 输出的"开启时钟→初始化→控制"模式相似。中断用法与后续 EXTI 章节一脉相承，区别在于 EXTI 由外部信号触发，TIM 中断由内部定时器触发。

# 16 — TIM输入捕获：输入捕获模式测频率与 PWMI 模式测频率占空比

> 视频对应：江协科技《STM32入门教程-2023版》P18，`[6-6] 输入捕获模式测频率 & PWMI模式测频率占空比`。
>
> 说明：当前无法直接取得视频的完整字幕与全部画面，以下笔记依据可确认的分集主题、课程前后文、STM32F103 定时器工作机制和标准外设库典型用法整理。核心代码属于贴合课程实验的**参考实现**，具体引脚与参数请以所用开发板原理图和课程资料为准。

## 实验概述

本节基于 STM32F103C8T6 的通用定时器输入捕获功能，完成两个实验：

1. 使用 TIM3_CH1 的普通输入捕获模式测量 PWM 信号频率。
2. 使用 TIM3 的 PWMI 模式同时测量 PWM 信号的频率和占空比。

为了便于验证，实验使用上一节的 TIM2_CH1 在 PA0 输出 PWM 波形，再通过杜邦线将 PA0 连接到 TIM3_CH1 对应的 PA6，由 TIM3 对该波形进行测量。

本节核心流程如下：

```text
TIM2 输出 PWM → PA0 与 PA6 连接 → TIM3 捕获输入边沿
→ CCR 锁存 CNT → 主从触发模式自动清零 CNT
→ 根据 CCR 计算频率和占空比 → OLED 显示结果
```

本节需要重点理解：

- 输入捕获不是“读取引脚当前电平”，而是在指定边沿到来时锁存计数器 CNT。
- 普通输入捕获使用一个通道测量相邻同类边沿之间的周期。
- PWMI 使用两个捕获通道观察同一个输入信号，分别得到周期和高电平时间。
- 主从触发模式可以让捕获和计数器复位由硬件自动完成，减少软件延迟和中断开销。
- 计数标准频率、16 位计数范围、滤波配置和输入信号质量共同决定测量范围与误差。

## 硬件连接

本节参考接线如下：

| 器件 / 信号 | STM32 引脚 | 说明 |
|---|---:|---|
| PWM 输出 | PA0 / TIM2_CH1 | TIM2 产生待测 PWM 信号 |
| 输入捕获 | PA6 / TIM3_CH1 | TIM3 捕获 PWM 的边沿 |
| PA0 → PA6 | 杜邦线直连 | 将输出信号送入输入捕获通道 |
| OLED SCL | PB8 | 沿用前面课程的软件 I²C OLED 模块 |
| OLED SDA | PB9 | 沿用前面课程的软件 I²C OLED 模块 |
| ST-Link SWDIO | PA13 | 程序下载与调试 |
| ST-Link SWCLK | PA14 | 程序下载与调试 |
| GND | GND | 外部信号源与 STM32 必须共地 |
| 3.3V | 3.3V | 给 OLED 等低压模块供电 |

> PA0 与 PA6 的连接是本实验最关键的一根信号线。  
> PWMI 中的通道 2 通过定时器内部的交叉输入映射捕获 PA6 上同一个信号，不需要把 PA7 再连接到 PWM 输出。

如果使用外部信号发生器替代 TIM2：

- 信号低电平建议为 0V。
- 信号高电平不得超过芯片引脚允许范围。
- 信号发生器 GND 必须与 STM32 GND 相连。
- 输入波形应为稳定方波，避免负压、过冲和超出容限的高电平。

## 工程文件结构

建议将两个实验分别保存，避免修改配置时相互干扰：

```text
16-TIM-Input-Capture
├── 01-Input-Capture-Frequency
│   ├── Code
│   │   ├── main.c
│   │   ├── PWM.c
│   │   ├── PWM.h
│   │   ├── IC.c
│   │   ├── IC.h
│   │   ├── OLED.c
│   │   └── OLED.h
│   ├── Hardware
│   │   └── PA0_to_PA6.png
│   ├── Results
│   │   └── Frequency_Measurement.gif
│   └── README.md
├── 02-PWMI-Frequency-Duty
│   ├── Code
│   │   ├── main.c
│   │   ├── PWM.c
│   │   ├── PWM.h
│   │   ├── IC.c
│   │   ├── IC.h
│   │   ├── OLED.c
│   │   └── OLED.h
│   ├── Hardware
│   │   └── PA0_to_PA6.png
│   ├── Results
│   │   └── PWMI_Measurement.gif
│   └── README.md
└── README.md
```

## 核心代码

> 以下代码采用 STM32F10x 标准外设库。OLED 驱动沿用前面课程工程，不在本节重复展开。  
> 为满足学习与复习需要，本章节所有非空 C 代码行都添加了中文行内注释。

### 1. PWM.h

```c
#ifndef __PWM_H                                      // 判断 PWM 头文件保护宏是否尚未定义，避免头文件被重复包含
#define __PWM_H                                      // 定义 PWM 头文件保护宏，使后续重复包含不会再次展开声明

#include "stm32f10x.h"                               // 引入 STM32F10x 芯片与标准外设库的类型、寄存器和函数声明

void PWM_Init(void);                                 // 声明 PWM 初始化函数，用于配置 PA0 和 TIM2_CH1 输出 PWM
void PWM_SetCompare1(uint16_t Compare);              // 声明占空比设置函数，通过修改 CCR1 改变高电平宽度
void PWM_SetPrescaler(uint16_t Prescaler);           // 声明预分频设置函数，通过修改 PSC 改变 PWM 输出频率

#endif                                               // 结束 PWM 头文件保护条件编译
```

### 2. PWM.c

```c
#include "stm32f10x.h"                               // 引入 STM32F10x 标准外设库接口，供 GPIO、RCC 和 TIM 配置使用
#include "PWM.h"                                     // 引入本模块函数声明，保证函数定义与外部声明保持一致

void PWM_Init(void)                                  // 定义 PWM 初始化函数，建立 TIM2_CH1 到 PA0 的 PWM 输出通路
{                                                    // 进入 PWM_Init 函数体
    RCC_APB1PeriphClockCmd(RCC_APB1Periph_TIM2, ENABLE); // 开启 TIM2 的 APB1 外设时钟，否则定时器寄存器不会工作
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA, ENABLE); // 开启 GPIOA 的 APB2 外设时钟，为配置 PA0 做准备

    GPIO_InitTypeDef GPIO_InitStructure;             // 定义 GPIO 初始化结构体，用于集中设置 PA0 的工作模式和速度
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_AF_PP;  // 将 PA0 配置为复用推挽输出，让 TIM2_CH1 接管引脚并主动输出高低电平
    GPIO_InitStructure.GPIO_Pin = GPIO_Pin_0;        // 选择 GPIOA 的第 0 号引脚，即默认映射的 TIM2_CH1
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz; // 设置 GPIO 最大翻转速度为 50MHz，满足 PWM 边沿输出需求
    GPIO_Init(GPIOA, &GPIO_InitStructure);           // 将上述配置写入 GPIOA 配置寄存器，完成 PA0 初始化

    TIM_InternalClockConfig(TIM2);                   // 选择 TIM2 内部时钟作为计数时钟，时钟来自 APB1 定时器时钟域

    TIM_TimeBaseInitTypeDef TIM_TimeBaseInitStructure; // 定义时基初始化结构体，用于配置 PSC、ARR 和计数模式
    TIM_TimeBaseStructInit(&TIM_TimeBaseInitStructure); // 先写入库提供的默认值，避免结构体未赋值字段包含随机数据
    TIM_TimeBaseInitStructure.TIM_ClockDivision = TIM_CKD_DIV1; // 设置采样时钟不分频，本实验不额外降低数字滤波时钟
    TIM_TimeBaseInitStructure.TIM_CounterMode = TIM_CounterMode_Up; // 设置向上计数模式，使 CNT 从 0 递增到 ARR
    TIM_TimeBaseInitStructure.TIM_Period = 100 - 1;  // 设置自动重装值为 99，使一个 PWM 周期包含 100 个计数单位
    TIM_TimeBaseInitStructure.TIM_Prescaler = 720 - 1; // 设置预分频值为 719，将 72MHz 定时器时钟分频为 100kHz
    TIM_TimeBaseInitStructure.TIM_RepetitionCounter = 0; // 通用定时器不使用重复计数功能，此处保持为 0
    TIM_TimeBaseInit(TIM2, &TIM_TimeBaseInitStructure); // 将时基参数写入 TIM2，使默认 PWM 频率为 100kHz/100=1kHz

    TIM_OCInitTypeDef TIM_OCInitStructure;           // 定义输出比较初始化结构体，用于配置 TIM2_CH1 的 PWM 行为
    TIM_OCStructInit(&TIM_OCInitStructure);          // 先加载输出比较默认配置，避免未赋值字段造成异常输出
    TIM_OCInitStructure.TIM_OCMode = TIM_OCMode_PWM1; // 选择 PWM 模式 1，向上计数时 CNT 小于 CCR1 期间输出有效电平
    TIM_OCInitStructure.TIM_OCPolarity = TIM_OCPolarity_High; // 设置有效电平为高电平，使 CCR1 对应高电平持续时间
    TIM_OCInitStructure.TIM_OutputState = TIM_OutputState_Enable; // 使能通道 1 输出，将比较结果送往 PA0 引脚
    TIM_OCInitStructure.TIM_Pulse = 50;              // 设置初始 CCR1 为 50，在 100 个计数周期中得到约 50% 占空比
    TIM_OC1Init(TIM2, &TIM_OCInitStructure);         // 将 PWM 配置写入 TIM2 通道 1 的相关寄存器

    TIM_OC1PreloadConfig(TIM2, TIM_OCPreload_Enable); // 使能 CCR1 预装载，避免更新占空比时在周期中途产生毛刺
    TIM_ARRPreloadConfig(TIM2, ENABLE);              // 使能 ARR 预装载，使周期参数在更新事件时同步生效
    TIM_Cmd(TIM2, ENABLE);                           // 使能 TIM2 计数器，开始在 PA0 连续输出 PWM 波形
}                                                    // 结束 PWM_Init 函数体

void PWM_SetCompare1(uint16_t Compare)               // 定义 PWM 占空比设置函数，Compare 对应本周期中的有效计数数
{                                                    // 进入 PWM_SetCompare1 函数体
    if (Compare > 100)                               // 判断用户给定的比较值是否超过当前 ARR 对应的最大范围
    {                                                // 进入比较值上限保护分支
        Compare = 100;                               // 将过大的比较值限制为 100，避免接口语义超出 0%～100%
    }                                                // 结束比较值上限保护分支
    TIM_SetCompare1(TIM2, Compare);                  // 将比较值写入 TIM2_CCR1，从而改变 PA0 的 PWM 占空比
}                                                    // 结束 PWM_SetCompare1 函数体

void PWM_SetPrescaler(uint16_t Prescaler)            // 定义 PWM 预分频设置函数，参数直接对应 PSC 寄存器值
{                                                    // 进入 PWM_SetPrescaler 函数体
    TIM_PrescalerConfig(TIM2, Prescaler, TIM_PSCReloadMode_Immediate); // 立即更新 TIM2 预分频器，从而实时改变 PWM 频率
}                                                    // 结束 PWM_SetPrescaler 函数体
```

默认参数下：

```text
TIM2 计数频率 = 72 MHz ÷ (719 + 1) = 100 kHz
PWM 频率 = 100 kHz ÷ (99 + 1) = 1 kHz
PWM 占空比 = CCR1 ÷ (ARR + 1) = 50 ÷ 100 = 50%
```

---

### 实验一：普通输入捕获模式测频率

### 3. IC.h

```c
#ifndef __IC_H                                       // 判断输入捕获头文件保护宏是否尚未定义，防止重复声明
#define __IC_H                                       // 定义输入捕获头文件保护宏，确保头文件内容只展开一次

#include "stm32f10x.h"                               // 引入 STM32F10x 类型定义和标准外设库函数声明

void IC_Init(void);                                  // 声明普通输入捕获初始化函数，用于配置 TIM3_CH1 测量周期
uint32_t IC_GetFreq(void);                           // 声明频率读取函数，根据 CCR1 捕获值计算输入信号频率
uint16_t IC_GetCapture(void);                        // 声明原始捕获值读取函数，便于观察一个周期包含的计数数

#endif                                               // 结束输入捕获头文件保护条件编译
```

### 4. IC.c

```c
#include "stm32f10x.h"                               // 引入 STM32F10x 标准外设库接口，供 RCC、GPIO 和 TIM 配置使用
#include "IC.h"                                      // 引入本输入捕获模块的函数声明，保证接口定义一致

#define IC_COUNTER_FREQ 1000000UL                    // 定义 TIM3 的目标计数频率为 1MHz，即每个计数代表 1微秒

void IC_Init(void)                                   // 定义普通输入捕获初始化函数，使用 TIM3_CH1 测量相邻上升沿周期
{                                                    // 进入 IC_Init 函数体
    RCC_APB1PeriphClockCmd(RCC_APB1Periph_TIM3, ENABLE); // 开启 TIM3 的 APB1 外设时钟，使输入捕获电路可以工作
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA, ENABLE); // 开启 GPIOA 的 APB2 外设时钟，为配置 PA6 输入做准备

    GPIO_InitTypeDef GPIO_InitStructure;             // 定义 GPIO 初始化结构体，用于设置 PA6 的输入模式
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_IPU;    // 将 PA6 配置为上拉输入，避免输入线悬空时出现随机跳变
    GPIO_InitStructure.GPIO_Pin = GPIO_Pin_6;        // 选择 PA6，该引脚默认对应 TIM3_CH1 输入
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz; // 填写 GPIO 速度字段以保持结构体完整，输入模式下该字段影响很小
    GPIO_Init(GPIOA, &GPIO_InitStructure);           // 将配置写入 GPIOA，使 PA6 成为稳定的数字输入引脚

    TIM_InternalClockConfig(TIM3);                   // 选择 TIM3 内部时钟驱动 CNT，让计数器充当测周法的时间基准

    TIM_TimeBaseInitTypeDef TIM_TimeBaseInitStructure; // 定义时基初始化结构体，用于配置 TIM3 计数标准频率和量程
    TIM_TimeBaseStructInit(&TIM_TimeBaseInitStructure); // 加载默认值，确保所有时基字段都处于已知状态
    TIM_TimeBaseInitStructure.TIM_ClockDivision = TIM_CKD_DIV1; // 设置数字滤波采样时钟不额外分频
    TIM_TimeBaseInitStructure.TIM_CounterMode = TIM_CounterMode_Up; // 设置 CNT 从 0 向上递增，用于累计两个边沿间时间
    TIM_TimeBaseInitStructure.TIM_Period = 65536 - 1; // 将 ARR 设置为 65535，充分利用 16 位计数器的最大单周期量程
    TIM_TimeBaseInitStructure.TIM_Prescaler = 72 - 1; // 将 72MHz 定时器时钟分频为 1MHz，使每个计数对应 1微秒
    TIM_TimeBaseInitStructure.TIM_RepetitionCounter = 0; // 通用定时器不使用重复计数功能，因此保持为 0
    TIM_TimeBaseInit(TIM3, &TIM_TimeBaseInitStructure); // 将时基参数写入 TIM3，建立 1MHz 的测量基准

    TIM_ICInitTypeDef TIM_ICInitStructure;           // 定义输入捕获初始化结构体，用于设置捕获边沿、映射和滤波
    TIM_ICStructInit(&TIM_ICInitStructure);          // 加载输入捕获默认值，避免未赋值字段导致通道配置异常
    TIM_ICInitStructure.TIM_Channel = TIM_Channel_1; // 选择 TIM3 通道 1，其外部输入引脚为 PA6
    TIM_ICInitStructure.TIM_ICFilter = 0x0F;         // 设置较强数字滤波，抑制输入信号上的短毛刺和抖动
    TIM_ICInitStructure.TIM_ICPolarity = TIM_ICPolarity_Rising; // 选择上升沿触发捕获，用相邻上升沿测量完整周期
    TIM_ICInitStructure.TIM_ICPrescaler = TIM_ICPSC_DIV1; // 输入捕获不分频，每个有效上升沿都参与捕获
    TIM_ICInitStructure.TIM_ICSelection = TIM_ICSelection_DirectTI; // 将 IC1 直接连接到 TI1，也就是 PA6 输入路径
    TIM_ICInit(TIM3, &TIM_ICInitStructure);          // 将上述参数写入 TIM3_CH1 输入捕获寄存器

    TIM_SelectInputTrigger(TIM3, TIM_TS_TI1FP1);     // 选择滤波后的 TI1FP1 作为从模式控制器的触发输入
    TIM_SelectSlaveMode(TIM3, TIM_SlaveMode_Reset);  // 选择复位从模式，使每次上升沿捕获后硬件自动将 CNT 清零
    TIM_SetCounter(TIM3, 0);                         // 在启动前手动清零 CNT，避免第一次捕获包含初始化前的残余计数
    TIM_Cmd(TIM3, ENABLE);                           // 使能 TIM3，开始以 1MHz 计数并等待 PA6 上升沿
}                                                    // 结束 IC_Init 函数体

uint16_t IC_GetCapture(void)                         // 定义原始周期计数读取函数，返回 CCR1 中锁存的计数值
{                                                    // 进入 IC_GetCapture 函数体
    return TIM_GetCapture1(TIM3);                    // 读取 TIM3_CCR1，得到相邻上升沿之间累计的计数数
}                                                    // 结束 IC_GetCapture 函数体

uint32_t IC_GetFreq(void)                            // 定义输入频率计算函数，依据测周法公式 f=fc/N
{                                                    // 进入 IC_GetFreq 函数体
    uint32_t CaptureValue;                           // 定义 32 位变量保存 CCR1，便于后续安全参与除法运算
    CaptureValue = TIM_GetCapture1(TIM3);            // 读取一个完整输入周期对应的计数数 N
    if (CaptureValue == 0)                           // 判断捕获值是否为 0，避免发生除以 0 的运行错误
    {                                                // 进入无有效捕获值处理分支
        return 0;                                    // 返回 0 表示当前尚未获得可用于计算的有效周期
    }                                                // 结束无有效捕获值处理分支
    return IC_COUNTER_FREQ / CaptureValue;           // 用 1MHz 标准计数频率除以周期计数值，得到输入频率 Hz
}                                                    // 结束 IC_GetFreq 函数体
```

### 5. 普通输入捕获实验 main.c

```c
#include "stm32f10x.h"                               // 引入 STM32F10x 芯片定义和标准外设库接口
#include "OLED.h"                                    // 引入 OLED 显示模块接口，用于显示设定值和测量结果
#include "PWM.h"                                     // 引入 PWM 输出模块接口，用于在 PA0 产生待测信号
#include "IC.h"                                      // 引入普通输入捕获模块接口，用于读取 PA6 输入频率

int main(void)                                       // 定义主函数，程序复位后从此处开始执行
{                                                    // 进入 main 函数体
    OLED_Init();                                     // 初始化 OLED，建立数据显示界面
    PWM_Init();                                      // 初始化 TIM2_CH1，使 PA0 输出默认 1kHz、50% 占空比 PWM
    IC_Init();                                       // 初始化 TIM3_CH1，使 PA6 以测周法捕获 PWM 周期

    PWM_SetCompare1(50);                             // 设置 CCR1 为 50，使 TIM2 输出约 50% 占空比
    PWM_SetPrescaler(720 - 1);                       // 设置 PSC 为 719，使 TIM2 在 ARR=99 时输出约 1kHz PWM

    OLED_ShowString(1, 1, "Freq:");                  // 在第一行显示频率标签，为实时测量值预留位置
    OLED_ShowString(2, 1, "CCR1:");                  // 在第二行显示捕获寄存器标签，便于观察原始周期计数
    OLED_ShowString(3, 1, "Duty:50%");               // 在第三行显示当前 PWM 设定占空比，方便与输出配置核对

    while (1)                                        // 进入主循环，持续刷新测量结果
    {                                                // 进入 while 循环体
        OLED_ShowNum(1, 6, IC_GetFreq(), 6);         // 读取并显示输入信号频率，单位为 Hz
        OLED_ShowNum(2, 6, IC_GetCapture(), 5);      // 读取并显示 CCR1 原始计数值，验证 f=1MHz/CCR1
    }                                                // 结束 while 循环体并返回循环开头
}                                                    // 结束 main 函数，嵌入式程序正常情况下不会执行到函数外
```

普通输入捕获的计算关系：

```text
TIM3 标准计数频率 fc = 72 MHz ÷ (71 + 1) = 1 MHz
相邻上升沿之间的计数值 N = CCR1
输入周期 T = N ÷ fc
输入频率 f = fc ÷ N
```

当输入约为 1kHz 时：

```text
CCR1 ≈ 1000
f ≈ 1,000,000 ÷ 1000 = 1000 Hz
```

---

### 实验二：PWMI 模式测量频率和占空比

### 6. PWMI 版 IC.h

```c
#ifndef __IC_H                                       // 判断 PWMI 输入捕获头文件保护宏是否尚未定义
#define __IC_H                                       // 定义头文件保护宏，避免函数声明被重复展开

#include "stm32f10x.h"                               // 引入 STM32F10x 类型定义和标准外设库接口

void IC_Init(void);                                  // 声明 PWMI 初始化函数，用两个捕获通道测量同一个 PWM 输入
uint32_t IC_GetFreq(void);                           // 声明频率读取函数，根据 CCR1 中的周期计数计算频率
uint16_t IC_GetDuty(void);                           // 声明占空比读取函数，根据 CCR2 与 CCR1 的比例计算百分比
uint16_t IC_GetPeriodCapture(void);                  // 声明周期捕获值读取函数，返回 CCR1
uint16_t IC_GetHighCapture(void);                    // 声明高电平捕获值读取函数，返回 CCR2

#endif                                               // 结束 PWMI 输入捕获头文件保护条件编译
```

### 7. PWMI 版 IC.c

```c
#include "stm32f10x.h"                               // 引入 STM32F10x 标准外设库接口，供定时器输入捕获配置使用
#include "IC.h"                                      // 引入本模块函数声明，保证实现与接口保持一致

#define IC_COUNTER_FREQ 1000000UL                    // 定义 TIM3 计数标准频率为 1MHz，每个计数对应 1微秒

void IC_Init(void)                                   // 定义 PWMI 初始化函数，同时获得 PWM 周期和高电平持续时间
{                                                    // 进入 IC_Init 函数体
    RCC_APB1PeriphClockCmd(RCC_APB1Periph_TIM3, ENABLE); // 开启 TIM3 时钟，为时基、捕获通道和从模式控制器供时
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA, ENABLE); // 开启 GPIOA 时钟，为 PA6 输入配置提供时钟

    GPIO_InitTypeDef GPIO_InitStructure;             // 定义 GPIO 初始化结构体，用于配置 TIM3_CH1 的输入引脚
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_IPU;    // 将 PA6 配置为上拉输入，降低断线或无信号时的悬空干扰
    GPIO_InitStructure.GPIO_Pin = GPIO_Pin_6;        // 选择 PA6，作为 TIM3 的 TI1 输入来源
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz; // 填写 GPIO 速度字段以保持初始化结构体完整
    GPIO_Init(GPIOA, &GPIO_InitStructure);           // 将 PA6 输入配置写入 GPIOA 寄存器

    TIM_InternalClockConfig(TIM3);                   // 选择内部时钟驱动 TIM3_CNT，为测周与测脉宽提供时间基准

    TIM_TimeBaseInitTypeDef TIM_TimeBaseInitStructure; // 定义 TIM3 时基初始化结构体
    TIM_TimeBaseStructInit(&TIM_TimeBaseInitStructure); // 加载默认值，避免结构体残留未初始化字段
    TIM_TimeBaseInitStructure.TIM_ClockDivision = TIM_CKD_DIV1; // 设置数字滤波器使用未额外分频的定时器采样时钟
    TIM_TimeBaseInitStructure.TIM_CounterMode = TIM_CounterMode_Up; // 设置向上计数，使 CNT 表示从基准边沿起经过的时间
    TIM_TimeBaseInitStructure.TIM_Period = 65536 - 1; // 设置 16 位最大自动重装值，扩大低频信号的可测周期范围
    TIM_TimeBaseInitStructure.TIM_Prescaler = 72 - 1; // 将 72MHz 分频为 1MHz，简化频率与脉宽计算
    TIM_TimeBaseInitStructure.TIM_RepetitionCounter = 0; // 通用定时器不使用重复计数器，保持该字段为 0
    TIM_TimeBaseInit(TIM3, &TIM_TimeBaseInitStructure); // 将时基参数写入 TIM3，建立 1微秒计数分辨率

    TIM_ICInitTypeDef TIM_ICInitStructure;           // 定义输入捕获结构体，先描述主捕获通道的配置
    TIM_ICStructInit(&TIM_ICInitStructure);          // 加载输入捕获默认值，保证所有字段都有确定内容
    TIM_ICInitStructure.TIM_Channel = TIM_Channel_1; // 选择通道 1 作为主通道，由 CCR1 记录完整 PWM 周期
    TIM_ICInitStructure.TIM_ICFilter = 0x0F;         // 启用较强数字滤波，减少输入短毛刺造成的错误捕获
    TIM_ICInitStructure.TIM_ICPolarity = TIM_ICPolarity_Rising; // 令通道 1 在上升沿捕获，以相邻上升沿确定周期
    TIM_ICInitStructure.TIM_ICPrescaler = TIM_ICPSC_DIV1; // 不对输入边沿分频，使每个 PWM 周期都能被测量
    TIM_ICInitStructure.TIM_ICSelection = TIM_ICSelection_DirectTI; // 将 IC1 直连 TI1，也就是 PA6 上的输入波形
    TIM_PWMIConfig(TIM3, &TIM_ICInitStructure);      // 配置 PWMI 通道对，同时生成反向极性和交叉映射的通道 2

    TIM_SelectInputTrigger(TIM3, TIM_TS_TI1FP1);     // 选择 TI1FP1 上升沿作为从模式触发源和周期起点
    TIM_SelectSlaveMode(TIM3, TIM_SlaveMode_Reset);  // 选择复位模式，使每次周期起点到来时硬件自动清零 CNT
    TIM_SetCounter(TIM3, 0);                         // 启动前清零 CNT，避免第一次读数包含初始化阶段残余计数
    TIM_Cmd(TIM3, ENABLE);                           // 使能 TIM3，开始自动捕获周期与高电平持续时间
}                                                    // 结束 IC_Init 函数体

uint16_t IC_GetPeriodCapture(void)                   // 定义周期捕获值读取函数，返回一个完整 PWM 周期的计数数
{                                                    // 进入 IC_GetPeriodCapture 函数体
    return TIM_GetCapture1(TIM3);                    // 读取 CCR1，得到相邻上升沿之间的周期计数值
}                                                    // 结束 IC_GetPeriodCapture 函数体

uint16_t IC_GetHighCapture(void)                     // 定义高电平捕获值读取函数，返回上升沿到下降沿的计数数
{                                                    // 进入 IC_GetHighCapture 函数体
    return TIM_GetCapture2(TIM3);                    // 读取 CCR2，得到当前 PWM 高电平持续时间的计数值
}                                                    // 结束 IC_GetHighCapture 函数体

uint32_t IC_GetFreq(void)                            // 定义频率计算函数，依据 CCR1 中的完整周期计数求频率
{                                                    // 进入 IC_GetFreq 函数体
    uint32_t PeriodCapture;                          // 定义 32 位变量保存周期计数，便于后续执行安全除法
    PeriodCapture = TIM_GetCapture1(TIM3);           // 读取 CCR1，获取一个完整 PWM 周期对应的计数数
    if (PeriodCapture == 0)                          // 判断是否尚未获得有效周期，防止除以 0
    {                                                // 进入无有效周期处理分支
        return 0;                                    // 返回 0，表示当前频率无法计算或输入信号尚未稳定
    }                                                // 结束无有效周期处理分支
    return IC_COUNTER_FREQ / PeriodCapture;          // 用 1MHz 计数基准除以周期计数，得到输入频率 Hz
}                                                    // 结束 IC_GetFreq 函数体

uint16_t IC_GetDuty(void)                            // 定义占空比计算函数，返回 0～100 的整数百分比
{                                                    // 进入 IC_GetDuty 函数体
    uint32_t PeriodCapture;                          // 定义变量保存 CCR1 中的完整周期计数
    uint32_t HighCapture;                            // 定义变量保存 CCR2 中的高电平持续计数
    PeriodCapture = TIM_GetCapture1(TIM3);           // 读取 CCR1，获得 PWM 周期长度
    HighCapture = TIM_GetCapture2(TIM3);             // 读取 CCR2，获得 PWM 高电平持续时间
    if (PeriodCapture == 0)                          // 判断周期值是否有效，避免占空比计算出现除以 0
    {                                                // 进入无有效周期处理分支
        return 0;                                    // 返回 0，表示当前没有可靠的占空比结果
    }                                                // 结束无有效周期处理分支
    if (HighCapture > PeriodCapture)                 // 检查高电平计数是否异常大于完整周期计数
    {                                                // 进入异常捕获保护分支
        return 0;                                    // 返回 0，提示本次捕获可能受毛刺或更新不同步影响
    }                                                // 结束异常捕获保护分支
    return (uint16_t)(HighCapture * 100UL / PeriodCapture); // 按高电平时间除以周期时间计算整数百分比占空比
}                                                    // 结束 IC_GetDuty 函数体
```

`TIM_PWMIConfig()` 根据主通道配置自动建立一对捕获通道。以上升沿直连通道 1 为例：

```text
TIM3_CH1 / IC1：上升沿捕获，CCR1 保存完整周期
TIM3_CH2 / IC2：下降沿捕获，CCR2 保存高电平持续时间
触发源 TI1FP1：每次上升沿触发从模式复位 CNT
```

这里的通道 2 使用内部交叉映射观察 TI1 输入，因此外部仍然只需要把信号接到 PA6。

### 8. PWMI 实验 main.c

```c
#include "stm32f10x.h"                               // 引入 STM32F10x 芯片定义与标准外设库函数
#include "OLED.h"                                    // 引入 OLED 显示接口，用于实时展示频率和占空比
#include "PWM.h"                                     // 引入 PWM 输出接口，用于产生可调的测试信号
#include "IC.h"                                      // 引入 PWMI 输入捕获接口，用于读取测量结果

int main(void)                                       // 定义主函数，系统启动后从此处执行
{                                                    // 进入 main 函数体
    OLED_Init();                                     // 初始化 OLED 显示屏，准备显示设定值与实测值
    PWM_Init();                                      // 初始化 TIM2_CH1，在 PA0 建立 PWM 输出
    IC_Init();                                       // 初始化 TIM3 PWMI 模式，在 PA6 捕获频率和占空比

    PWM_SetPrescaler(720 - 1);                       // 设置 TIM2 预分频器，使默认输出频率约为 1kHz
    PWM_SetCompare1(30);                             // 设置 CCR1 为 30，使输出 PWM 占空比约为 30%

    OLED_ShowString(1, 1, "Freq:");                  // 在第一行显示频率标签
    OLED_ShowString(2, 1, "Duty:");                  // 在第二行显示占空比标签
    OLED_ShowString(2, 11, "%");                     // 在占空比数值后显示百分号
    OLED_ShowString(3, 1, "Period:");                // 在第三行显示完整周期捕获值标签
    OLED_ShowString(4, 1, "High:");                  // 在第四行显示高电平捕获值标签

    while (1)                                        // 进入无限循环，持续刷新 PWMI 测量数据
    {                                                // 进入 while 循环体
        OLED_ShowNum(1, 6, IC_GetFreq(), 6);         // 读取并显示 PWM 输入频率，单位为 Hz
        OLED_ShowNum(2, 6, IC_GetDuty(), 3);         // 读取并显示 PWM 输入占空比，范围约为 0～100
        OLED_ShowNum(3, 8, IC_GetPeriodCapture(), 5); // 显示 CCR1 周期计数，便于验证频率计算公式
        OLED_ShowNum(4, 8, IC_GetHighCapture(), 5);  // 显示 CCR2 高电平计数，便于验证占空比计算公式
    }                                                // 结束 while 循环体并回到循环开头
}                                                    // 结束 main 函数，嵌入式系统正常运行时不会返回
```

当输出频率约为 1kHz、占空比约为 30% 时，理论结果为：

```text
CCR1 ≈ 1000
CCR2 ≈ 300
频率 ≈ 1,000,000 ÷ 1000 = 1000 Hz
占空比 ≈ 300 ÷ 1000 × 100% = 30%
```

## 代码要点

| 行 / 函数 | 说明 |
|---|---|
| `GPIO_Mode_AF_PP` | PWM 输出引脚使用复用推挽模式，由定时器外设驱动 PA0。 |
| `GPIO_Mode_IPU` | 输入捕获引脚使用上拉输入，避免断线或无信号时悬空；实际也可根据外部电路选择浮空输入。 |
| `TIM_TimeBaseInit()` | 配置计数标准频率与最大计数范围。测量公式中的 `fc` 由定时器时钟和 PSC 决定。 |
| `TIM_ICInit()` | 配置单个普通输入捕获通道，适合测量周期或单一边沿间隔。 |
| `TIM_PWMIConfig()` | 根据主通道参数配置一对互补捕获通道，用于同时测量周期和高电平时间。 |
| `TIM_ICPolarity_Rising` | 选择上升沿为周期边界，相邻上升沿之间构成一个完整周期。 |
| `TIM_ICSelection_DirectTI` | 将捕获通道直接连接到同编号的定时器输入信号。 |
| `TIM_ICPSC_DIV1` | 每一个有效边沿都捕获；也可配置为每 2、4、8 个边沿捕获一次。 |
| `TIM_ICFilter` | 对输入边沿进行数字滤波。数值越大通常抗毛刺能力越强，但也会增加响应延迟并限制最高输入频率。 |
| `TIM_TS_TI1FP1` | 选择滤波和极性处理后的通道 1 输入作为从模式触发信号。 |
| `TIM_SlaveMode_Reset` | 每次触发到来时硬件自动复位 CNT，使 CCR1 直接表示一个周期的计数值。 |
| `TIM_GetCapture1()` | 读取 CCR1。普通输入捕获中为周期；本节 PWMI 配置中也作为周期。 |
| `TIM_GetCapture2()` | 读取 CCR2。本节 PWMI 配置中表示从上升沿到下降沿的高电平持续时间。 |
| `IC_COUNTER_FREQ / CCR1` | 测周法计算频率；必须先判断 CCR1 是否为 0。 |
| `CCR2 * 100 / CCR1` | 计算高电平占空比；整数运算会舍弃小数部分。 |
| `TIM_OC1PreloadConfig()` | 让 PWM 比较值在更新事件同步生效，减少输出毛刺。 |
| `TIM_PrescalerConfig()` | 改变 PWM 输出频率；参数是写入 PSC 的寄存器值，而不是直接填写分频倍数。 |

## 关键知识点

### 1. 输入捕获的本质

#### 原理

输入捕获 IC（Input Capture）的核心动作是：

> 当定时器输入通道检测到指定边沿时，把当前 CNT 的数值锁存到对应 CCR 寄存器。

定时器 CNT 按固定标准频率持续计数，输入捕获电路负责在边沿到来的准确时刻保存计数值。由此可以测量：

- 相邻同类边沿之间的周期。
- 两个不同边沿之间的高电平或低电平持续时间。
- 脉冲间隔。
- PWM 的频率和占空比。

输入捕获与普通 GPIO 读取的区别在于：

- GPIO 读取获得的是“当前是高电平还是低电平”。
- 输入捕获得到的是“边沿发生时计数器走到了多少”。

#### 特点

- 捕获动作由硬件完成，时间精度通常高于软件轮询。
- 每个通用定时器通常提供多个捕获 / 比较通道。
- 输入捕获与输出比较共享通道引脚和 CCR 资源。
- 可结合数字滤波、边沿选择、输入分频和从模式控制器。
- 可用于测速、测频、超声波测距、遥控脉宽解码和通信时序测量。

#### 面试易问

**Q：输入捕获时 CCR 中保存的是什么？**

A：保存指定输入边沿到来瞬间 CNT 的当前值。它不是直接保存频率或时间，软件需要结合计数标准频率进行换算。

**Q：输入捕获和输出比较有什么区别？**

A：输入捕获在外部边沿到来时把 CNT 锁存到 CCR；输出比较则持续比较 CNT 与 CCR，在二者满足比较条件时改变输出或产生事件。

**Q：为什么输入捕获比 GPIO 轮询测脉宽更准确？**

A：捕获由定时器硬件在边沿时刻直接完成，不依赖主循环执行时机，避免了软件轮询延迟和代码路径抖动。

#### 易错点

- 误以为 CCR 直接保存频率。
- 只配置 GPIO 输入，没有配置定时器捕获通道。
- 将输入捕获引脚接到错误的定时器通道。
- 同一通道已经用于 PWM 输出，又试图同时用作输入捕获。
- 忽略输入信号的电压范围、共地和波形质量。

---

### 2. 测频法与测周法

#### 原理

频率测量常见两种思路：

**测频法：**

在固定闸门时间 `T` 内统计输入脉冲数 `N`：

```text
f = N / T
```

**测周法：**

使用标准计数频率 `fc`，统计一个输入周期内的计数值 `N`：

```text
T = N / fc
f = fc / N
```

本节输入捕获实验使用测周法。TIM3 以 1MHz 计数，相邻上升沿之间的计数值被锁存到 CCR1。

#### 特点

- 测频法更适合较高频率，固定时间内可累计较多脉冲。
- 测周法更适合较低频率，一个周期内可累计较多标准计数。
- 测周法的误差通常体现为约 ±1 个计数单位的量化误差。
- 计数标准频率越高，时间分辨率越高，但可测最低频率越高。
- 计数标准频率越低，低频量程更大，但高频测量分辨率下降。

#### 面试易问

**Q：高频信号适合测频法还是测周法？**

A：通常高频信号更适合测频法，因为固定闸门时间内能累计大量脉冲，相对误差更小。

**Q：低频信号为什么更适合测周法？**

A：低频信号周期长，在一个周期内可以累计很多标准时钟计数，因此周期分辨率较高；如果用短闸门测频，可能只能统计到很少的脉冲。

**Q：什么是中界频率？**

A：它是测频法与测周法误差大致相等的分界点。实际仪表常根据输入频率自动切换测量方法，以获得更小相对误差。

#### 易错点

- 不区分测频法与测周法，直接套用错误公式。
- 计算时忘记 PSC 和定时器实际输入时钟。
- 将 PSC 寄存器值误当成实际分频系数，忽略 `PSC + 1`。
- 输入频率太低导致 16 位 CNT 在下一个边沿前溢出。
- 输入频率太高导致一个周期只有很少计数，量化误差明显。

---

### 3. 主从触发模式与自动复位

#### 原理

如果只使用普通输入捕获，边沿到来时 CCR 能锁存 CNT，但测量下一个周期前还需要重新确定计数起点。

TIM 的从模式控制器可以把 `TI1FP1` 选为触发输入，并选择 Reset 模式：

```text
上升沿到来
├── CCR1 锁存当前 CNT
└── 从模式控制器将 CNT 复位为 0
```

这样每次捕获值就是上一个上升沿到当前上升沿之间的计数数，整个过程由硬件自动执行。

#### 特点

- 不需要在中断服务函数中手动清零 CNT。
- 避免中断响应延迟引入额外周期误差。
- 捕获与复位由同一硬件触发事件协调完成。
- CPU 只需在需要时读取 CCR，无须处理每一个输入周期。
- 是 TIM 输入捕获和 PWMI 实现自动测量的关键机制。

#### 面试易问

**Q：为什么不在输入捕获中断里手动执行 `TIM_SetCounter(TIMx, 0)`？**

A：中断存在响应延迟，边沿发生到软件清零之间 CNT 仍会继续计数，会把不确定延迟加入下一次测量。Reset 从模式由硬件在触发时完成，精度和一致性更好。

**Q：输入触发源为什么选择 `TI1FP1` 而不是直接写 GPIO？**

A：从模式控制器接收的是定时器内部触发信号。`TI1FP1` 是外部 TI1 信号经过输入选择、滤波和极性处理后的有效触发路径。

#### 易错点

- 配置了 `TIM_SelectSlaveMode()`，但没有选择正确的输入触发源。
- 选择了错误的通道输入，例如 PA6 接入却选择 TI2FP2。
- 误以为必须开启捕获中断才能使用从模式复位。
- 在硬件已自动复位 CNT 的情况下又在软件中清零，造成逻辑混乱。
- 忽略第一次捕获可能尚未形成完整周期，启动初期读数可能短暂异常。

---

### 4. 普通输入捕获测频率

#### 原理

普通输入捕获实验只配置 TIM3_CH1：

- PA6 输入 PWM。
- 通道 1 捕获上升沿。
- CCR1 保存相邻上升沿之间的计数值。
- 上升沿同时触发 Reset 从模式清零 CNT。

若 TIM3 计数频率为 1MHz：

```text
频率 = 1,000,000 / CCR1
```

例如 CCR1 为 2000：

```text
周期 = 2000 μs = 2 ms
频率 = 1 / 2 ms = 500 Hz
```

#### 特点

- 只占用一个捕获通道。
- 代码和硬件结构简单。
- 可以测量频率或周期。
- 不能仅凭一个同极性捕获通道直接得到占空比。
- 适合只关心周期、转速或脉冲间隔的场景。

#### 面试易问

**Q：普通输入捕获为什么不能直接得到占空比？**

A：它只记录相邻同类边沿之间的完整周期，没有记录上升沿到下降沿之间的高电平时间。要测占空比，还需要另一个捕获事件或切换捕获极性。

**Q：为什么使用上升沿捕获而不是下降沿？**

A：两者都可以测周期，只要连续捕获相同类型边沿即可。课程通常选择上升沿作为直观的周期起点。

#### 易错点

- `TIM_ICSelection` 配置成间接输入，导致 CCR1 没有捕获预期的 PA6 边沿。
- 频率计算函数未处理 CCR1 为 0。
- 用 16 位变量执行高频率计算，导致结果范围不足。
- OLED 刷新时没有固定显示宽度，旧数字残留。
- PA0 和 PA6 未连接，导致捕获值不更新。

---

### 5. PWMI 模式的双通道捕获

#### 原理

PWMI（PWM Input）模式使用一对捕获通道测量同一个输入：

- 通道 1 直连 TI1，在上升沿捕获，CCR1 得到完整周期。
- 通道 2 间接连接 TI1，在下降沿捕获，CCR2 得到高电平时间。
- 上升沿同时触发 Reset 从模式，将 CNT 清零。

时序关系如下：

```text
第一个上升沿：CNT 清零，开始一个新周期
下降沿：CCR2 ← CNT，记录高电平时间
下一个上升沿：CCR1 ← CNT，记录完整周期；随后 CNT 再次清零
```

计算公式：

```text
频率 = fc / CCR1
占空比 = CCR2 / CCR1 × 100%
```

#### 特点

- 可以自动同时得到频率与占空比。
- 两个捕获通道共享同一个外部输入信号。
- 第二通道通过内部交叉映射，不要求额外输入引脚接线。
- CPU 只需读取 CCR1 和 CCR2。
- 标准外设库的 `TIM_PWMIConfig()` 可快速完成配对配置。

#### 面试易问

**Q：PWMI 为什么要使用两个通道？**

A：一个通道测完整周期，另一个通道测高电平或低电平持续时间。只有同时获得这两个时间量，才能计算占空比。

**Q：PWMI 的通道 2 是否需要把信号再接到另一个 GPIO？**

A：本节配置不需要。通道 2 通过定时器内部间接输入映射连接到 TI1，外部信号仍然只接 PA6。

**Q：`TIM_PWMIConfig()` 做了什么？**

A：它以用户给出的主通道配置为基础，配置另一个配对通道的相反捕获极性和间接输入映射，从而形成适合 PWM 输入测量的双通道结构。

#### 易错点

- 把 CCR1 和 CCR2 的含义写反。
- 占空比公式分子分母颠倒。
- 误把通道 2 当成必须接 PA7 的独立外部输入。
- 主通道边沿选择改变后，没有同步理解周期和有效时间的含义。
- 在输入接近 0% 或 100% 占空比时，某个边沿可能很难稳定捕获。

---

### 6. 输入滤波、极性与捕获分频

#### 原理

输入信号进入捕获单元前，通常经过：

```text
GPIO 输入 → 输入选择 → 数字滤波 → 边沿检测 → 捕获分频 → CCR 锁存
```

相关参数包括：

- `TIM_ICPolarity`：选择上升沿或下降沿。
- `TIM_ICSelection`：选择直连输入或交叉输入。
- `TIM_ICFilter`：要求输入稳定若干采样后才认定为有效边沿。
- `TIM_ICPrescaler`：每 1、2、4 或 8 个有效边沿产生一次捕获。

#### 特点

- 滤波可抑制机械抖动、线路干扰和窄脉冲毛刺。
- 强滤波会增加边沿确认延迟，并降低可可靠识别的最高频率。
- 捕获分频适合边沿频率过高或只需稀疏采样的场景。
- 极性配置决定捕获的是上升沿、下降沿以及占空比中的哪一段。
- 直连与交叉映射是 PWMI 双通道观察同一输入的基础。

#### 面试易问

**Q：输入捕获滤波器是不是模拟低通滤波器？**

A：不是。它是定时器内部的数字采样滤波逻辑，只有输入在连续若干次采样中保持稳定，才认为出现有效跳变。

**Q：输入捕获分频会不会改变 CNT 的计数频率？**

A：不会。它只决定每隔多少个有效输入边沿执行一次捕获，CNT 的标准计数频率仍由定时器时钟和 PSC 决定。

#### 易错点

- 滤波值设置过大，导致高频输入边沿被漏掉。
- 输入信号很干净却盲目使用最大滤波，增加不必要延迟。
- 将 `TIM_ICPSC_DIV2` 误认为把定时器计数时钟除以 2。
- 选择了下降沿，却仍按上升沿开始的高电平时间解释 CCR。
- 输入信号边沿缓慢或有严重振铃，仅靠数字滤波仍无法可靠测量。

---

### 7. 测量范围、分辨率与误差

#### 原理

本实验将 TIM3 设置为：

```text
计数频率 fc = 1 MHz
计数器位宽 = 16 位
最大周期计数 = 65535
```

不考虑滤波和硬件限制时，单次不溢出的最低可测频率约为：

```text
f_min ≈ 1,000,000 / 65535 ≈ 15.26 Hz
```

高频端理论上周期至少需要若干个计数才能具有可用分辨率。即使计数器能够记录 `CCR1 = 1`，该结果的相对量化误差也很大。

测周法的典型量化误差可近似理解为 ±1 个计数：

```text
相对误差约为 1 / N
```

其中 `N` 为一个周期内的计数数。

#### 特点

- 提高计数频率可以改善高频测量的时间分辨率。
- 降低计数频率可以扩大低频信号的不溢出量程。
- 16 位定时器对低频测量存在天然上限。
- 可通过溢出计数、级联定时器或使用 32 位定时器扩大量程。
- 输入时钟误差会直接影响最终频率测量准确度。

#### 面试易问

**Q：为什么 1MHz 计数时最低频率约为 15Hz？**

A：16 位 CNT 最多记录约 65535 个计数。一个周期如果超过约 65.535ms，CNT 会在下一个同类边沿到来前溢出，因此对应频率约为 1/65.535ms，即 15.26Hz。

**Q：如何测量更低频率？**

A：可以降低 CNT 标准频率、统计定时器溢出次数、使用 32 位定时器，或改用固定闸门时间的测频法。

**Q：为什么高频时测周法误差增大？**

A：输入周期越短，一个周期内的标准计数 N 越小，±1 个计数造成的相对误差 `1/N` 就越大。

#### 易错点

- 只关注理论最大频率，忽略输入同步、滤波和边沿质量限制。
- 认为提高计数频率只会提高精度，不会缩小低频量程。
- 输入频率低于量程时仍直接读取 CCR，得到溢出后的错误结果。
- 使用内部 RC 时钟却期待高精度频率计测量。
- 忽略整数除法带来的小数截断。

---

### 8. 输入捕获与中断的关系

#### 原理

输入捕获事件可以选择产生中断，但本节通过主从触发模式已经能自动完成：

- 边沿检测。
- CNT 锁存到 CCR。
- CNT 复位。

因此，如果主循环只需要周期性读取最新测量结果，可以不为每个捕获事件开启中断。

当需要以下功能时，可考虑使用捕获中断：

- 记录每次捕获的时间序列。
- 检测信号丢失或超时。
- 对捕获结果进行软件滤波或统计。
- 测量不规则脉冲序列。
- 处理 CNT 溢出并扩展低频量程。

#### 特点

- 不开中断时，CPU 开销低，CCR 始终保存最近一次捕获结果。
- 开中断后可以逐事件处理，但高频输入可能造成频繁中断。
- 主从模式与捕获中断可以同时使用，二者作用不同。
- 对稳定周期 PWM 的简单测量通常无需每周期进入 ISR。
- 超时检测可结合更新中断或系统时基实现。

#### 面试易问

**Q：输入捕获是否必须开启中断？**

A：不必须。捕获与 CCR 锁存是硬件功能，不开中断也会执行。软件可以在主循环中读取 CCR。

**Q：不开中断如何知道输入信号已经停止？**

A：仅反复读取 CCR 可能一直得到最后一次有效值，因此需要额外的超时机制，例如定时检查捕获标志、记录最后捕获时间或使用更新溢出中断判断无新边沿。

#### 易错点

- 认为不开中断 CCR 就不会更新。
- 信号停止后仍把最后一次 CCR 结果当作当前有效频率。
- 输入频率很高时为每个边沿开启中断，造成 CPU 占用过高。
- 在 ISR 中执行 OLED 刷新、长延时或复杂浮点运算。
- 开启中断后忘记清除对应捕获标志位。

---

## 本节核心记忆

```text
输入捕获：边沿到来时，CCR ← CNT
```

```text
普通测频：
CCR1 = 一个完整周期的计数值
频率 = 计数标准频率 / CCR1
```

```text
PWMI：
CCR1 = 完整周期
CCR2 = 高电平时间
频率 = fc / CCR1
占空比 = CCR2 / CCR1 × 100%
```

```text
主从触发 Reset 模式：
捕获周期的同时由硬件自动清零 CNT
避免软件中断清零带来的不确定延迟
```

```text
1MHz + 16位计数器：
时间分辨率约 1μs
不溢出的最低频率约 15.26Hz
```

```text
PA0 输出 PWM → 一根线连接 PA6
PWMI 通道2使用内部交叉映射，不需要再接 PA7
```

## 开发过程总结

### 问题 1：OLED 频率一直显示 0

现象：

- PWM 输出程序已经运行。
- OLED 上的频率始终为 0。
- CCR1 也一直为 0。

排查过程：

1. 检查 PA0 是否确实配置为 TIM2_CH1 复用推挽输出。
2. 检查 PA0 与 PA6 是否使用杜邦线连接。
3. 检查 TIM3_CH1 是否对应当前芯片默认映射的 PA6。
4. 检查 TIM2 和 TIM3 的 RCC 时钟是否开启。
5. 用示波器或逻辑分析仪确认 PA0 是否有 PWM 波形。
6. 检查 `TIM_Cmd(TIM3, ENABLE)` 是否执行。

解决方案：

- 正确连接 PA0 与 PA6。
- 确认没有启用改变 TIM3 引脚位置的错误重映射。
- 使用普通 GPIO 翻转或示波器先验证 PWM 输出，再调试输入捕获。
- 在 Keil 调试器中观察 `TIM3->CCR1` 是否更新。

---

### 问题 2：测得频率与设定值相差固定倍数

现象：

- 设定输出为 1kHz，显示结果却为 500Hz、2kHz 或其他固定比例。
- CCR1 有稳定数值，但公式计算结果不正确。

排查过程：

1. 检查系统时钟是否真的是 72MHz。
2. 检查 APB1 分频以及定时器时钟倍频规则。
3. 检查 TIM3 的 PSC 是否为 `72 - 1`。
4. 检查公式中使用的是分频后计数频率，而不是直接使用 72MHz。
5. 检查捕获预分频是否误设为 DIV2、DIV4 或 DIV8。
6. 检查 PWM 的 PSC 和 ARR 计算是否都包含 `+1`。

解决方案：

- 统一使用实际 TIM3 计数频率作为 `IC_COUNTER_FREQ`。
- 按 `f = f_TIM / (PSC + 1) / (ARR + 1)` 重新核对 PWM 输出频率。
- 将输入捕获预分频恢复为 `TIM_ICPSC_DIV1`。
- 在代码中用宏集中定义计数标准频率，减少魔法数字。

---

### 问题 3：PWMI 的占空比显示异常或大于 100%

现象：

- 频率显示基本正确。
- 占空比跳变、为 0 或出现大于 100% 的异常结果。
- CCR2 偶尔大于 CCR1。

排查过程：

1. 检查 `TIM_PWMIConfig()` 的主通道是否为 TIM_Channel_1。
2. 检查主通道是否选择上升沿、直连 TI1。
3. 检查触发源是否为 `TIM_TS_TI1FP1`。
4. 检查从模式是否为 `TIM_SlaveMode_Reset`。
5. 检查占空比公式是否为 `CCR2 / CCR1`。
6. 检查输入波形是否有毛刺、振铃或不稳定边沿。
7. 检查读取 CCR1 和 CCR2 时是否恰逢新一周期更新。

解决方案：

- 按课程结构重新配置通道 1 周期捕获和通道 2 高电平捕获。
- 对异常的 `CCR2 > CCR1` 增加保护。
- 对显示值做多次采样平均，减小偶发不同步。
- 使用示波器确认实际占空比不是接近 0% 或 100% 的极限状态。

---

### 问题 4：高频信号测量值跳动或明显偏低

现象：

- 低频测量正常。
- 提高输入频率后读数开始跳动。
- 频率继续升高时出现漏捕获。

排查过程：

1. 检查 `TIM_ICFilter` 是否设置过大。
2. 检查 PA6 输入波形的上升沿和下降沿是否足够陡峭。
3. 检查杜邦线是否过长、是否存在较大干扰。
4. 检查输入捕获预分频是否正确。
5. 检查一个周期内 CNT 是否只有很少计数。
6. 检查 OLED 刷新逻辑是否把瞬态值直接显示出来。

解决方案：

- 对干净的高速方波适当减小输入数字滤波。
- 缩短信号线并保证良好共地。
- 提高 TIM3 计数标准频率，以改善高频测量分辨率。
- 对读数进行滑动平均或中值滤波。
- 高频测量需求较高时，考虑改用测频法。

---

### 问题 5：低频信号测量结果错误或突然变大

现象：

- 频率下降到十几 Hz 附近后结果异常。
- CCR1 不再随周期继续增大。
- 频率显示为不合理的较大数值。

排查过程：

1. 检查 TIM3 ARR 是否为 65535。
2. 计算输入周期是否超过 65.535ms。
3. 检查 CNT 是否在下一个捕获边沿到来前溢出。
4. 检查是否实现溢出次数累计。
5. 检查信号是否已经停止而程序仍显示旧 CCR。

解决方案：

- 降低 TIM3 计数频率以扩大单次测量范围。
- 使用更新中断累计溢出次数。
- 使用 32 位定时器或定时器级联扩展计数宽度。
- 对无新捕获事件设置超时，并将频率显示为 0 或“无信号”。

---

### 问题 6：外部信号发生器接入后完全无法捕获

现象：

- 使用 STM32 自己输出的 PWM 可以测量。
- 换成外部信号发生器后 CCR 不更新或芯片工作异常。

排查过程：

1. 检查信号发生器与 STM32 是否共地。
2. 检查高电平幅值是否处于 STM32 引脚允许范围。
3. 检查是否存在负电压或过大的过冲。
4. 检查信号输出模式是否为方波而不是高阻或模拟波形。
5. 检查信号频率是否处于当前配置的可测范围。
6. 检查 PA6 是否被其他外设或调试功能占用。

解决方案：

- 将信号发生器 GND 与开发板 GND 相连。
- 设置为 0～3.3V 兼容方波。
- 必要时使用限流、电平转换或整形电路。
- 先从 1kHz、50% 占空比的稳定方波开始验证。

## 结果展示

> 实验 1：TIM2_CH1 在 PA0 输出约 1kHz、50% 占空比 PWM；PA0 连接 PA6 后，TIM3_CH1 捕获到约 1000 个计数，OLED 显示频率约 1000Hz。✅

> 实验 2：将 PWM 占空比设置为约 30% 后，PWMI 模式下 CCR1 约为 1000、CCR2 约为 300，OLED 同时显示约 1000Hz 和 30%。✅

> 实验 3：改变 TIM2 的预分频器后，PWM 输出频率发生变化，TIM3 测量结果随之更新。✅

> 实验 4：改变 TIM2_CCR1 后，PWM 占空比发生变化，PWMI 测得的占空比与设定值基本一致。✅

## 本节小结

本节使用 TIM3 输入捕获完成了 PWM 参数测量，打通了定时器“输出波形”和“测量波形”两类功能。

普通输入捕获模式中：

```text
相邻上升沿 → CCR1 锁存周期计数 → f = fc / CCR1
```

PWMI 模式中：

```text
上升沿通道记录完整周期
下降沿通道记录高电平时间
占空比 = 高电平时间 / 完整周期
```

真正需要掌握的不是记住某几个库函数，而是理解定时器内部的数据流：

```text
输入边沿
→ 数字滤波与极性选择
→ CCR 捕获 CNT
→ 触发从模式复位 CNT
→ 软件读取 CCR 并换算物理量
```

掌握输入捕获后，可以进一步实现：

- 电机转速与风扇转速测量。
- 超声波回波脉宽测量。
- 舵机与遥控接收机 PWM 解码。
- 红外、脉冲传感器和流量计信号测量。
- 频率计、占空比计和数字测速仪。
- 不规则脉冲的时间戳记录与统计分析。

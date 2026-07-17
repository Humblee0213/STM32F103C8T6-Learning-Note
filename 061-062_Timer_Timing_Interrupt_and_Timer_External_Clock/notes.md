# 12 — TIM定时器：定时中断与外部时钟

> 内容说明：本节对应江协科技《STM32入门教程-2023版》P14——`[6-2] 定时器定时中断&定时器外部时钟`。由于无法直接获取视频完整字幕与逐帧画面，以下代码根据该课程公开目录、课程上下文及 STM32F103 标准外设库用法整理为参考实现，具体引脚与参数请以开发板原理图和实际工程为准。

## 实验概述

本实验基于 STM32F103C8T6 标准外设库，使用通用定时器 TIM2 完成两个基础实验：

1. 使用 TIM2 内部时钟产生 1 秒一次的更新中断，并在中断服务函数中累加计数值。
2. 使用 TIM2 的 ETR 外部时钟输入端对传感器脉冲计数，每累计 10 个有效脉冲产生一次更新中断。

本节的核心目标是掌握定时器基本定时功能的完整配置链路：

> 开启定时器时钟 → 选择计数时钟源 → 配置时基单元 → 开启更新中断 → 配置 NVIC → 启动计数器 → 编写中断服务函数

同时理解定时器既可以作为“定时器”，也可以作为“计数器”：

```text
内部稳定时钟驱动 CNT → 按时间周期溢出 → 定时器
外部脉冲边沿驱动 CNT → 按事件数量溢出 → 计数器
```

## 硬件连接

### 实验一：定时器定时中断

定时中断实验不需要额外的计数输入器件，只需要最小系统、OLED 和下载调试接口。

| 器件 | 对应引脚 | 说明 |
|---|---:|---|
| OLED SCL | PB8 | 软件 I²C 时钟线，具体以 OLED 驱动文件为准 |
| OLED SDA | PB9 | 软件 I²C 数据线，具体以 OLED 驱动文件为准 |
| ST-Link SWDIO | PA13 | 程序下载与调试 |
| ST-Link SWCLK | PA14 | 程序下载与调试 |
| 供电 | 3.3V、GND | 所有模块必须共地 |

### 实验二：定时器外部时钟

本实验以对射式红外传感器或其他数字脉冲源为例，通过 TIM2 的 ETR 引脚输入外部时钟。

| 器件 | 对应引脚 | 说明 |
|---|---:|---|
| 传感器 DO | PA0 / TIM2_ETR | 外部时钟模式 2 的 ETR 输入，无重映射时使用 PA0 |
| 传感器 VCC | 3.3V | 按模块允许的供电电压连接 |
| 传感器 GND | GND | 与 STM32 共地 |
| OLED SCL | PB8 | 显示累计溢出次数和当前 CNT 值 |
| OLED SDA | PB9 | 显示累计溢出次数和当前 CNT 值 |
| ST-Link | PA13、PA14、3.3V、GND | SWD 下载与调试 |

> 注意：传感器数字输出的有效边沿、空闲电平和输出类型可能不同。若计数方向或触发边沿与预期不符，需要调整 `TIM_ExtTRGPolarity_NonInverted`、输入上拉方式或传感器模块上的比较器阈值。

## 工程文件结构

建议将两个实验分别建立为独立工程，便于对比内部时钟与外部时钟的区别。

```text
12-TIM-Update-Interrupt-External-Clock
├── 01-Timer-Update-Interrupt
│   ├── User
│   │   └── main.c
│   ├── System
│   │   ├── Timer.c
│   │   ├── Timer.h
│   │   ├── OLED.c
│   │   └── OLED.h
│   └── Startup
│       └── stm32f10x_it.c
├── 02-Timer-External-Clock
│   ├── User
│   │   └── main.c
│   ├── System
│   │   ├── Timer.c
│   │   ├── Timer.h
│   │   ├── OLED.c
│   │   └── OLED.h
│   └── Startup
│       └── stm32f10x_it.c
├── Hardware
│   └── TIM2_ETR_PA0_Connection.png
├── Results
│   ├── Timer_Update_Interrupt.gif
│   └── Timer_External_Clock.gif
└── README.md
```

## 核心代码

> 以下代码为标准外设库参考实现。为了突出本节重点，默认工程中已经加入启动文件、CMSIS、标准外设库、OLED 驱动及对应头文件路径。

### 1. 定时器定时中断：Timer.h

```c
#ifndef __TIMER_H                              // 判断定时器头文件是否尚未被包含，避免重复定义
#define __TIMER_H                              // 定义头文件保护宏，使本文件在一次编译中只展开一次
#include "stm32f10x.h"                         // 引入 STM32F10x 设备定义和标准外设库基础类型
void Timer_Init(void);                         // 声明 TIM2 定时中断初始化函数，供 main.c 调用
#endif                                         // 结束 __TIMER_H 对应的条件编译区域
```

### 2. 定时器定时中断：Timer.c

```c
#include "stm32f10x.h"                         // 引入 RCC、TIM 和 NVIC 等标准外设库接口
#include "Timer.h"                             // 引入本模块函数声明，保证声明与定义保持一致
void Timer_Init(void)                          // 定义 TIM2 定时中断初始化函数
{                                              // 进入定时器初始化函数体
    RCC_APB1PeriphClockCmd(RCC_APB1Periph_TIM2, ENABLE); // 打开 APB1 总线上的 TIM2 外设时钟，使 TIM2 寄存器可以工作
    TIM_InternalClockConfig(TIM2);             // 选择 TIM2 内部时钟 CK_INT 作为计数器时钟源
    TIM_TimeBaseInitTypeDef TIM_TimeBaseInitStructure; // 定义时基初始化结构体，用于集中配置 PSC、ARR 和计数模式
    TIM_TimeBaseInitStructure.TIM_ClockDivision = TIM_CKD_DIV1; // 设置 CKD 为 1 分频，此字段主要影响数字滤波采样时钟而非 CNT 基本计数频率
    TIM_TimeBaseInitStructure.TIM_CounterMode = TIM_CounterMode_Up; // 设置 CNT 从 0 向 ARR 递增计数
    TIM_TimeBaseInitStructure.TIM_Period = 10000 - 1; // 设置自动重装值 ARR 为 9999，使 CNT 每累计 10000 个计数时钟产生更新事件
    TIM_TimeBaseInitStructure.TIM_Prescaler = 7200 - 1; // 设置预分频值 PSC 为 7199，将 72MHz 定时器时钟分频为 10kHz
    TIM_TimeBaseInitStructure.TIM_RepetitionCounter = 0; // 设置重复计数器为 0，通用定时器不使用该字段但需给出确定值
    TIM_TimeBaseInit(TIM2, &TIM_TimeBaseInitStructure); // 把结构体参数写入 TIM2 的 PSC、ARR 和 CR1 等寄存器
    TIM_ClearFlag(TIM2, TIM_FLAG_Update);      // 清除初始化时可能由更新事件置位的 UIF 标志，避免启动后立即误进中断
    TIM_ITConfig(TIM2, TIM_IT_Update, ENABLE); // 允许 TIM2 更新事件向 NVIC 发出中断请求
    NVIC_PriorityGroupConfig(NVIC_PriorityGroup_2); // 设置 NVIC 为 2 位抢占优先级和 2 位响应优先级的分组方式
    NVIC_InitTypeDef NVIC_InitStructure;       // 定义 NVIC 初始化结构体，用于配置 TIM2 中断通道和优先级
    NVIC_InitStructure.NVIC_IRQChannel = TIM2_IRQn; // 选择 TIM2 全局中断通道
    NVIC_InitStructure.NVIC_IRQChannelCmd = ENABLE; // 在 NVIC 中使能 TIM2 中断通道
    NVIC_InitStructure.NVIC_IRQChannelPreemptionPriority = 2; // 设置 TIM2 的抢占优先级为 2
    NVIC_InitStructure.NVIC_IRQChannelSubPriority = 1; // 设置 TIM2 的响应优先级为 1
    NVIC_Init(&NVIC_InitStructure);            // 将中断通道和优先级配置写入 NVIC
    TIM_Cmd(TIM2, ENABLE);                     // 置位 TIM2 的 CEN 位，启动计数器开始计数
}                                              // 结束 TIM2 定时中断初始化函数
```

### 3. 定时器定时中断：main.c

```c
#include "stm32f10x.h"                         // 引入 STM32F10x 设备定义和标准外设库接口
#include "OLED.h"                              // 引入 OLED 初始化与显示函数声明
#include "Timer.h"                             // 引入 TIM2 定时中断初始化函数声明
volatile uint16_t Num = 0;                     // 定义由中断修改的全局计数变量，并用 volatile 防止编译器缓存旧值
int main(void)                                 // 定义程序入口函数
{                                              // 进入主函数体
    OLED_Init();                               // 初始化 OLED 显示屏及其通信引脚
    Timer_Init();                              // 初始化 TIM2，使其每 1 秒产生一次更新中断
    OLED_ShowString(1, 1, "Num:");             // 在 OLED 第 1 行第 1 列显示计数标签
    while (1)                                  // 进入主循环，让前台任务持续刷新显示
    {                                          // 进入主循环代码块
        OLED_ShowNum(1, 5, Num, 5);            // 显示中断累计次数，定时器每溢出一次该数值加 1
    }                                          // 结束本次主循环并立即开始下一次循环
}                                              // 结束 main 函数，嵌入式程序通常不会执行到此处之后
```

### 4. 定时器定时中断：stm32f10x_it.c

```c
#include "stm32f10x.h"                         // 引入 TIM 中断状态读取和标志清除函数
extern volatile uint16_t Num;                  // 声明 main.c 中定义的全局变量，使中断文件可以访问同一存储对象
void TIM2_IRQHandler(void)                     // 定义 TIM2 的中断服务函数，函数名必须与启动文件向量表完全一致
{                                              // 进入 TIM2 中断服务函数体
    if (TIM_GetITStatus(TIM2, TIM_IT_Update) == SET) // 判断本次 TIM2 中断是否确实由更新事件触发
    {                                          // 进入更新中断处理分支
        Num++;                                 // 每发生一次定时器更新事件就把累计次数加 1
        TIM_ClearITPendingBit(TIM2, TIM_IT_Update); // 清除 TIM2 更新中断挂起标志，允许系统响应下一次更新中断
    }                                          // 结束更新中断处理分支
}                                              // 退出 TIM2 中断服务函数并返回被中断的程序
```

### 5. 定时器外部时钟：Timer.h

```c
#ifndef __TIMER_H                              // 判断定时器头文件是否尚未定义，避免重复包含
#define __TIMER_H                              // 定义头文件保护宏，确保声明只展开一次
#include "stm32f10x.h"                         // 引入 uint16_t、TIM_TypeDef 和标准外设库基础定义
void Timer_Init(void);                         // 声明 TIM2 外部时钟计数初始化函数
uint16_t Timer_GetCounter(void);               // 声明读取 TIM2 当前 CNT 计数值的接口函数
#endif                                         // 结束头文件保护条件编译
```

### 6. 定时器外部时钟：Timer.c

```c
#include "stm32f10x.h"                         // 引入 RCC、GPIO、TIM 和 NVIC 标准外设库接口
#include "Timer.h"                             // 引入当前模块的函数声明
void Timer_Init(void)                          // 定义 TIM2 外部时钟模式初始化函数
{                                              // 进入外部时钟初始化函数体
    RCC_APB1PeriphClockCmd(RCC_APB1Periph_TIM2, ENABLE); // 打开 TIM2 外设时钟，使定时器寄存器和计数逻辑工作
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA, ENABLE); // 打开 GPIOA 时钟，使 PA0 可以作为 TIM2_ETR 输入
    GPIO_InitTypeDef GPIO_InitStructure;       // 定义 GPIO 初始化结构体，用于配置 PA0 输入方式
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_IPU; // 将 PA0 配置为上拉输入，为开漏或悬空脉冲源提供稳定空闲高电平
    GPIO_InitStructure.GPIO_Pin = GPIO_Pin_0;  // 选择 PA0，引脚在 TIM2 无重映射时对应 ETR 外部触发输入
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz; // 填写 GPIO 速度字段，输入模式下该字段基本不决定采样速度
    GPIO_Init(GPIOA, &GPIO_InitStructure);     // 将以上配置写入 GPIOA 的 CRL 寄存器
    TIM_ETRClockMode2Config(TIM2, TIM_ExtTRGPSC_OFF, TIM_ExtTRGPolarity_NonInverted, 0x0F); // 选择 ETR 外部时钟模式 2，不预分频、非反相并启用较强数字滤波
    TIM_TimeBaseInitTypeDef TIM_TimeBaseInitStructure; // 定义时基初始化结构体，用于设置外部脉冲累计规则
    TIM_TimeBaseInitStructure.TIM_ClockDivision = TIM_CKD_DIV1; // 设置数字滤波采样相关时钟为不分频
    TIM_TimeBaseInitStructure.TIM_CounterMode = TIM_CounterMode_Up; // 设置 CNT 每接收到一个有效外部边沿就向上加 1
    TIM_TimeBaseInitStructure.TIM_Period = 10 - 1; // 设置 ARR 为 9，使第 10 个有效脉冲到来时产生更新事件并回到 0
    TIM_TimeBaseInitStructure.TIM_Prescaler = 1 - 1; // 设置 PSC 为 0，使外部时钟进入计数器前不再分频
    TIM_TimeBaseInitStructure.TIM_RepetitionCounter = 0; // 通用定时器不使用重复计数功能，设置为 0 保持结构体完整
    TIM_TimeBaseInit(TIM2, &TIM_TimeBaseInitStructure); // 把时基参数写入 TIM2，建立每 10 个脉冲更新一次的计数周期
    TIM_ClearFlag(TIM2, TIM_FLAG_Update);      // 清除初始化过程可能产生的更新标志，防止使能后出现一次伪中断
    TIM_ITConfig(TIM2, TIM_IT_Update, ENABLE); // 允许计数器溢出产生 TIM2 更新中断请求
    NVIC_PriorityGroupConfig(NVIC_PriorityGroup_2); // 配置全局 NVIC 优先级分组，与工程内其他中断保持一致
    NVIC_InitTypeDef NVIC_InitStructure;       // 定义 NVIC 初始化结构体
    NVIC_InitStructure.NVIC_IRQChannel = TIM2_IRQn; // 选择 TIM2 全局中断通道
    NVIC_InitStructure.NVIC_IRQChannelCmd = ENABLE; // 在 NVIC 中打开 TIM2 中断响应
    NVIC_InitStructure.NVIC_IRQChannelPreemptionPriority = 2; // 设置 TIM2 抢占优先级为 2
    NVIC_InitStructure.NVIC_IRQChannelSubPriority = 1; // 设置 TIM2 响应优先级为 1
    NVIC_Init(&NVIC_InitStructure);            // 将 TIM2 中断优先级配置写入 NVIC
    TIM_Cmd(TIM2, ENABLE);                     // 启动 TIM2，等待 PA0/ETR 上的有效边沿驱动 CNT 计数
}                                              // 结束外部时钟初始化函数
uint16_t Timer_GetCounter(void)                // 定义获取当前外部脉冲余数的函数
{                                              // 进入计数值读取函数体
    return TIM_GetCounter(TIM2);               // 返回 TIM2 的 CNT 当前值，本例范围通常为 0 到 9
}                                              // 结束计数值读取函数
```

### 7. 定时器外部时钟：main.c

```c
#include "stm32f10x.h"                         // 引入 STM32F10x 设备定义和标准外设库接口
#include "OLED.h"                              // 引入 OLED 显示函数声明
#include "Timer.h"                             // 引入 TIM2 外部计数初始化和 CNT 读取函数声明
volatile uint16_t Num = 0;                     // 保存 TIM2 每累计 10 个脉冲后的更新次数，并允许中断异步修改
int main(void)                                 // 定义程序入口函数
{                                              // 进入主函数体
    OLED_Init();                               // 初始化 OLED，准备显示总计数和当前余数
    Timer_Init();                              // 初始化 TIM2 外部时钟模式 2 和更新中断
    OLED_ShowString(1, 1, "Num:");             // 在第 1 行显示累计更新次数标签
    OLED_ShowString(2, 1, "CNT:");             // 在第 2 行显示当前 CNT 余数标签
    while (1)                                  // 进入持续运行的前台显示循环
    {                                          // 进入主循环代码块
        OLED_ShowNum(1, 5, Num, 5);            // 显示完整的十脉冲组数量，每产生一次更新中断加 1
        OLED_ShowNum(2, 5, Timer_GetCounter(), 5); // 读取并显示当前尚未凑满 10 个的外部脉冲数量
    }                                          // 结束本轮显示刷新并返回循环起点
}                                              // 结束 main 函数
```

### 8. 定时器外部时钟：stm32f10x_it.c

```c
#include "stm32f10x.h"                         // 引入 TIM2 中断状态读取和挂起位清除函数
extern volatile uint16_t Num;                  // 引用 main.c 中的累计更新次数变量
void TIM2_IRQHandler(void)                     // 定义 TIM2 全局中断服务函数，名称需与中断向量表一致
{                                              // 进入 TIM2 中断处理函数体
    if (TIM_GetITStatus(TIM2, TIM_IT_Update) == SET) // 确认 TIM2 的更新中断标志已置位，排除其他中断源
    {                                          // 进入有效更新事件处理分支
        Num++;                                 // 每累计 10 个外部有效脉冲，就把完整组数量加 1
        TIM_ClearITPendingBit(TIM2, TIM_IT_Update); // 清除 UIF 挂起标志，避免退出中断后立即再次进入
    }                                          // 结束更新事件处理分支
}                                              // 退出 TIM2 中断服务函数并恢复主程序运行
```

## 代码要点

| 行/段 | 说明 |
|---|---|
| `RCC_APB1PeriphClockCmd(RCC_APB1Periph_TIM2, ENABLE)` | TIM2 挂载在 APB1 总线上，使用前必须打开外设时钟。 |
| `TIM_InternalClockConfig(TIM2)` | 明确选择内部时钟 CK_INT 作为 TIM2 时基单元的输入。复位状态下通常也是内部时钟，但显式配置更便于阅读。 |
| `TIM_TimeBaseInitTypeDef` | 配置预分频器 PSC、自动重装寄存器 ARR、计数模式和 CKD。 |
| `TIM_Prescaler = 7200 - 1` | 寄存器实际保存 7199，因此分频系数为 `PSC + 1 = 7200`。 |
| `TIM_Period = 10000 - 1` | CNT 从 0 数到 9999，共计 10000 个计数，随后产生更新事件。 |
| `TIM_ClearFlag()` | 清除时基初始化可能产生的更新标志，避免刚启动就进入一次非预期中断。 |
| `TIM_ITConfig()` | 打开定时器内部的更新中断输出开关；仅配置 NVIC 还不够。 |
| `NVIC_Init()` | 在 Cortex-M3 的 NVIC 中开启 TIM2_IRQn 并设置优先级。 |
| `TIM_Cmd()` | 置位 CEN，真正启动计数器。没有这一步，CNT 不会运行。 |
| `TIM_GetITStatus()` | 同时检查中断使能状态和标志状态，确认中断来源。 |
| `TIM_ClearITPendingBit()` | 中断服务完成后必须清除更新挂起位，否则可能反复进入中断。 |
| `TIM_ETRClockMode2Config()` | 让 ETR 引脚直接作为外部时钟输入，配置 ETR 预分频、极性和数字滤波。 |
| `TIM_GetCounter()` | 读取当前 CNT，可观察尚未达到 ARR 溢出条件的脉冲余数。 |
| `volatile` | 告诉编译器该变量可能在中断等异步上下文中改变，每次使用都应重新读取内存。 |

## 关键知识点

### 1. 定时器时基单元

#### 原理

通用定时器的基本定时功能主要由三个寄存器构成：

```text
输入时钟 → PSC 预分频器 → CNT 计数器 → ARR 自动重装寄存器 → 更新事件
```

预分频器把较高频率的定时器时钟降低为 CNT 可使用的计数时钟。CNT 按设定方向计数，当向上计数达到 ARR 后产生更新事件，并重新从 0 开始。

向上计数时，更新频率可表示为：

```text
f_update = f_TIM / ((PSC + 1) × (ARR + 1))
```

本实验在 TIM2 时钟为 72MHz 时设置：

```text
PSC = 7200 - 1
ARR = 10000 - 1
f_update = 72MHz / 7200 / 10000 = 1Hz
```

因此每 1 秒产生一次更新事件。

#### 特点

- 定时过程由硬件自动完成，不需要 CPU 执行空循环。
- PSC 和 ARR 均按“寄存器值加 1”参与分频和计数。
- 更新事件可触发中断、DMA 请求或内部触发信号。
- TIM2 是 16 位通用定时器，CNT 和 ARR 的常用范围为 0～65535。
- 修改 PSC 后通常需要更新事件，才能让新预分频值装载到工作寄存器。

#### 面试易问

**Q：为什么 `TIM_Prescaler` 和 `TIM_Period` 通常都要减 1？**

A：因为硬件的实际分频系数是 `PSC + 1`，向上计数的周期计数次数是 `ARR + 1`。若希望分频 7200、计数 10000 次，就应分别写入 7199 和 9999。

**Q：定时器为什么比软件 Delay 更适合周期任务？**

A：定时器由硬件独立计数，CPU 可以在等待期间执行其他任务；软件 Delay 会持续占用 CPU，并且容易受编译优化和系统时钟变化影响。

#### 易错点

- 直接把目标分频系数写入 PSC，导致周期比预期多一个计数。
- 忽略 ARR 计数包含 0，导致周期计算出现一个计数的偏差。
- 只初始化时基但没有调用 `TIM_Cmd()`，CNT 始终不变化。
- 错把 `TIM_ClockDivision` 当成 CNT 主计数时钟的普通预分频器。

---

### 2. APB1 定时器时钟倍频规则

#### 原理

STM32F103 常见的 72MHz 系统时钟配置中，APB1 总线通常被分频到 36MHz。对于挂在 APB1 上的定时器，当 APB1 预分频系数不为 1 时，定时器内核时钟会自动变为 APB1 外设时钟的 2 倍。

典型关系如下：

```text
SYSCLK = 72MHz
HCLK   = 72MHz
PCLK1  = 36MHz
TIM2CLK = 2 × PCLK1 = 72MHz
```

因此不能简单地把 TIM2 时钟当成 36MHz。

#### 特点

- TIM2、TIM3、TIM4 等通用定时器位于 APB1。
- APB 预分频为 1 时，定时器时钟等于 PCLK。
- APB 预分频大于 1 时，定时器时钟通常等于 2 倍 PCLK。
- 该规则允许降低外设总线频率，同时保持定时器较高的计数精度。

#### 面试易问

**Q：APB1 是 36MHz，为什么 TIM2 还能按 72MHz 计算？**

A：因为 APB1 预分频不为 1 时，STM32F1 会把 APB1 定时器时钟自动倍频为 PCLK1 的 2 倍，所以 TIM2 的输入时钟仍为 72MHz。

**Q：怎样确认当前定时器真实时钟？**

A：应检查 RCC 时钟树、APB 预分频设置和芯片参考手册中的定时器倍频规则，而不能只看系统主频或 PCLK 数值。

#### 易错点

- 只根据 PCLK1 计算周期，导致实际中断频率变成预期的 2 倍。
- 修改系统时钟后没有同步修改 PSC 和 ARR。
- 在不同系列 STM32 之间直接照搬定时器时钟结论，没有查看对应参考手册。

---

### 3. 更新事件与更新中断

#### 原理

更新事件通常在以下情况出现：

- 向上计数器溢出。
- 向下计数器下溢。
- 软件设置 UG 位强制产生更新。
- 某些触发控制器事件重新初始化计数器。

更新事件产生后，UIF 标志位置位。只有同时打开定时器的更新中断使能和 NVIC 中断通道，CPU 才会进入 `TIM2_IRQHandler()`。

完整中断链路为：

```text
CNT 达到 ARR
→ 产生更新事件
→ UIF 置位
→ DIER.UIE 允许更新中断
→ NVIC 接收 TIM2_IRQn
→ CPU 执行 TIM2_IRQHandler()
```

#### 特点

- “事件”和“中断”不是同一个概念，事件可在不开中断时照常产生。
- 定时器内部中断开关与 NVIC 通道开关必须同时打开。
- 中断服务函数中应判断来源并清除对应标志。
- 同一个定时器可能具有更新、捕获比较、触发等多个中断来源。

#### 面试易问

**Q：已经在 NVIC 中使能 TIM2_IRQn，为什么还没有中断？**

A：还必须使用 `TIM_ITConfig()` 打开定时器内部的更新中断使能，同时确保计数器已启动、时钟源有效且中断标志能够产生。

**Q：为什么中断服务函数中要先判断 `TIM_GetITStatus()`？**

A：TIM2 全局中断通道可能对应多个内部中断源，判断状态可以确认具体来源，避免错误处理。

#### 易错点

- 忘记清除 UIF，导致程序不断重复进入中断。
- 只打开 `TIM_IT_Update`，却没有在 NVIC 中打开 TIM2_IRQn。
- 初始化时产生了更新标志，未清除就启动，导致立即执行一次中断。
- 中断函数名称拼写错误，向量表无法跳转到用户编写的函数。

---

### 4. NVIC 优先级分组

#### 原理

Cortex-M3 使用 NVIC 管理可屏蔽中断。STM32F103 实现了若干有效优先级位，并通过优先级分组将这些位划分为抢占优先级和响应优先级。

- 抢占优先级决定一个中断能否打断另一个正在执行的中断。
- 响应优先级用于抢占优先级相同且同时挂起时的先后排序。
- 数值越小，优先级越高。

#### 特点

- 优先级分组是全局设置，不属于某一个中断。
- 工程中通常只需统一设置一次分组。
- 抢占优先级不同的中断可以发生嵌套。
- 同抢占优先级的中断通常不能互相抢占。

#### 面试易问

**Q：抢占优先级和响应优先级有什么区别？**

A：抢占优先级决定中断嵌套能力；响应优先级只在抢占优先级相同、多个中断同时等待时决定服务顺序。

**Q：优先级数值越大是否优先级越高？**

A：不是。Cortex-M 的中断优先级数值越小，实际优先级越高。

#### 易错点

- 在多个模块中反复设置不同的优先级分组，导致原有优先级含义改变。
- 把响应优先级误认为可以打断正在执行的同抢占级中断。
- 优先级参数超出当前分组允许范围。
- 在中断中执行长时间阻塞操作，影响其他中断实时性。

---

### 5. ETR 外部时钟模式 2

#### 原理

外部时钟模式 2 使用定时器的 ETR 引脚直接驱动计数器。外部信号经过极性选择、数字滤波和可选预分频后，成为 CNT 的计数时钟。

```text
PA0 / TIM2_ETR
→ 极性选择
→ 数字滤波
→ ETR 预分频
→ TIM2 CNT
→ CNT 达到 ARR
→ 更新事件
```

本实验使用：

```c
TIM_ETRClockMode2Config(
    TIM2,
    TIM_ExtTRGPSC_OFF,
    TIM_ExtTRGPolarity_NonInverted,
    0x0F
);
```

当外部有效边沿到来时，CNT 增加 1。ARR 设为 9 后，每 10 个有效脉冲产生一次更新事件。

#### 特点

- 外部脉冲由硬件直接计数，CPU 不需要响应每一个边沿。
- ETR 输入支持极性选择、1/2/4/8 分频和数字滤波。
- 适合脉冲计数、转速统计、流量计量和事件累计。
- 计数上限和溢出周期由 PSC、ARR 共同决定。
- PA0 只有在对应引脚映射条件满足时才是 TIM2_ETR。

#### 面试易问

**Q：定时器外部时钟计数与 EXTI 每次中断计数有什么区别？**

A：定时器可以在硬件中直接累计脉冲，仅在达到设定数量时中断一次，CPU 开销更小；EXTI 通常每个有效边沿都进入中断，脉冲频率较高时 CPU 负担更大。

**Q：外部时钟模式 1 和模式 2 有什么主要区别？**

A：模式 1 通过触发输入选择器把 TIx、ETR 或内部触发信号作为从模式控制器的时钟；模式 2 专门使用 ETR 路径直接作为外部时钟。具体选择取决于信号接入引脚和所需触发链路。

#### 易错点

- 把传感器输出接到普通 GPIO，而不是当前映射下的 TIM2_ETR 引脚。
- 外部信号没有与 STM32 共地，导致计数随机或完全不计数。
- 触发极性选择错误，只在相反边沿计数。
- 滤波值过强导致窄脉冲被滤掉，或滤波太弱导致抖动被重复计数。
- 输入电压超过 STM32 引脚允许范围。

---

### 6. ETR 数字滤波与极性

#### 原理

机械触点、比较器输出和长导线可能产生毛刺。ETR 数字滤波器会以指定采样时钟对输入信号采样，只有检测到连续若干个一致样本后，才把电平变化传递给计数逻辑。

`TIM_ExtTRGPolarity_NonInverted` 表示输入不反相，通常对应上升沿有效；反相配置则改变有效边沿。`0x0F` 表示采用较强滤波，具体采样频率和连续样本数由滤波编码决定。

#### 特点

- 数字滤波可以抑制短毛刺和抖动。
- 滤波会引入一定传播延迟。
- 滤波越强，能够识别的最窄有效脉冲宽度通常越大。
- 极性设置决定哪个边沿驱动计数器。

#### 面试易问

**Q：滤波参数是不是越大越好？**

A：不是。滤波越强虽然抗干扰能力越好，但可能过滤掉频率较高或脉宽较窄的真实信号，因此应根据输入频率、脉宽和噪声情况选择。

**Q：为什么传感器遮挡一次可能计数多次？**

A：可能是传感器输出抖动、比较器阈值处于临界状态、导线干扰或滤波不足，使一个物理事件产生多个有效边沿。

#### 易错点

- 不分析输入脉宽就直接使用最大滤波，造成漏计数。
- 传感器阈值调得过于敏感，输出在高低电平间反复跳变。
- 将上升沿和下降沿对应的物理事件理解反了。
- 只靠软件延时消抖，却忽略定时器自带硬件滤波能力。

---

### 7. 定时器外部计数的分组含义

#### 原理

外部时钟实验中，`ARR = 9`、`PSC = 0`，因此 CNT 每接收 10 个有效边沿就溢出一次：

```text
总脉冲数 = Num × 10 + CNT
```

其中：

- `Num` 表示已经完成的十脉冲组数量。
- `CNT` 表示当前尚未凑满 10 个的余数。
- 若只需要总脉冲数，可以在主循环中按上述公式计算。

#### 特点

- 利用硬件溢出可以把长时间累计值扩展到 16 位以上。
- 中断频率只有输入脉冲频率的十分之一。
- 修改 ARR 可以改变每组脉冲数量。
- 软件高位计数与硬件 CNT 组合可形成更宽的计数器。

#### 面试易问

**Q：16 位定时器如何统计超过 65535 个脉冲？**

A：可以在每次溢出中断中累加软件高位，再将软件溢出次数与 CNT 当前值组合，形成 32 位或更宽的计数结果。

**Q：读取软件高位和 CNT 时为什么可能出现不一致？**

A：如果读取过程中恰好发生溢出中断，软件高位和 CNT 可能来自不同计数周期。严格场景下应使用临界区、重复读取校验或其他原子快照方法。

#### 易错点

- 只显示 `Num`，误以为它就是每一个外部脉冲的数量。
- 忽略 CNT 余数，导致总计数始终是 10 的整数倍。
- 在主循环计算总数时未考虑中断并发更新。
- 计数变量宽度太小，长时间运行后发生软件溢出。

---

### 8. `volatile` 与中断共享变量

#### 原理

主程序和中断服务函数共享变量时，变量可能在主程序没有显式赋值的情况下被异步修改。`volatile` 告诉编译器，每次读取都必须从内存获取最新值，每次写入也必须真正写回内存。

```c
volatile uint16_t Num;
```

但 `volatile` 只约束编译优化，不等于线程安全，也不会自动保证多个步骤组成的复合操作具有原子性。

#### 特点

- 适用于中断共享变量、硬件寄存器和可能被异步改变的数据。
- 防止编译器把变量长期保存在寄存器中。
- 不提供互斥、不关闭中断，也不建立完整的并发同步机制。
- 简单的对齐 16 位读取在 Cortex-M3 上通常是单次访问，但复杂表达式仍可能被中断打断。

#### 面试易问

**Q：中断变量为什么常加 `volatile`？**

A：因为变量可能随时被中断修改，若不加 `volatile`，编译器可能认为它没有变化并复用旧值，主程序就无法及时看到更新。

**Q：加了 `volatile` 就一定线程安全吗？**

A：不一定。`volatile` 只保证实际内存访问，不保证复合操作原子性，也不解决多个执行上下文同时修改数据的竞争问题。

#### 易错点

- 忘记添加 `volatile`，优化级别提高后显示值不更新。
- 把 `volatile` 当作互斥锁使用。
- 在中断和主循环中同时执行 `Num++`，造成更新丢失。
- 用过窄的数据类型保存长期累计值。

---

## 本节核心记忆

```text
定时器基本结构：
时钟源 → PSC → CNT → ARR → 更新事件
```

```text
更新频率：
f_update = f_TIM / ((PSC + 1) × (ARR + 1))
```

```text
内部时钟驱动 CNT：按时间计数
外部 ETR 脉冲驱动 CNT：按事件数量计数
```

```text
产生中断必须同时满足：
定时器事件产生 + 定时器中断使能 + NVIC 通道使能
```

```text
中断服务函数固定步骤：
判断中断来源 → 执行简短任务 → 清除挂起标志
```

```text
外部计数总脉冲数：
Total = OverflowCount × (ARR + 1) × (PSC + 1) + CNT × (PSC + 1)
```

## 开发过程总结

### 问题 1：TIM2 定时中断频率不正确

现象：

- 预期每 1 秒加 1，实际每 0.5 秒或 2 秒加 1。
- 修改 PSC 后周期仍与计算不一致。

排查过程：

1. 检查系统主频和 APB1 预分频。
2. 确认 APB1 预分频不为 1 时的定时器倍频规则。
3. 检查 PSC 和 ARR 是否都按实际系数减 1。
4. 检查是否误把 `TIM_ClockDivision` 当成 CNT 时钟分频。
5. 检查工程的 `SystemInit()` 是否使用预期时钟配置。

解决方案：

- 根据真实 `TIM2CLK` 重新计算 PSC 和 ARR。
- 使用示波器或翻转 GPIO 的方式测量中断周期。
- 将时钟参数集中定义为宏，避免魔法数字分散在代码中。

### 问题 2：程序启动时 Num 立即变成 1

现象：

- 刚复位时尚未等待完整周期，计数值已经增加。
- 后续中断周期正常。

排查过程：

1. 检查 `TIM_TimeBaseInit()` 后 UIF 是否已经置位。
2. 检查开启更新中断前是否清除更新标志。
3. 检查是否在启动定时器前执行了软件更新事件。

解决方案：

- 在 `TIM_TimeBaseInit()` 之后、打开中断之前调用 `TIM_ClearFlag(TIM2, TIM_FLAG_Update)`。
- 确保清标志操作发生在 `TIM_ITConfig()` 和 `TIM_Cmd()` 之前。

### 问题 3：TIM2 完全不进入中断

现象：

- OLED 正常显示，但 Num 始终为 0。
- 调试时 CNT 可能在变化，也可能不变化。

排查过程：

1. 检查是否打开 TIM2 的 APB1 时钟。
2. 检查是否调用 `TIM_Cmd(TIM2, ENABLE)`。
3. 检查 `TIM_ITConfig()` 是否打开更新中断。
4. 检查 NVIC 是否使能 TIM2_IRQn。
5. 检查中断服务函数名是否为 `TIM2_IRQHandler`。
6. 检查 ISR 是否清除了正确的中断标志。

解决方案：

- 按“时钟源—时基—中断输出—NVIC—启动—ISR”的顺序逐项核对。
- 在调试器中观察 CNT、SR、DIER 和 CR1 寄存器。
- 在 ISR 中临时翻转一个 GPIO，用示波器确认是否进入中断。

### 问题 4：外部时钟模式下 CNT 不变化

现象：

- 遮挡传感器或输入脉冲时，OLED 上 CNT 一直为 0。
- 定时器内部时钟实验正常。

排查过程：

1. 检查传感器 DO 是否连接到 PA0/TIM2_ETR。
2. 检查传感器与 STM32 是否共地。
3. 用万用表、逻辑分析仪或示波器确认 DO 是否真的产生电平跳变。
4. 检查 ETR 极性是否与传感器有效边沿一致。
5. 检查 GPIOA 时钟和 PA0 输入模式。
6. 检查是否启用了引脚重映射，导致 TIM2_ETR 不再位于预期引脚。

解决方案：

- 先用杜邦线或信号发生器向 PA0 输入已知脉冲，隔离传感器问题。
- 根据传感器输出类型选择上拉输入、浮空输入或外部上拉。
- 调整 ETR 极性与滤波参数。

### 问题 5：遮挡一次却计数多次

现象：

- 单次通过对射传感器，CNT 增加 2～5 次。
- 在临界遮挡位置计数持续跳动。

排查过程：

1. 观察传感器 DO 波形是否存在毛刺。
2. 检查比较器阈值是否处于临界位置。
3. 检查 ETR 数字滤波参数是否过小。
4. 检查连接线是否过长或没有可靠共地。
5. 确认物理动作是否同时产生上升沿和下降沿，而配置只应统计其中一种。

解决方案：

- 调整传感器电位器，使高低电平切换干净。
- 适当增大 ETR 数字滤波。
- 缩短信号线并加强电源去耦。
- 根据需要选择正确触发极性。

### 问题 6：OLED 显示的 Num 与 CNT 组合后偶尔跳变

现象：

- 在第 10 个脉冲附近，总数计算偶尔短暂错误。
- Num 已加 1，但 CNT 读取仍来自溢出前，或反之。

排查过程：

1. 检查主循环是否分两次独立读取 Num 和 CNT。
2. 判断读取期间是否可能恰好发生更新中断。
3. 检查变量类型和读写宽度。

解决方案：

- 在严格计量场景中短暂关闭相关中断后读取快照。
- 或采用“高位—CNT—高位”重复读取，只有两次高位一致时才接受结果。
- 简单低频演示可以保持当前结构，但要理解其并发边界。

## 结果展示

> 实验 1：烧录定时器定时中断程序后，OLED 上的 `Num` 每隔约 1 秒自动加 1，主循环不需要调用阻塞式延时。✅

> 实验 2：烧录定时器外部时钟程序后，每产生一个有效传感器脉冲，`CNT` 增加 1；累计到 10 个脉冲时，`CNT` 回到 0，`Num` 增加 1。✅

> 实验 3：主循环可以持续刷新 OLED 或执行其他任务，脉冲累计和周期产生由 TIM2 硬件独立完成。✅

## 本节小结

本节通过“内部时钟定时”和“ETR 外部脉冲计数”两个实验，掌握了 TIM2 基本定时功能的完整使用流程。

最重要的知识点是：

```text
TIM 定时器不仅能测量时间，也能统计外部事件。
```

内部时钟实验中，稳定的 TIM2 时钟经过 PSC 分频后驱动 CNT，CNT 达到 ARR 时产生周期性更新中断；外部时钟实验中，PA0/ETR 上的有效边沿直接驱动 CNT，实现低 CPU 开销的硬件脉冲累计。

完成本节后，应能够独立完成以下任务：

- 根据目标周期计算 PSC 和 ARR。
- 配置 TIM2 更新中断及 NVIC。
- 正确编写并清除更新中断标志。
- 使用 ETR 外部时钟模式 2 统计传感器脉冲。
- 区分定时器硬件计数与 EXTI 逐边沿中断计数。
- 理解滤波、极性、并发读取和计数扩展中的常见问题。

这些知识是后续学习 PWM 输出、输入捕获、编码器接口、频率测量和电机控制的基础。

# 05 — EXTI外部中断：对射式红外传感器计次与旋转编码器计次

## 实验概述

本实验基于 STM32F103C8T6 标准外设库，在理解 EXTI 外部中断工作流程的基础上，完成两个典型的外部脉冲计数实验：

1. 使用对射式红外传感器检测遮挡事件，每次物体穿过光路时计数加 1
2. 使用增量式旋转编码器的 A、B 两相脉冲判断旋转方向，并累计正向或反向步数
3. 使用 OLED 实时显示传感器计数值和编码器位置值

本节的核心目标是掌握外部中断的完整信号链路：

> GPIO 检测边沿 → AFIO 选择端口映射 → EXTI 产生中断请求 → NVIC 仲裁优先级 → CPU 执行中断服务函数

> 资料说明：无法直接获取该视频的完整字幕与逐帧画面。本笔记依据 P12 所在课程章节、课程上下文以及 STM32F103C8T6 标准外设库的典型实现整理；“核心代码”为贴合本节实验的参考实现，不冒充视频逐字源码。

## 硬件连接

以下接线采用该课程常见的 STM32F103C8T6 最小系统板连接方式，具体以实际模块原理图和开发板接线为准。

| 器件 | 对应引脚 | 说明 |
|---|---:|---|
| 对射式红外传感器 DO | PB14 | 数字量输出，遮挡时通常产生下降沿 |
| 旋转编码器 A 相 | PB0 | 接入 EXTI0，用于检测 A 相下降沿 |
| 旋转编码器 B 相 | PB1 | 接入 EXTI1，用于检测 B 相下降沿 |
| OLED SCL | PB8 | 软件 I²C 时钟线，沿用上一节 OLED 驱动 |
| OLED SDA | PB9 | 软件 I²C 数据线，沿用上一节 OLED 驱动 |
| ST-Link V2 | PA13、PA14、GND、3.3V | SWD 下载与调试 |
| 模块电源 | 3.3V、GND | 所有模块必须与 STM32 共地 |

> 注意：部分红外传感器模块和旋转编码器模块可能使用 5V 供电。连接信号线前应确认模块输出电平是否兼容 STM32 的 3.3V 输入。

## 工程文件结构

建议将两个实验分别保存，避免两个工程中的中断服务函数和显示逻辑互相干扰。

```text
05-EXTI-Infrared-Encoder
├── Code
│   ├── InfraredCounter
│   │   ├── main.c
│   │   ├── CountSensor.c
│   │   └── CountSensor.h
│   ├── RotaryEncoder
│   │   ├── main.c
│   │   ├── Encoder.c
│   │   └── Encoder.h
│   ├── OLED.c
│   └── OLED.h
├── Hardware
│   ├── Infrared_Sensor_Wiring.png
│   └── Rotary_Encoder_Wiring.png
├── Results
│   ├── Infrared_Count.gif
│   └── Encoder_Count.gif
└── README.md
```

## 核心代码

> 以下代码基于 STM32F10x 标准外设库编写，并按 Skill V2 要求为每一行非空代码添加中文详细注释。

### 1. CountSensor.h

```c
#ifndef __COUNT_SENSOR_H                                      // 判断计数传感器头文件是否尚未被包含，防止重复声明
#define __COUNT_SENSOR_H                                      // 定义头文件保护宏，使同一编译单元中只展开一次本头文件

#include "stm32f10x.h"                                        // 引入 STM32F10x 设备定义、标准数据类型和外设声明

void CountSensor_Init(void);                                  // 声明红外计数传感器初始化函数，用于配置 GPIO、AFIO、EXTI 和 NVIC
uint16_t CountSensor_Get(void);                               // 声明计数值读取函数，向主程序返回当前累计遮挡次数
void CountSensor_Clear(void);                                 // 声明计数值清零函数，便于重新开始一次测量

#endif                                                        // 结束头文件保护条件编译
```

### 2. CountSensor.c

```c
#include "CountSensor.h"                                      // 引入本模块接口声明，使函数定义与头文件保持一致

static volatile uint16_t CountSensor_Count = 0;               // 定义仅本文件可见的计数变量，volatile 防止主程序与中断共享数据被错误优化

void CountSensor_Init(void)                                   // 定义红外计数传感器初始化函数
{                                                             // 进入初始化函数代码块
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOB, ENABLE);      // 开启 GPIOB 外设时钟，使 PB14 的输入配置能够生效
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_AFIO, ENABLE);       // 开启 AFIO 时钟，使 GPIO 到 EXTI 的端口映射功能能够使用

    GPIO_InitTypeDef GPIO_InitStructure;                       // 定义 GPIO 初始化结构体，用于集中保存 PB14 的配置参数
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_IPU;              // 将 PB14 配置为上拉输入，避免传感器输出悬空时电平不稳定
    GPIO_InitStructure.GPIO_Pin = GPIO_Pin_14;                 // 选择 GPIOB 的第 14 号引脚作为红外传感器数字输入
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;          // 填写输出速度字段以完整初始化结构体，输入模式下该字段实际不起作用
    GPIO_Init(GPIOB, &GPIO_InitStructure);                     // 将上述配置写入 GPIOB 寄存器，完成 PB14 输入初始化

    GPIO_EXTILineConfig(GPIO_PortSourceGPIOB, GPIO_PinSource14); // 将 EXTI14 的输入源映射到 GPIOB 的 PB14，而不是其他端口的 14 号引脚

    EXTI_InitTypeDef EXTI_InitStructure;                       // 定义 EXTI 初始化结构体，用于配置中断线、触发边沿和工作模式
    EXTI_InitStructure.EXTI_Line = EXTI_Line14;                // 选择 EXTI14，使 PB14 的电平变化能够产生外部中断请求
    EXTI_InitStructure.EXTI_LineCmd = ENABLE;                  // 使能 EXTI14，允许该中断线向 NVIC 提交中断请求
    EXTI_InitStructure.EXTI_Mode = EXTI_Mode_Interrupt;        // 配置为中断模式，边沿到来时请求 CPU 执行中断服务函数
    EXTI_InitStructure.EXTI_Trigger = EXTI_Trigger_Falling;    // 选择下降沿触发，适配遮挡时输出由高电平变为低电平的常见模块
    EXTI_Init(&EXTI_InitStructure);                            // 将 EXTI 配置写入控制器寄存器，完成外部中断线初始化

    NVIC_PriorityGroupConfig(NVIC_PriorityGroup_2);            // 设置优先级分组 2，将四位优先级划分为两位抢占和两位响应优先级

    NVIC_InitTypeDef NVIC_InitStructure;                       // 定义 NVIC 初始化结构体，用于配置中断通道和优先级
    NVIC_InitStructure.NVIC_IRQChannel = EXTI15_10_IRQn;       // 选择 EXTI10 至 EXTI15 共用的中断通道，EXTI14 属于该通道
    NVIC_InitStructure.NVIC_IRQChannelCmd = ENABLE;            // 使能该 NVIC 通道，使 CPU 可以响应 EXTI14 中断请求
    NVIC_InitStructure.NVIC_IRQChannelPreemptionPriority = 1;  // 设置抢占优先级为 1，数值越小表示优先级越高
    NVIC_InitStructure.NVIC_IRQChannelSubPriority = 1;         // 设置响应优先级为 1，用于抢占级相同时决定服务顺序
    NVIC_Init(&NVIC_InitStructure);                            // 将中断通道和优先级配置写入 NVIC
}                                                             // 结束红外计数传感器初始化函数

uint16_t CountSensor_Get(void)                                // 定义计数值读取函数
{                                                             // 进入计数值读取函数代码块
    return CountSensor_Count;                                 // 返回由中断服务函数累计的当前遮挡次数
}                                                             // 结束计数值读取函数

void CountSensor_Clear(void)                                  // 定义计数值清零函数
{                                                             // 进入计数值清零函数代码块
    __disable_irq();                                          // 临时关闭全局中断，避免清零过程中计数中断同时修改共享变量
    CountSensor_Count = 0;                                    // 将累计遮挡次数清零，开始新一轮计数
    __enable_irq();                                           // 恢复全局中断响应，使后续传感器边沿仍可被记录
}                                                             // 结束计数值清零函数

void EXTI15_10_IRQHandler(void)                               // 定义 EXTI10 至 EXTI15 共用的中断服务函数，函数名必须与启动文件一致
{                                                             // 进入共用中断服务函数代码块
    if (EXTI_GetITStatus(EXTI_Line14) == SET)                 // 检查 EXTI14 挂起标志，确认本次中断确实由 PB14 对应线路触发
    {                                                         // 进入 EXTI14 中断处理分支
        CountSensor_Count++;                                  // 每检测到一次有效下降沿就将遮挡计数值增加 1
        EXTI_ClearITPendingBit(EXTI_Line14);                   // 清除 EXTI14 挂起标志，防止退出后立即重复进入中断
    }                                                         // 结束 EXTI14 中断处理分支
}                                                             // 结束 EXTI15_10 共用中断服务函数
```

### 3. 对射式红外传感器计次 main.c

```c
#include "stm32f10x.h"                                        // 引入 STM32F10x 芯片定义和标准外设库接口
#include "OLED.h"                                             // 引入 OLED 显示驱动接口，用于显示累计计数值
#include "CountSensor.h"                                      // 引入红外计数传感器接口，用于初始化和读取遮挡次数

int main(void)                                                // 定义程序入口函数，系统复位后从此处开始执行
{                                                             // 进入主函数代码块
    OLED_Init();                                              // 初始化 OLED 对应 GPIO、通信时序和显示控制器
    CountSensor_Init();                                       // 初始化 PB14、AFIO、EXTI14 和 NVIC 中断通道

    OLED_ShowString(1, 1, "Count:");                          // 在 OLED 第一行第一列显示固定标签 Count
    OLED_ShowString(2, 1, "IR Sensor");                       // 在 OLED 第二行显示当前实验名称，便于区分测试项目

    while (1)                                                 // 进入无限循环，使主程序持续刷新显示内容
    {                                                         // 进入主循环代码块
        OLED_ShowNum(1, 7, CountSensor_Get(), 5);             // 读取中断累计值并以五位十进制数显示在 Count 标签后
    }                                                         // 结束一次主循环并立即进入下一次刷新
}                                                             // 结束主函数，嵌入式程序正常情况下不会执行到此处
```

### 4. Encoder.h

```c
#ifndef __ENCODER_H                                           // 判断编码器头文件是否尚未展开，避免重复定义接口
#define __ENCODER_H                                           // 定义编码器头文件保护宏

#include "stm32f10x.h"                                        // 引入 STM32F10x 数据类型、寄存器定义和内核函数声明

void Encoder_Init(void);                                      // 声明旋转编码器初始化函数，用于配置两路 GPIO 和外部中断
int16_t Encoder_Get(void);                                    // 声明增量读取函数，返回自上次读取以来的正负旋转步数

#endif                                                        // 结束编码器头文件保护条件编译
```

### 5. Encoder.c

```c
#include "Encoder.h"                                          // 引入编码器模块接口声明，保证函数原型和定义一致

static volatile int16_t Encoder_Count = 0;                    // 保存中断累计的有符号增量，volatile 确保每次都访问真实内存值

void Encoder_Init(void)                                       // 定义旋转编码器初始化函数
{                                                             // 进入编码器初始化函数代码块
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOB, ENABLE);      // 开启 GPIOB 时钟，使 PB0 和 PB1 的输入配置能够生效
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_AFIO, ENABLE);       // 开启 AFIO 时钟，使 EXTI0 和 EXTI1 可以映射到 GPIOB

    GPIO_InitTypeDef GPIO_InitStructure;                       // 定义 GPIO 初始化结构体，统一配置编码器 A、B 两相信号
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_IPU;              // 将两路输入配置为上拉输入，适配机械编码器触点接地的常见接法
    GPIO_InitStructure.GPIO_Pin = GPIO_Pin_0 | GPIO_Pin_1;     // 同时选择 PB0 和 PB1，分别连接编码器 A 相和 B 相
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;          // 完整填写结构体字段，输入模式下速度字段不会改变采样速度
    GPIO_Init(GPIOB, &GPIO_InitStructure);                     // 将配置写入 GPIOB，完成两相信号输入初始化

    GPIO_EXTILineConfig(GPIO_PortSourceGPIOB, GPIO_PinSource0); // 将 EXTI0 的输入源选择为 PB0，对应编码器 A 相
    GPIO_EXTILineConfig(GPIO_PortSourceGPIOB, GPIO_PinSource1); // 将 EXTI1 的输入源选择为 PB1，对应编码器 B 相

    EXTI_InitTypeDef EXTI_InitStructure;                       // 定义 EXTI 初始化结构体，用于设置两条中断线的公共参数
    EXTI_InitStructure.EXTI_Line = EXTI_Line0 | EXTI_Line1;    // 同时选择 EXTI0 和 EXTI1，分别监测编码器 A、B 相
    EXTI_InitStructure.EXTI_LineCmd = ENABLE;                  // 使能两条外部中断线，使边沿事件能够向 NVIC 发出请求
    EXTI_InitStructure.EXTI_Mode = EXTI_Mode_Interrupt;        // 选择中断模式，让 CPU 在有效边沿出现时执行服务函数
    EXTI_InitStructure.EXTI_Trigger = EXTI_Trigger_Falling;    // 选择下降沿触发，每相由高变低时进行一次方向判断
    EXTI_Init(&EXTI_InitStructure);                            // 将两条 EXTI 线路的配置写入外部中断控制器

    NVIC_PriorityGroupConfig(NVIC_PriorityGroup_2);            // 设置优先级分组 2，提供抢占优先级和响应优先级两级配置

    NVIC_InitTypeDef NVIC_InitStructure;                       // 定义 NVIC 初始化结构体，依次配置 EXTI0 和 EXTI1 通道
    NVIC_InitStructure.NVIC_IRQChannel = EXTI0_IRQn;           // 选择 EXTI0 独立中断通道，对应编码器 A 相下降沿
    NVIC_InitStructure.NVIC_IRQChannelCmd = ENABLE;            // 使能 EXTI0 通道，使 A 相边沿能够打断主程序
    NVIC_InitStructure.NVIC_IRQChannelPreemptionPriority = 1;  // 将 EXTI0 抢占优先级设置为 1
    NVIC_InitStructure.NVIC_IRQChannelSubPriority = 1;         // 将 EXTI0 响应优先级设置为 1
    NVIC_Init(&NVIC_InitStructure);                            // 把 EXTI0 通道配置写入 NVIC

    NVIC_InitStructure.NVIC_IRQChannel = EXTI1_IRQn;           // 将待配置通道切换为 EXTI1，对应编码器 B 相下降沿
    NVIC_InitStructure.NVIC_IRQChannelCmd = ENABLE;            // 使能 EXTI1 通道，使 B 相边沿能够打断主程序
    NVIC_InitStructure.NVIC_IRQChannelPreemptionPriority = 1;  // 将 EXTI1 抢占优先级设置为 1，与 EXTI0 保持同一抢占级
    NVIC_InitStructure.NVIC_IRQChannelSubPriority = 2;         // 将 EXTI1 响应优先级设为 2，在同时挂起时排在 EXTI0 之后
    NVIC_Init(&NVIC_InitStructure);                            // 把 EXTI1 通道配置写入 NVIC
}                                                             // 结束旋转编码器初始化函数

int16_t Encoder_Get(void)                                     // 定义编码器增量读取函数
{                                                             // 进入编码器增量读取函数代码块
    int16_t Temp;                                             // 定义临时变量，用于保存读取瞬间的旋转增量
    __disable_irq();                                          // 临时关闭全局中断，避免读取与清零之间发生新的计数而丢失增量
    Temp = Encoder_Count;                                     // 将中断累计的正负步数复制到局部变量
    Encoder_Count = 0;                                        // 清零模块内部增量，使下一次读取只返回新的旋转变化
    __enable_irq();                                           // 恢复全局中断响应，继续接收编码器 A、B 相边沿
    return Temp;                                              // 返回本次获取到的有符号旋转增量
}                                                             // 结束编码器增量读取函数

void EXTI0_IRQHandler(void)                                   // 定义 EXTI0 中断服务函数，用于处理编码器 A 相下降沿
{                                                             // 进入 EXTI0 中断服务函数代码块
    if (EXTI_GetITStatus(EXTI_Line0) == SET)                  // 检查 EXTI0 挂起标志，确认中断来源是编码器 A 相
    {                                                         // 进入 A 相有效中断处理分支
        if (GPIO_ReadInputDataBit(GPIOB, GPIO_Pin_1) == 0)    // 在 A 相下降沿时读取 B 相，利用两相相位关系判断旋转方向
        {                                                     // 进入符合该方向组合的处理分支
            Encoder_Count--;                                  // 将增量减 1，表示检测到约定方向的一步旋转
        }                                                     // 结束 A 相方向判断分支
        EXTI_ClearITPendingBit(EXTI_Line0);                    // 清除 EXTI0 挂起标志，允许下一次 A 相边沿再次触发
    }                                                         // 结束 EXTI0 有效中断处理分支
}                                                             // 结束 EXTI0 中断服务函数

void EXTI1_IRQHandler(void)                                   // 定义 EXTI1 中断服务函数，用于处理编码器 B 相下降沿
{                                                             // 进入 EXTI1 中断服务函数代码块
    if (EXTI_GetITStatus(EXTI_Line1) == SET)                  // 检查 EXTI1 挂起标志，确认中断来源是编码器 B 相
    {                                                         // 进入 B 相有效中断处理分支
        if (GPIO_ReadInputDataBit(GPIOB, GPIO_Pin_0) == 0)    // 在 B 相下降沿时读取 A 相，通过相位先后关系判断相反方向
        {                                                     // 进入符合该方向组合的处理分支
            Encoder_Count++;                                  // 将增量加 1，表示检测到约定正方向的一步旋转
        }                                                     // 结束 B 相方向判断分支
        EXTI_ClearITPendingBit(EXTI_Line1);                    // 清除 EXTI1 挂起标志，避免同一事件被重复处理
    }                                                         // 结束 EXTI1 有效中断处理分支
}                                                             // 结束 EXTI1 中断服务函数
```

### 6. 旋转编码器计次 main.c

```c
#include "stm32f10x.h"                                        // 引入 STM32F10x 芯片定义和标准外设库接口
#include "OLED.h"                                             // 引入 OLED 显示接口，用于显示累计位置值
#include "Encoder.h"                                          // 引入旋转编码器接口，用于初始化和读取方向增量

int main(void)                                                // 定义嵌入式程序入口函数
{                                                             // 进入主函数代码块
    int16_t Num = 0;                                         // 定义有符号累计位置变量，使正转和反转都能正确表示

    OLED_Init();                                              // 初始化 OLED 显示屏及其通信接口
    Encoder_Init();                                           // 初始化编码器 A、B 相输入、EXTI0/1 和 NVIC 通道

    OLED_ShowString(1, 1, "Num:");                            // 在 OLED 第一行显示累计位置标签
    OLED_ShowString(2, 1, "Encoder");                         // 在 OLED 第二行显示实验名称

    while (1)                                                 // 进入无限循环，持续处理编码器增量并刷新屏幕
    {                                                         // 进入主循环代码块
        Num += Encoder_Get();                                 // 读取自上次循环后的正负增量，并累加到长期位置变量
        OLED_ShowSignedNum(1, 5, Num, 5);                     // 以带符号五位十进制形式显示当前累计位置
    }                                                         // 结束一次主循环并继续下一次读取
}                                                             // 结束主函数，正常运行时程序不会从此处返回
```

## 代码要点

| 行/段 | 说明 |
|---|---|
| `RCC_APB2PeriphClockCmd(RCC_APB2Periph_AFIO, ENABLE)` | EXTI 的 GPIO 端口映射由 AFIO 管理；只开启 GPIO 时钟而忘记 AFIO 时钟，映射配置不会正确工作。 |
| `GPIO_Mode_IPU` | 使用内部上拉让空闲状态保持高电平，适合低电平有效的红外模块和机械编码器触点。 |
| `GPIO_EXTILineConfig()` | 选择某条 EXTI 线来自哪个 GPIO 端口；EXTI14 只能在 PA14、PB14、PC14 等同号引脚中选择一个来源。 |
| `EXTI_Trigger_Falling` | 在输入由高变低时触发。若模块输出逻辑相反，应根据实际波形改为上升沿或双边沿。 |
| `EXTI15_10_IRQn` | EXTI10～EXTI15 共用一个 NVIC 通道，因此服务函数中必须检查具体线路的挂起标志。 |
| `EXTI_GetITStatus()` | 判断指定 EXTI 线是否真的产生了待处理的中断，尤其适用于共享中断入口。 |
| `EXTI_ClearITPendingBit()` | 清除中断挂起标志；遗漏此操作会造成中断反复进入，主程序看起来像“卡死”。 |
| `volatile` | 告诉编译器该变量可能被中断异步修改，不能只使用寄存器缓存值。 |
| `__disable_irq()` / `__enable_irq()` | 构成简短临界区，保证“读取后清零”或“清零计数”不会与中断更新交叉。 |
| `Encoder_Get()` | 返回一段时间内的增量而不是永久累计值，主程序可决定如何积分、限幅或换算角度。 |
| `GPIO_ReadInputDataBit()` | 在某一相边沿发生时读取另一相电平，通过 A、B 两相的相位先后关系判断方向。 |
| `OLED_ShowSignedNum()` | 编码器累计值可能为负数，因此应使用带符号显示函数。 |

## 关键知识点

### 1. 外部中断完整信号链路

#### 原理

STM32 的 GPIO 电平变化不会直接跳转到中断服务函数，而是依次经过多个硬件模块：

```text
外部信号 → GPIO 输入采样 → AFIO 端口映射 → EXTI 边沿检测
        → NVIC 优先级仲裁 → Cortex-M3 响应 → 中断服务函数
```

GPIO 负责读取引脚电平；AFIO 决定某条 EXTI 线连接哪个端口；EXTI 检测上升沿或下降沿并置位挂起标志；NVIC 决定 CPU 是否响应以及先处理哪个中断。

#### 特点

- 外部事件到来时可以立即打断主程序，响应速度通常优于持续轮询。
- CPU 不需要不断读取 GPIO，主循环可以同时处理显示、通信或控制任务。
- 一次正确配置涉及 RCC、GPIO、AFIO、EXTI、NVIC 五个环节。
- 中断服务函数名称必须与启动文件中的向量表名称完全一致。

#### 面试易问

**Q：STM32 配置 GPIO 外部中断需要经过哪些步骤？**

A：开启 GPIO 和 AFIO 时钟，配置 GPIO 输入模式，使用 `GPIO_EXTILineConfig()` 完成端口映射，配置 EXTI 的线路、模式和触发边沿，配置 NVIC 通道及优先级，最后编写正确名称的中断服务函数并清除挂起标志。

**Q：外部中断与 GPIO 轮询有什么区别？**

A：轮询由 CPU 主动、反复读取引脚；中断由硬件在边沿出现时通知 CPU。轮询简单但占用 CPU，且轮询周期过长可能漏掉窄脉冲；中断响应更及时，但程序结构和共享数据处理更复杂。

#### 易错点

- 只配置 GPIO 和 EXTI，却忘记开启 AFIO 时钟。
- 中断函数名拼写错误，导致向量表无法进入自定义函数。
- 配置了错误的 EXTI 线路或错误的 GPIO 端口映射。
- 在中断服务函数末尾忘记清除挂起标志。

---

### 2. EXTI 线路与 GPIO 端口映射

#### 原理

EXTI0～EXTI15 分别对应 GPIO 的引脚编号 0～15，而不是固定对应某个端口。

例如：

```text
EXTI0  可选择 PA0、PB0、PC0……
EXTI1  可选择 PA1、PB1、PC1……
EXTI14 可选择 PA14、PB14、PC14……
```

AFIO_EXTICR 寄存器用于选择每条 EXTI 线的端口来源。因此，同一时刻 EXTI0 不能同时独立接收 PA0 和 PB0。

#### 特点

- EXTI 线号由 GPIO 引脚编号决定。
- 端口来源由 AFIO 进行选择。
- 同号引脚共享同一条 EXTI 线。
- 映射配置只决定来源，不负责设置 GPIO 输入模式。

#### 面试易问

**Q：PB14 应该配置成 EXTI_Line14 还是 EXTI_Line_B14？**

A：应配置为 `EXTI_Line14`。EXTI 只按引脚编号划分线路，PB14 的端口 B 信息由 `GPIO_EXTILineConfig(GPIO_PortSourceGPIOB, GPIO_PinSource14)` 单独指定。

**Q：PA0 和 PB0 能否同时作为两个独立的外部中断输入？**

A：不能直接同时映射到 EXTI0，因为它们共用同一条 EXTI0。需要更换其中一个引脚编号，或使用其他外设和软件方案区分。

#### 易错点

- 把 GPIO 端口号和 EXTI 线号混为一谈。
- 使用 `GPIO_Pin_14` 代替 `GPIO_PinSource14` 作为映射函数参数。
- 同时尝试把两个同号引脚映射到同一条 EXTI 线。
- 修改映射后没有重新确认硬件接线。

---

### 3. EXTI 中断通道分组

#### 原理

EXTI0～EXTI4 在 NVIC 中拥有独立中断通道；EXTI5～EXTI9 共用一个通道；EXTI10～EXTI15 共用另一个通道。

```text
EXTI0  → EXTI0_IRQn
EXTI1  → EXTI1_IRQn
……
EXTI5～9   → EXTI9_5_IRQn
EXTI10～15 → EXTI15_10_IRQn
```

因此 PB14 对应的 EXTI14 必须使用 `EXTI15_10_IRQHandler()`，并在函数内部判断 `EXTI_Line14` 是否挂起。

#### 特点

- 低编号 EXTI0～4 的入口彼此独立。
- 高编号中断线共享 NVIC 通道和中断服务函数。
- 共用入口中可能同时存在多个挂起标志。
- 服务函数应逐一判断并清除所有可能的中断源。

#### 面试易问

**Q：为什么配置 EXTI14，却要编写 EXTI15_10_IRQHandler？**

A：因为 EXTI10～EXTI15 在 NVIC 中共用 `EXTI15_10_IRQn` 通道和同一个中断入口。进入后再通过挂起标志判断具体是哪条线路触发。

**Q：共享中断入口中只清除一条线路会怎样？**

A：若另一条线路仍处于挂起状态，退出中断后 CPU 可能立即再次进入该共享入口，造成高频重复中断。

#### 易错点

- 为 EXTI14 错写 `EXTI14_IRQHandler()`，启动文件中通常不存在该入口。
- 共享入口中不检查中断标志就直接执行计数。
- 只清除部分挂起标志。
- 在共享入口中写过长的阻塞逻辑。

---

### 4. 上升沿、下降沿与双边沿触发

#### 原理

EXTI 通过比较输入信号前后状态检测边沿：

- 上升沿：低电平变为高电平
- 下降沿：高电平变为低电平
- 双边沿：上升沿和下降沿都触发

对射式红外模块在无遮挡时通常输出一种稳定电平，遮挡后翻转为另一电平。应根据模块实际输出波形选择触发方式，而不是机械照抄代码。

#### 特点

- 单边沿计数可以让一次状态变化只产生一次中断。
- 双边沿可同时检测进入和离开，但一次遮挡可能计数两次。
- 机械触点可能在一个动作中产生多个抖动边沿。
- 触发极性应通过原理图、万用表或逻辑分析仪确认。

#### 面试易问

**Q：为什么红外计数常用下降沿而不是低电平触发？**

A：边沿触发只在状态变化瞬间产生一次请求；若使用持续电平触发，信号保持有效期间可能不断申请中断，不适合“一次遮挡计一次”的需求。

**Q：什么时候使用双边沿触发？**

A：需要同时记录信号进入和退出、测量高低电平持续时间，或不能预先确定有效边沿时可使用双边沿；但必须在软件中区分当前电平并避免重复计数。

#### 易错点

- 传感器输出逻辑与代码相反，导致遮挡时完全不计数。
- 使用双边沿后一次遮挡被累计两次。
- 输入悬空产生大量随机边沿。
- 未处理机械编码器触点抖动。

---

### 5. NVIC 优先级与优先级分组

#### 原理

NVIC 使用抢占优先级和响应优先级管理多个中断：

- 抢占优先级决定一个中断能否打断另一个正在执行的中断。
- 抢占优先级相同时，响应优先级决定多个挂起中断的先后顺序。
- 优先级数值越小，实际优先级越高。

`NVIC_PriorityGroupConfig()` 决定优先级位如何在抢占优先级和响应优先级之间分配。

#### 特点

- 同一工程应统一设置一次优先级分组。
- 只有抢占优先级更高的中断才能形成嵌套。
- 响应优先级不能打断正在执行的同级中断。
- 简单实验中可让相关中断处于同一抢占等级。

#### 面试易问

**Q：抢占优先级和响应优先级的区别是什么？**

A：抢占优先级决定中断嵌套能力；响应优先级只在抢占优先级相同且多个中断同时等待时决定先后顺序。

**Q：优先级数值 0 和 3，哪个更高？**

A：数值 0 的优先级更高。STM32 NVIC 的优先级数值越小，响应级别越高。

#### 易错点

- 在不同模块中反复设置不同的优先级分组。
- 误以为响应优先级高就能打断正在执行的中断。
- 优先级超出当前分组允许的取值范围。
- 把“数字更大”误认为“优先级更高”。

---

### 6. 中断服务函数设计

#### 原理

中断服务函数负责快速处理异步事件。进入中断后，CPU 会保存必要现场，执行 ISR，再恢复主程序。

良好的 ISR 通常只做以下事情：

```text
确认中断源 → 读取必要状态 → 更新标志或计数 → 清除挂起位 → 尽快退出
```

耗时显示、延时、复杂计算和通信应尽量留在主循环中完成。

#### 特点

- ISR 应短小、确定、不可长时间阻塞。
- 共享中断入口必须判断具体中断源。
- 中断标志通常需要软件显式清除。
- ISR 与主程序共享的变量应考虑 `volatile` 和原子性。

#### 面试易问

**Q：为什么不建议在中断服务函数中调用 Delay_ms？**

A：阻塞延时会长时间占用中断上下文，推迟其他中断和主程序执行，降低系统实时性，甚至导致丢事件或死锁。

**Q：中断服务函数中可以刷新 OLED 吗？**

A：技术上可能可以，但不建议。OLED 通信通常耗时较长，应只在 ISR 中更新计数或设置标志，再由主循环刷新屏幕。

#### 易错点

- ISR 中执行大量字符串显示和浮点运算。
- ISR 中使用阻塞式延时。
- 未判断标志就修改计数。
- 清除挂起位的顺序和对象错误。

---

### 7. 对射式红外传感器计次

#### 原理

对射式红外传感器由相对放置的发射端和接收端组成。无遮挡时接收端能检测到红外光；物体穿过时光路被切断，模块的数字输出发生跳变。

将数字输出连接到 EXTI 后，每次有效边沿都会进入中断服务函数，从而实现物体数量、转盘槽口或通过次数的累计。

#### 特点

- 非接触检测，机械磨损小。
- 响应速度通常高于人工按键。
- 数字模块可通过比较器直接输出高低电平。
- 检测可靠性受安装位置、环境光、物体尺寸和移动速度影响。

#### 面试易问

**Q：为什么红外传感器计数适合使用外部中断？**

A：物体经过产生的脉冲可能较窄，外部中断可在边沿出现时立即响应，降低主循环忙于其他任务时漏检的概率。

**Q：计数值为什么要声明为 volatile？**

A：计数值由中断服务函数异步修改，而主循环随时读取。`volatile` 要求编译器每次从内存重新访问，避免使用过期的寄存器缓存。

#### 易错点

- 发射端与接收端没有对准，输出始终不变化。
- 模块阈值电位器设置不合适，输出在临界点抖动。
- 物体边缘反复遮挡造成一次通过多次计数。
- 计数变量位宽过小，长时间运行后发生溢出。

---

### 8. 增量式旋转编码器与正交判向

#### 原理

增量式旋转编码器通常输出相差约 90° 的 A、B 两相信号。旋转方向不同，两相边沿出现的先后顺序相反。

一种简单判向方法是：

```text
A 相下降沿到来时读取 B 相：
- B 为低：判定一个方向

B 相下降沿到来时读取 A 相：
- A 为低：判定相反方向
```

通过两个外部中断分别检测 A、B 相下降沿，即可把旋转过程转换为正负增量。

#### 特点

- 可同时获得旋转步数和方向。
- 输出是相对位移，掉电后通常不知道绝对位置。
- 每个机械卡点可能对应多个电气边沿，分辨率取决于解码方式。
- 软件可选择一倍频、二倍频或四倍频计数。

#### 面试易问

**Q：旋转编码器为什么需要 A、B 两相信号？**

A：单路脉冲只能判断发生了移动，无法区分方向；A、B 两相具有固定相位差，通过比较谁领先谁滞后即可判断正转或反转。

**Q：一倍频、二倍频、四倍频解码有什么区别？**

A：一倍频只统计某一相的一种边沿；二倍频统计某一相的两种边沿或两相各一种边沿；四倍频统计 A、B 两相的全部上升沿和下降沿。倍频越高分辨率越高，但对抖动和处理速度要求也更高。

#### 易错点

- A、B 相接反导致正负方向与预期相反。
- 机械抖动造成多次计数。
- 只检测单相而又试图判断方向。
- 快速旋转时 ISR 太慢，导致边沿丢失。

---

### 9. volatile、原子性与共享变量

#### 原理

`volatile` 解决的是“每次都要真实读写内存”的编译优化问题，但它不自动保证多步操作的原子性。

例如：

```text
Temp = Encoder_Count;
Encoder_Count = 0;
```

两行之间若发生编码器中断，新增加的计数可能在第二行清零时丢失。因此参考代码使用短暂关闭中断的临界区完成“读取并清零”。

#### 特点

- `volatile` 适合标记 ISR、DMA 或硬件可能异步修改的变量。
- 单次对齐读写可能是原子的，但“读—改—写”组合通常不是。
- 临界区应尽可能短。
- 更复杂系统可使用原子操作、互斥机制或消息队列。

#### 面试易问

**Q：volatile 能保证线程安全吗？**

A：不能。`volatile` 只限制编译器优化和访问缓存，不保证复合操作原子性，也不提供互斥或内存同步语义。

**Q：为什么 Encoder_Get 要在读取和清零时关闭中断？**

A：为了让“取得当前增量”和“把内部增量清零”成为不可被编码器 ISR 插入的整体，避免新脉冲计数在两步之间被覆盖。

#### 易错点

- 认为添加 `volatile` 后所有并发问题都会消失。
- 临界区中执行耗时操作，导致系统长时间无法响应中断。
- 关闭中断后因分支或提前返回而忘记恢复。
- 主程序直接修改 ISR 共享变量却不考虑竞争条件。

---

## 本节核心记忆

```text
外部中断链路：
GPIO → AFIO → EXTI → NVIC → CPU → ISR
```

```text
EXTI 线号由引脚编号决定：
PB14 → EXTI14，PB0 → EXTI0，PB1 → EXTI1
```

```text
EXTI0～4 独立通道
EXTI5～9 共用 EXTI9_5_IRQn
EXTI10～15 共用 EXTI15_10_IRQn
```

```text
中断服务函数：
先判断来源 → 快速处理 → 清除挂起标志 → 尽快退出
```

```text
旋转编码器判向：
在一相边沿到来时读取另一相电平，利用 A/B 相位先后确定正反方向
```

```text
volatile 只保证真实访问，不保证复合操作原子性
```

## 开发过程总结

### 问题 1：遮挡红外传感器，但计数始终不增加

现象：

- OLED 可以正常显示
- 遮挡光路后计数仍为 0
- GPIO 直接读取可能有变化，但没有进入中断

排查过程：

1. 检查 PB14 是否配置为输入模式
2. 检查 GPIOB 和 AFIO 时钟是否都已开启
3. 检查 `GPIO_EXTILineConfig()` 是否映射到 GPIOB、PinSource14
4. 检查 EXTI 是否选择 `EXTI_Line14`
5. 检查 NVIC 通道是否使用 `EXTI15_10_IRQn`
6. 检查 ISR 名称是否为 `EXTI15_10_IRQHandler`
7. 检查传感器输出在遮挡时到底产生上升沿还是下降沿

解决方案：

- 补充 AFIO 时钟和正确的端口映射
- 按实际输出逻辑调整 `EXTI_Trigger_Falling` 或 `EXTI_Trigger_Rising`
- 使用万用表、示波器或逻辑分析仪确认 PB14 的电平变化
- 确认启动文件使用的中断函数名称与代码完全一致

### 问题 2：物体只经过一次，计数却增加多次

现象：

- 单次遮挡出现 2～数十次计数
- 慢速移动或停在临界位置时尤其明显
- 传感器输出在阈值附近反复跳变

排查过程：

1. 检查模块阈值电位器是否处于临界位置
2. 检查传感器安装是否牢固且发射、接收端对准
3. 观察输出波形是否存在毛刺
4. 检查是否误配置为双边沿触发
5. 检查外部供电和共地是否稳定

解决方案：

- 重新调整比较器阈值，让高低电平切换更明确
- 使用单边沿触发，避免进入和离开各计一次
- 增加硬件施密特触发、RC 滤波或软件时间窗
- 对高速计数场景优先考虑定时器输入捕获或外部计数模式

### 问题 3：旋转编码器方向与预期相反

现象：

- 顺时针旋转时数值减少
- 逆时针旋转时数值增加
- 计数步数基本正常，但符号相反

排查过程：

1. 检查 A、B 两相是否接反
2. 检查两个 ISR 中的加减号是否与接线定义一致
3. 确认编码器公共端和上拉方式
4. 观察 A、B 两相谁领先谁滞后

解决方案：

- 交换 A、B 两相信号线
- 或交换 `Encoder_Count++` 与 `Encoder_Count--`
- 在 README 中固定说明项目采用的“正方向”定义

### 问题 4：旋转编码器轻轻转动一次，数值跳变很多

现象：

- 每个机械卡点出现多个增量
- 停在某一位置时偶尔仍继续计数
- 不同旋转速度下计数差异明显

排查过程：

1. 确认编码器是机械式还是光电式
2. 观察 A、B 相是否存在触点抖动
3. 检查当前使用的是一倍频、二倍频还是四倍频逻辑
4. 检查输入上拉是否可靠
5. 检查 ISR 是否同时对不完整状态进行了计数

解决方案：

- 使用状态机查表法进行完整正交解码
- 加入适当的硬件或软件消抖
- 根据每个卡点对应的边沿数对结果进行换算
- 对高速编码器改用定时器编码器接口模式

### 问题 5：程序进入中断后主循环像“卡死”

现象：

- OLED 停止刷新
- 调试时程序反复停在同一个 ISR
- 一触发传感器后系统不再正常运行

排查过程：

1. 检查是否调用 `EXTI_ClearITPendingBit()`
2. 检查清除的线路是否与实际触发线路一致
3. 检查输入是否持续产生毛刺
4. 检查 ISR 内是否有死循环或阻塞延时
5. 检查共享入口内是否还有其他挂起标志未处理

解决方案：

- 在每个有效中断分支中正确清除对应挂起位
- 缩短 ISR，只更新计数或标志
- 对输入信号进行滤波和阈值调整
- 在共享 ISR 中逐项检查并清除所有可能线路

## 结果展示

> 实验 1：烧录红外计数程序后，每当物体完整穿过对射式红外传感器的光路，OLED 上的 `Count` 数值增加 1。✅

> 实验 2：烧录旋转编码器程序后，沿约定正方向旋转时 OLED 数值递增，沿反方向旋转时数值递减。✅

> 实验 3：主循环只负责读取计数和刷新 OLED，外部脉冲由 EXTI 中断异步捕获，体现了中断方式相较 GPIO 轮询的实时性。✅

## 本节小结

本节通过对射式红外传感器和旋转编码器两个实验，把 EXTI 外部中断从配置流程落实到了实际应用。

最重要的知识点是：

```text
GPIO 负责输入电平
AFIO 负责选择端口
EXTI 负责检测边沿并产生请求
NVIC 负责优先级和通道管理
ISR 负责快速处理事件并清除标志
```

对射式红外传感器展示了“单路脉冲累计”的基本方法；旋转编码器进一步展示了如何利用两路正交信号判断方向。掌握本节后，可以继续学习定时器外部时钟、输入捕获、编码器接口和更高频率的硬件计数方案。

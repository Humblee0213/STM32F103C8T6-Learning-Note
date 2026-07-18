# 14 — PWM驱动：LED呼吸灯、舵机与直流电机

## 实验概述

本节基于 STM32F103C8T6 标准外设库，在上一节 TIM 输出比较的基础上，进一步使用定时器硬件产生 PWM 波形，并完成三个典型应用：

1. 使用 TIM2_CH1 输出 PWM，通过改变占空比实现 LED 呼吸灯。
2. 使用 TIM2_CH2 输出 50Hz 舵机控制脉冲，通过改变高电平脉宽控制舵机角度。
3. 使用 TIM2_CH3 输出 PWM，配合电机驱动模块的方向控制引脚，实现直流电机调速与正反转。

本节的核心目标是把“输出比较”真正落到 PWM 应用中：

> 定时器持续计数 → CNT 与 CCR 比较 → 硬件自动翻转输出状态 → 形成 PWM → 修改 CCR 改变占空比或脉宽

本节视频为 **[6-4] PWM驱动LED呼吸灯 & PWM驱动舵机 & PWM驱动直流电机**。由于无法直接获取视频的完整字幕与逐帧画面，以下代码按照该课程的标准库编程风格、公开课程笔记与 STM32F103 定时器工作机制整理为**参考实现**；具体器件型号、供电方式和接线仍应以实际开发板原理图与模块规格书为准。

---

## 硬件连接

### 实验 1：PWM 驱动 LED 呼吸灯

| 器件 | 对应引脚 | 说明 |
|---|---:|---|
| LED | PA0 / TIM2_CH1 | 使用 TIM2 通道 1 输出 PWM |
| LED 限流电阻 | 与 LED 串联 | 防止 LED 和 GPIO 过流 |
| ST-Link V2 | PA13、PA14、GND、3.3V | SWD 下载与调试 |

> 如果 LED 采用低电平点亮接法，亮度变化方向可能与占空比变化方向相反，可根据实际硬件调整 PWM 极性或程序中的比较值变化方向。

### 实验 2：PWM 驱动舵机

| 器件 | 对应引脚 | 说明 |
|---|---:|---|
| 舵机信号线 | PA1 / TIM2_CH2 | 输出约 50Hz 的控制脉冲 |
| 舵机电源 | 5V 或模块规定电压 | 不建议直接由 MCU GPIO 给舵机供电 |
| 舵机 GND | GND | 必须与 STM32 共地 |
| 按键模块 | 复用前文 Key 模块 | 用于切换舵机目标角度 |
| OLED | 复用前文 OLED 模块 | 显示当前角度，可选 |

> 常见小型舵机通常使用约 20ms 周期的控制帧，通过约 0.5ms～2.5ms 的高电平脉宽映射角度；不同型号的有效脉宽范围可能不同，必须以舵机规格书为准。

### 实验 3：PWM 驱动直流电机

| 器件 | 对应引脚 | 说明 |
|---|---:|---|
| 电机 PWM/使能 | PA2 / TIM2_CH3 | 输出 PWM，控制平均驱动功率 |
| 方向控制 1 | PA4 | 普通推挽输出 |
| 方向控制 2 | PA5 | 普通推挽输出 |
| 电机驱动模块 | 外接 | MCU 不应直接驱动直流电机 |
| 电机电源 | 按电机与驱动模块要求 | 与 STM32 控制地共地 |
| 按键模块 | 复用前文 Key 模块 | 用于切换速度 |
| OLED | 复用前文 OLED 模块 | 显示当前速度，可选 |

> 直流电机会产生较大的启动电流和反向电动势。实际连接时必须通过电机驱动器或功率级驱动，并做好共地、续流和电源去耦。

---

## 工程文件结构

建议将三个实验分别建立工程，避免不同 PWM 周期和通道配置互相干扰：

```text
14-PWM-LED-Servo-Motor
├── 01-LED-Breathing
│   ├── Code
│   │   ├── main.c
│   │   ├── PWM.c
│   │   └── PWM.h
│   └── README.md
├── 02-Servo
│   ├── Code
│   │   ├── main.c
│   │   ├── PWM.c
│   │   ├── PWM.h
│   │   ├── Servo.c
│   │   └── Servo.h
│   └── README.md
├── 03-DC-Motor
│   ├── Code
│   │   ├── main.c
│   │   ├── PWM.c
│   │   ├── PWM.h
│   │   ├── Motor.c
│   │   └── Motor.h
│   └── README.md
├── Hardware
└── Results
```

---

## 核心代码

> 下面代码为按本节主题整理的标准外设库参考实现。为便于复习，所有非空代码行均添加了中文行内说明。

### 1. LED 呼吸灯 — PWM.h

```c
#ifndef __PWM_H                                      // 判断 __PWM_H 是否尚未定义，防止头文件被重复包含
#define __PWM_H                                      // 定义 __PWM_H 宏，与 #ifndef 配合形成头文件保护

#include "stm32f10x.h"                               // 引入 STM32F10x 标准外设库定义，以便使用 uint16_t 等类型和外设声明

void PWM_Init(void);                                 // 声明 PWM 初始化函数，用于配置 TIM2_CH1 在 PA0 输出 PWM
void PWM_SetCompare1(uint16_t Compare);              // 声明 CCR1 设置函数，通过修改比较值改变 TIM2_CH1 的 PWM 占空比

#endif                                               // 结束头文件保护条件编译
```

### 2. LED 呼吸灯 — PWM.c

```c
#include "stm32f10x.h"                               // 引入 STM32F10x 标准外设库头文件，获得 RCC、GPIO 和 TIM 的库函数接口
#include "PWM.h"                                     // 引入本模块头文件，保证函数声明与实现保持一致

void PWM_Init(void)                                  // 定义 PWM 初始化函数，为 LED 呼吸灯配置 TIM2 通道 1
{                                                    // PWM_Init 函数体开始
    RCC_APB1PeriphClockCmd(RCC_APB1Periph_TIM2, ENABLE); // 开启 TIM2 的 APB1 外设时钟，否则定时器寄存器无法正常工作
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA, ENABLE); // 开启 GPIOA 的 APB2 外设时钟，为 PA0 复用输出做准备

    GPIO_InitTypeDef GPIO_InitStructure;             // 定义 GPIO 初始化结构体，用于保存 PA0 的模式、引脚和速度参数
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_AF_PP;  // 将 PA0 配置为复用推挽输出，把引脚控制权交给 TIM2_CH1
    GPIO_InitStructure.GPIO_Pin = GPIO_Pin_0;        // 选择 PA0，因为 TIM2 通道 1 默认复用输出引脚为 PA0
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz; // 设置 GPIO 最大输出速度为 50MHz，满足 PWM 数字波形输出需求
    GPIO_Init(GPIOA, &GPIO_InitStructure);           // 按上述参数完成 GPIOA 的 PA0 初始化

    TIM_InternalClockConfig(TIM2);                   // 选择 TIM2 内部时钟作为计数时钟源，本实验不使用外部时钟输入

    TIM_TimeBaseInitTypeDef TIM_TimeBaseInitStructure; // 定义定时器时基初始化结构体，用于配置 PSC、ARR 和计数模式
    TIM_TimeBaseInitStructure.TIM_ClockDivision = TIM_CKD_DIV1; // 设置时钟分频因子为 1，此参数主要影响数字滤波采样时钟
    TIM_TimeBaseInitStructure.TIM_CounterMode = TIM_CounterMode_Up; // 设置 TIM2 为向上计数模式，从 0 递增到 ARR
    TIM_TimeBaseInitStructure.TIM_Period = 100 - 1;  // 设置 ARR=99，使一个 PWM 周期包含 100 个计数单位，便于百分比映射
    TIM_TimeBaseInitStructure.TIM_Prescaler = 720 - 1; // 设置 PSC=719，72MHz 定时器时钟预分频后得到 100kHz 计数频率
    TIM_TimeBaseInitStructure.TIM_RepetitionCounter = 0; // 通用定时器 TIM2 不使用重复计数器，此成员保持为 0
    TIM_TimeBaseInit(TIM2, &TIM_TimeBaseInitStructure); // 将时基参数写入 TIM2，使 PWM 频率约为 100kHz/100=1kHz

    TIM_OCInitTypeDef TIM_OCInitStructure;           // 定义输出比较初始化结构体，用于配置 PWM 模式和输出极性
    TIM_OCStructInit(&TIM_OCInitStructure);          // 先使用库函数写入默认值，避免结构体未赋值成员产生不确定行为
    TIM_OCInitStructure.TIM_OCMode = TIM_OCMode_PWM1; // 选择 PWM 模式 1，向上计数时 CNT<CCR 为有效电平区间
    TIM_OCInitStructure.TIM_OCPolarity = TIM_OCPolarity_High; // 设置有效电平为高电平，使 CCR 增大时高电平时间增加
    TIM_OCInitStructure.TIM_OutputState = TIM_OutputState_Enable; // 使能输出比较通道，将内部 OC1REF 波形输出到 CH1 引脚
    TIM_OCInitStructure.TIM_Pulse = 0;               // 初始 CCR1 设置为 0，使 PWM 初始占空比为 0%
    TIM_OC1Init(TIM2, &TIM_OCInitStructure);         // 按上述参数初始化 TIM2 输出比较通道 1

    TIM_Cmd(TIM2, ENABLE);                           // 启动 TIM2 计数器，从此由硬件持续产生 PWM 波形
}                                                    // PWM_Init 函数体结束

void PWM_SetCompare1(uint16_t Compare)               // 定义 TIM2_CH1 比较值设置函数，参数 Compare 对应 CCR1
{                                                    // PWM_SetCompare1 函数体开始
    TIM_SetCompare1(TIM2, Compare);                  // 修改 TIM2 的 CCR1，在 ARR=99 时 Compare=0~100 近似对应 0%~100% 占空比
}                                                    // PWM_SetCompare1 函数体结束
```

### 3. LED 呼吸灯 — main.c

```c
#include "stm32f10x.h"                               // 引入 STM32F10x 标准外设库头文件，提供基础类型和芯片定义
#include "Delay.h"                                   // 引入延时模块，用于控制亮度变化的速度
#include "PWM.h"                                     // 引入 PWM 模块，用于初始化 PWM 并动态修改占空比

uint8_t i;                                           // 定义循环变量 i，用于生成 0~100 的亮度渐变序列

int main(void)                                       // 主函数入口，系统复位后最终进入此函数执行用户程序
{                                                    // main 函数体开始
    PWM_Init();                                      // 初始化 TIM2_CH1 PWM，使 PA0 输出约 1kHz 的 PWM 波形

    while (1)                                        // 进入无限循环，使 LED 持续执行渐亮和渐暗的呼吸效果
    {                                                // while 循环体开始
        for (i = 0; i <= 100; i++)                   // 让 i 从 0 递增到 100，对应占空比逐步增大
        {                                            // 第一个 for 循环体开始
            PWM_SetCompare1(i);                      // 将 CCR1 设置为当前 i，使 LED 平均功率随占空比增加而逐渐变化
            Delay_ms(10);                            // 每次调整后延时 10ms，让亮度变化过程能够被人眼观察到
        }                                            // 第一个 for 循环体结束

        for (i = 0; i <= 100; i++)                   // 再次让 i 从 0 递增到 100，用 100-i 生成反向变化序列
        {                                            // 第二个 for 循环体开始
            PWM_SetCompare1(100 - i);                // 将 CCR1 从 100 逐步减到 0，使 LED 平均功率逐渐降低
            Delay_ms(10);                            // 每次降低占空比后延时 10ms，形成平滑的渐暗视觉效果
        }                                            // 第二个 for 循环体结束
    }                                                // while 循环体结束，随后自动返回循环起点继续呼吸
}                                                    // main 函数体结束，嵌入式程序正常情况下不会执行到函数返回
```

### 4. 舵机 — PWM.h

```c
#ifndef __PWM_H                                      // 判断 PWM 头文件是否已被包含，避免重复定义
#define __PWM_H                                      // 定义 PWM 头文件保护宏

#include "stm32f10x.h"                               // 引入标准外设库类型定义，供 uint16_t 和 TIM 接口使用

void PWM_Init(void);                                 // 声明舵机专用 PWM 初始化函数，配置 TIM2_CH2 输出 50Hz PWM
void PWM_SetCompare2(uint16_t Compare);              // 声明 TIM2_CH2 比较值设置函数，用 CCR2 直接控制高电平脉宽

#endif                                               // 结束 PWM 头文件保护
```

### 5. 舵机 — PWM.c

```c
#include "stm32f10x.h"                               // 引入 STM32F10x 标准外设库，使用 RCC、GPIO、TIM 等接口
#include "PWM.h"                                     // 引入 PWM 模块函数声明，确保接口一致

void PWM_Init(void)                                  // 定义舵机 PWM 初始化函数，为 TIM2_CH2 配置 20ms 周期
{                                                    // PWM_Init 函数体开始
    RCC_APB1PeriphClockCmd(RCC_APB1Periph_TIM2, ENABLE); // 开启 TIM2 时钟，使定时器能够计数并产生输出比较波形
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA, ENABLE); // 开启 GPIOA 时钟，使 PA1 能够配置为复用输出

    GPIO_InitTypeDef GPIO_InitStructure;             // 定义 GPIO 初始化结构体，用于配置舵机信号输出引脚
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_AF_PP;  // 将 PA1 设置为复用推挽输出，由 TIM2_CH2 硬件驱动
    GPIO_InitStructure.GPIO_Pin = GPIO_Pin_1;        // 选择 PA1，对应 TIM2 默认映射的通道 2 输出
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz; // 设置 GPIO 输出速度上限为 50MHz，满足舵机脉冲输出要求
    GPIO_Init(GPIOA, &GPIO_InitStructure);           // 应用 GPIO 配置，使 PA1 进入 TIM2_CH2 复用输出状态

    TIM_InternalClockConfig(TIM2);                   // 使用 TIM2 内部时钟作为计数源，保证 PWM 周期由定时器时基决定

    TIM_TimeBaseInitTypeDef TIM_TimeBaseInitStructure; // 定义 TIM2 时基结构体，用于建立 1us 计数单位和 20ms 周期
    TIM_TimeBaseInitStructure.TIM_ClockDivision = TIM_CKD_DIV1; // 设置时钟分频参数为 1，不额外改变定时器数字滤波时钟
    TIM_TimeBaseInitStructure.TIM_CounterMode = TIM_CounterMode_Up; // 选择向上计数模式，使 CNT 从 0 计数到 ARR
    TIM_TimeBaseInitStructure.TIM_Period = 20000 - 1; // 设置 ARR=19999，在 1MHz 计数频率下得到 20000us 即 20ms 周期
    TIM_TimeBaseInitStructure.TIM_Prescaler = 72 - 1; // 设置 PSC=71，将 72MHz 定时器时钟分频为 1MHz，即每计数一次为 1us
    TIM_TimeBaseInitStructure.TIM_RepetitionCounter = 0; // TIM2 为通用定时器，不使用重复计数功能，因此保持为 0
    TIM_TimeBaseInit(TIM2, &TIM_TimeBaseInitStructure); // 写入时基配置，使 PWM 频率为 1MHz/20000=50Hz

    TIM_OCInitTypeDef TIM_OCInitStructure;           // 定义输出比较结构体，用于配置舵机 PWM 的模式、极性和初始脉宽
    TIM_OCStructInit(&TIM_OCInitStructure);          // 先填充标准默认值，防止未初始化成员影响输出比较行为
    TIM_OCInitStructure.TIM_OCMode = TIM_OCMode_PWM1; // 使用 PWM 模式 1，使高电平持续时间主要由 CCR2 决定
    TIM_OCInitStructure.TIM_OCPolarity = TIM_OCPolarity_High; // 设置有效输出为高电平，形成常见舵机控制正脉冲
    TIM_OCInitStructure.TIM_OutputState = TIM_OutputState_Enable; // 使能 CH2 输出，将 PWM 波形真正送到 PA1
    TIM_OCInitStructure.TIM_Pulse = 500;             // 初始 CCR2=500，对应约 500us 高电平脉宽，作为参考起始位置
    TIM_OC2Init(TIM2, &TIM_OCInitStructure);         // 初始化 TIM2 通道 2 输出比较单元

    TIM_Cmd(TIM2, ENABLE);                           // 启动 TIM2，使 PA1 开始持续输出 50Hz 舵机控制脉冲
}                                                    // PWM_Init 函数体结束

void PWM_SetCompare2(uint16_t Compare)               // 定义 CCR2 设置函数，Compare 的数值在本配置下等价于高电平微秒数
{                                                    // PWM_SetCompare2 函数体开始
    TIM_SetCompare2(TIM2, Compare);                  // 更新 TIM2_CCR2，例如 500、1500、2500 分别对应约 0.5ms、1.5ms、2.5ms
}                                                    // PWM_SetCompare2 函数体结束
```

### 6. 舵机 — Servo.h

```c
#ifndef __SERVO_H                                    // 判断舵机头文件是否已定义，避免多次包含造成重复声明
#define __SERVO_H                                    // 定义舵机头文件保护宏

void Servo_Init(void);                               // 声明舵机初始化函数，内部调用底层 PWM 初始化
void Servo_SetAngle(float Angle);                    // 声明舵机角度设置函数，将 0~180° 映射为目标脉宽

#endif                                               // 结束舵机头文件保护
```

### 7. 舵机 — Servo.c

```c
#include "stm32f10x.h"                               // 引入 STM32F10x 基础定义，保持模块与标准库工程环境一致
#include "PWM.h"                                     // 引入 PWM 底层驱动接口，用于修改 TIM2_CH2 的 CCR2
#include "Servo.h"                                   // 引入本模块头文件，保证 Servo 函数声明与实现一致

void Servo_Init(void)                                // 定义舵机初始化函数，对上层屏蔽 TIM2 的具体配置细节
{                                                    // Servo_Init 函数体开始
    PWM_Init();                                      // 初始化 50Hz PWM，使舵机信号引脚开始输出周期控制脉冲
}                                                    // Servo_Init 函数体结束

void Servo_SetAngle(float Angle)                     // 定义角度设置函数，输入期望角度 Angle，典型范围为 0~180°
{                                                    // Servo_SetAngle 函数体开始
    PWM_SetCompare2((uint16_t)(Angle / 180.0f * 2000.0f + 500.0f)); // 将 0~180° 线性映射到约 500~2500us 脉宽并写入 CCR2
}                                                    // Servo_SetAngle 函数体结束
```

### 8. 舵机 — main.c

```c
#include "stm32f10x.h"                               // 引入 STM32F10x 标准外设库基础定义
#include "OLED.h"                                    // 引入 OLED 显示模块，用于显示当前舵机角度
#include "Key.h"                                     // 引入按键模块，用于检测用户按键操作
#include "Servo.h"                                   // 引入舵机模块，用于初始化舵机并设置目标角度

uint8_t KeyNum;                                      // 保存按键模块返回的键码，0 通常表示当前没有新的按键事件
float Angle;                                         // 保存当前目标角度，使用浮点类型便于进行角度到脉宽的线性换算

int main(void)                                       // 主函数入口，完成模块初始化并循环响应按键
{                                                    // main 函数体开始
    OLED_Init();                                     // 初始化 OLED，准备显示角度信息
    Servo_Init();                                    // 初始化舵机底层 50Hz PWM 输出
    Key_Init();                                      // 初始化按键输入模块，供主循环读取按键事件
    OLED_ShowString(1, 1, "Angle:");                 // 在 OLED 第一行显示 Angle 标签，提示后方数字为当前角度

    while (1)                                        // 进入主循环，持续扫描按键并更新舵机位置
    {                                                // while 循环体开始
        KeyNum = Key_GetNum();                       // 获取一次按键事件，具体键码规则由前文 Key 模块定义
        if (KeyNum == 1)                             // 判断是否按下用于增加角度的按键
        {                                            // if 分支开始
            Angle += 30;                             // 每触发一次按键，将目标角度增加 30°
            if (Angle > 180)                         // 判断角度是否超过示例程序设定的 180° 上限
            {                                        // 角度越界处理分支开始
                Angle = 0;                           // 超过 180° 后回到 0°，形成 0、30、60…180 的循环切换
            }                                        // 角度越界处理分支结束
        }                                            // 按键判断分支结束
        Servo_SetAngle(Angle);                       // 将当前角度换算成 PWM 脉宽并更新 CCR2，驱动舵机转到目标位置
        OLED_ShowNum(1, 7, (uint32_t)Angle, 3);      // 在 OLED 上以三位数字显示当前目标角度，方便观察控制结果
    }                                                // while 循环体结束，继续下一次按键扫描
}                                                    // main 函数体结束
```

### 9. 直流电机 — PWM.h

```c
#ifndef __PWM_H                                      // 判断 PWM 头文件是否尚未包含，避免重复定义
#define __PWM_H                                      // 定义 PWM 头文件保护宏

#include "stm32f10x.h"                               // 引入 STM32F10x 标准库类型和外设声明

void PWM_Init(void);                                 // 声明电机专用 PWM 初始化函数，配置 TIM2_CH3 在 PA2 输出 PWM
void PWM_SetCompare3(uint16_t Compare);              // 声明 CCR3 设置函数，用 0~100 比较值控制电机 PWM 占空比

#endif                                               // 结束 PWM 头文件保护
```

### 10. 直流电机 — PWM.c

```c
#include "stm32f10x.h"                               // 引入标准外设库，使用 RCC、GPIO、TIM 等接口
#include "PWM.h"                                     // 引入本模块函数声明，保证编译器能够检查接口一致性

void PWM_Init(void)                                  // 定义直流电机 PWM 初始化函数，使用 TIM2 通道 3
{                                                    // PWM_Init 函数体开始
    RCC_APB1PeriphClockCmd(RCC_APB1Periph_TIM2, ENABLE); // 开启 TIM2 时钟，为硬件 PWM 计数与比较提供时钟
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA, ENABLE); // 开启 GPIOA 时钟，使 PA2 能配置成 TIM2_CH3 复用输出

    GPIO_InitTypeDef GPIO_InitStructure;             // 定义 GPIO 初始化结构体，用于配置电机 PWM 输出脚
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_AF_PP;  // 将 PA2 设置为复用推挽输出，由 TIM2_CH3 驱动 PWM 波形
    GPIO_InitStructure.GPIO_Pin = GPIO_Pin_2;        // 选择 PA2，因为 TIM2 通道 3 默认输出映射到 PA2
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz; // 设置 GPIO 输出速度上限为 50MHz，满足较高频 PWM 输出
    GPIO_Init(GPIOA, &GPIO_InitStructure);           // 应用 PA2 的复用推挽输出配置

    TIM_InternalClockConfig(TIM2);                   // 选择 TIM2 内部时钟作为计数源，PWM 频率由 PSC 和 ARR 决定

    TIM_TimeBaseInitTypeDef TIM_TimeBaseInitStructure; // 定义 TIM2 时基初始化结构体
    TIM_TimeBaseInitStructure.TIM_ClockDivision = TIM_CKD_DIV1; // 设置时钟分频参数为 1，不额外修改数字滤波时钟
    TIM_TimeBaseInitStructure.TIM_CounterMode = TIM_CounterMode_Up; // 设置向上计数模式，CNT 从 0 计数到 ARR
    TIM_TimeBaseInitStructure.TIM_Period = 100 - 1;  // 设置 ARR=99，让 CCR3=0~100 可以直观表示约 0%~100% 占空比
    TIM_TimeBaseInitStructure.TIM_Prescaler = 72 - 1; // 设置 PSC=71，将 72MHz 定时器时钟分频为 1MHz
    TIM_TimeBaseInitStructure.TIM_RepetitionCounter = 0; // TIM2 为通用定时器，不使用重复计数器功能
    TIM_TimeBaseInit(TIM2, &TIM_TimeBaseInitStructure); // 写入时基参数，得到约 1MHz/100=10kHz 的 PWM 频率

    TIM_OCInitTypeDef TIM_OCInitStructure;           // 定义输出比较初始化结构体，用于配置 TIM2_CH3 PWM
    TIM_OCStructInit(&TIM_OCInitStructure);          // 使用库函数填充默认值，避免未初始化成员导致异常
    TIM_OCInitStructure.TIM_OCMode = TIM_OCMode_PWM1; // 选择 PWM 模式 1，通过 CNT 与 CCR3 比较生成占空比可调波形
    TIM_OCInitStructure.TIM_OCPolarity = TIM_OCPolarity_High; // 设置 PWM 有效电平为高电平
    TIM_OCInitStructure.TIM_OutputState = TIM_OutputState_Enable; // 使能输出比较通道，使内部波形输出到 PA2
    TIM_OCInitStructure.TIM_Pulse = 0;               // 初始 CCR3=0，使电机上电后默认 PWM 占空比为 0，降低误启动风险
    TIM_OC3Init(TIM2, &TIM_OCInitStructure);         // 初始化 TIM2 通道 3 输出比较单元

    TIM_Cmd(TIM2, ENABLE);                           // 启动 TIM2，使 PA2 开始输出硬件 PWM
}                                                    // PWM_Init 函数体结束

void PWM_SetCompare3(uint16_t Compare)               // 定义 TIM2_CH3 比较值设置函数
{                                                    // PWM_SetCompare3 函数体开始
    TIM_SetCompare3(TIM2, Compare);                  // 修改 CCR3，在 ARR=99 的配置下用 0~100 控制约 0%~100% 占空比
}                                                    // PWM_SetCompare3 函数体结束
```

### 11. 直流电机 — Motor.h

```c
#ifndef __MOTOR_H                                    // 判断电机头文件是否尚未定义，防止重复包含
#define __MOTOR_H                                    // 定义电机头文件保护宏

#include "stm32f10x.h"                               // 引入标准库基础类型定义，以便使用 int8_t 等整数类型

void Motor_Init(void);                               // 声明电机初始化函数，负责方向 GPIO 和底层 PWM 的初始化
void Motor_SetSpeed(int8_t Speed);                   // 声明电机速度设置函数，示例范围为 -100~100，符号表示方向

#endif                                               // 结束电机头文件保护
```

### 12. 直流电机 — Motor.c

```c
#include "stm32f10x.h"                               // 引入 STM32F10x 标准外设库，使用 GPIO 控制方向
#include "PWM.h"                                     // 引入 PWM 驱动，用于设置 TIM2_CH3 的电机调速占空比
#include "Motor.h"                                   // 引入本模块头文件，保证电机接口声明与实现一致

void Motor_Init(void)                                // 定义电机初始化函数，配置两个方向引脚和一个 PWM 输出
{                                                    // Motor_Init 函数体开始
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA, ENABLE); // 开启 GPIOA 时钟，为 PA4 和 PA5 方向控制输出提供时钟

    GPIO_InitTypeDef GPIO_InitStructure;             // 定义 GPIO 初始化结构体，用于配置电机驱动器方向控制脚
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_Out_PP; // 将方向引脚配置为普通推挽输出，可主动输出稳定高低电平
    GPIO_InitStructure.GPIO_Pin = GPIO_Pin_4 | GPIO_Pin_5; // 同时选择 PA4 和 PA5，作为电机驱动模块的两个方向控制信号
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz; // 设置 GPIO 输出速度上限为 50MHz，满足普通数字控制需求
    GPIO_Init(GPIOA, &GPIO_InitStructure);           // 应用 GPIO 配置，使 PA4、PA5 可由软件控制方向

    GPIO_ResetBits(GPIOA, GPIO_Pin_4 | GPIO_Pin_5);  // 初始化时先将两个方向信号拉低，尽量避免电机在启动阶段意外转动
    PWM_Init();                                      // 初始化 TIM2_CH3 PWM，使 PA2 可作为电机速度控制信号输出
}                                                    // Motor_Init 函数体结束

void Motor_SetSpeed(int8_t Speed)                    // 定义速度设置函数，正负号控制方向，绝对值控制 PWM 占空比
{                                                    // Motor_SetSpeed 函数体开始
    if (Speed >= 0)                                  // 判断速度是否为非负数，非负数按示例定义为正转方向
    {                                                // 正转分支开始
        GPIO_SetBits(GPIOA, GPIO_Pin_4);             // 将 PA4 输出高电平，建立正转方向控制组合
        GPIO_ResetBits(GPIOA, GPIO_Pin_5);           // 将 PA5 输出低电平，与 PA4 组合后令驱动模块按正方向驱动
        PWM_SetCompare3((uint16_t)Speed);            // 将 0~100 的速度绝对值写入 CCR3，改变正转时的 PWM 占空比
    }                                                // 正转分支结束
    else                                             // 当 Speed 为负数时进入反转控制分支
    {                                                // 反转分支开始
        GPIO_ResetBits(GPIOA, GPIO_Pin_4);           // 将 PA4 输出低电平，准备建立与正转相反的方向组合
        GPIO_SetBits(GPIOA, GPIO_Pin_5);             // 将 PA5 输出高电平，使电机驱动模块切换为反转方向
        PWM_SetCompare3((uint16_t)(-Speed));         // 对负速度取绝对值后写入 CCR3，使 PWM 比较值保持为非负数
    }                                                // 反转分支结束
}                                                    // Motor_SetSpeed 函数体结束
```

### 13. 直流电机 — main.c

```c
#include "stm32f10x.h"                               // 引入 STM32F10x 标准外设库基础定义
#include "OLED.h"                                    // 引入 OLED 模块，用于显示当前电机速度
#include "Key.h"                                     // 引入按键模块，用于切换速度档位
#include "Motor.h"                                   // 引入电机模块，用于初始化电机并设置转速方向

uint8_t KeyNum;                                      // 保存当前获取到的按键键码
int8_t Speed;                                        // 保存目标速度，范围示例为 -100~100，符号同时表示旋转方向

int main(void)                                       // 主函数入口，完成显示、按键和电机模块初始化
{                                                    // main 函数体开始
    OLED_Init();                                     // 初始化 OLED 显示模块
    Motor_Init();                                    // 初始化方向控制 GPIO 与 TIM2_CH3 PWM，默认速度为 0
    Key_Init();                                      // 初始化按键模块，用于主循环获取速度切换命令
    OLED_ShowString(1, 1, "Speed:");                 // 在 OLED 第一行显示 Speed 标签

    while (1)                                        // 进入无限循环，持续读取按键并刷新电机速度
    {                                                // while 循环体开始
        KeyNum = Key_GetNum();                       // 获取按键事件，具体键码由 Key 模块实现决定
        if (KeyNum == 1)                             // 判断是否触发速度切换按键
        {                                            // 按键处理分支开始
            Speed += 20;                             // 每次按键将目标速度增加 20，逐档提高正向占空比
            if (Speed > 100)                         // 判断速度是否超过示例允许的最大正向值 100
            {                                        // 速度越界处理分支开始
                Speed = -100;                        // 超过 +100 后跳转到 -100，从最大反转速度重新循环
            }                                        // 速度越界处理分支结束
        }                                            // 按键处理分支结束
        Motor_SetSpeed(Speed);                       // 根据 Speed 的符号设置方向，并根据绝对值设置 PWM 占空比
        OLED_ShowSignedNum(1, 7, Speed, 3);          // 在 OLED 上显示带符号的三位速度值，便于观察当前控制档位
    }                                                // while 循环体结束，继续下一轮按键扫描
}                                                    // main 函数体结束
```

---

## 代码要点

| 行/段 | 说明 |
|---|---|
| `GPIO_Mode_AF_PP` | PWM 输出引脚必须配置为复用推挽输出，才能把 GPIO 控制权交给 TIM 输出比较通道。 |
| `TIM_OCMode_PWM1` | 使用 PWM 模式 1。向上计数时，输出参考信号通常根据 `CNT` 与 `CCR` 的比较关系切换。 |
| `TIM_OCxInit()` | `x` 必须与实际使用的定时器通道一致：CH1 用 `TIM_OC1Init`，CH2 用 `TIM_OC2Init`，CH3 用 `TIM_OC3Init`。 |
| `TIM_SetComparex()` | 本质是修改对应通道的 CCRx，不直接设置“百分比”；占空比还取决于 ARR。 |
| LED：`PSC=719, ARR=99` | 72MHz 时约得到 1kHz PWM，100 个计数单位便于用 0~100 表示亮度等级。 |
| 舵机：`PSC=71, ARR=19999` | 形成 1us 计数单位和 20ms 周期，CCR2 的数值可直接近似理解为高电平微秒数。 |
| 电机：`PSC=71, ARR=99` | 约产生 10kHz PWM，CCR3=0~100 便于直接映射速度百分比。 |
| `Servo_SetAngle()` | 将角度线性映射到约 500~2500us 控制脉宽；实际范围要根据舵机规格校准。 |
| `Motor_SetSpeed()` | 速度符号控制方向 GPIO，速度绝对值控制 PWM 占空比，实现方向与调速解耦。 |
| 初始化顺序 | 先开启 RCC 时钟，再初始化 GPIO 和 TIM，最后启动定时器；电机项目建议先设置安全方向状态和 0% 占空比。 |

---

## 关键知识点

### 1. PWM 的本质

#### 原理

PWM（Pulse Width Modulation，脉冲宽度调制）是在固定或基本固定的周期内，通过改变有效电平持续时间来改变占空比。

在 STM32 定时器中，可以把一个 PWM 周期理解为：

```text
CNT：0 ───────────────────────→ ARR
     ├──── 有效区间 ────┤
     0                  CCR
```

以向上计数、PWM 模式 1、高电平有效为例，计数器 `CNT` 在每个周期内不断递增，硬件比较 `CNT` 与 `CCR`，从而决定输出有效或无效电平。CPU 不需要逐个翻转 GPIO。

常用近似关系：

```text
PWM频率 = 定时器计数时钟 / (PSC + 1) / (ARR + 1)
占空比  ≈ CCR / (ARR + 1)
分辨率  ≈ 1 / (ARR + 1)
```

#### 特点

- 由定时器硬件持续输出，稳定性和效率通常优于软件延时翻转 GPIO。
- 修改 CCR 可以动态改变占空比，而不必停止定时器。
- 修改 PSC 或 ARR 可以改变频率，但也会同时影响分辨率和 CCR 的含义。
- 同一个定时器的多个通道共享同一套时基，因此通常共享 PSC 和 ARR。

#### 面试易问

**Q：PWM 如何实现“模拟量”控制？**

A：PWM 本身仍然是数字高低电平，只是通过改变一个周期内高、低电平所占时间比例，改变负载获得的平均能量。经过负载惯性或低通滤波后，可以表现出类似连续变化的效果。

**Q：为什么修改 CCR 可以改变占空比？**

A：因为输出比较单元使用 CCR 作为比较阈值。PSC 和 ARR 不变时，一个周期的总计数长度固定，CCR 改变就会改变有效电平持续的计数时间，因此占空比发生变化。

#### 易错点

- 把 `CCR=50` 直接理解为 50%，却忽略 ARR 可能不是 99。
- 为了修改占空比错误地频繁重配整个定时器，实际上通常只需更新 CCR。
- 忘记 GPIO 必须配置为复用输出，导致定时器内部有波形但引脚没有输出。
- 同一个 TIM 的多个通道需要不同 PWM 频率时，不能简单只改某个通道的 CCR 解决。

---

### 2. PWM 频率、占空比与分辨率

#### 原理

PWM 参数主要由三个寄存器决定：

- `PSC`：预分频器，决定计数器输入时钟。
- `ARR`：自动重装寄存器，决定一个计数周期的长度。
- `CCR`：捕获/比较寄存器，在 PWM 模式下决定比较点。

例如 LED 项目中：

```text
TIM2 时钟约 72MHz
PSC = 720 - 1
ARR = 100 - 1

计数频率 = 72MHz / 720 = 100kHz
PWM频率  = 100kHz / 100 = 1kHz
```

#### 特点

- PSC 越大，计数越慢。
- ARR 越大，PWM 分辨率通常越高，但在相同计数时钟下 PWM 频率越低。
- CCR 的有效范围应与 ARR 匹配。
- 设计 PWM 时常需要在“频率”和“分辨率”之间折中。

#### 面试易问

**Q：想提高 PWM 分辨率应该怎么做？**

A：在计数时钟允许的情况下增大 ARR，使一个周期包含更多计数级别。但如果 PSC 不变，ARR 增大会降低 PWM 频率，因此需要综合考虑。

**Q：同一个 TIM2 的 CH1 和 CH2 能否一个输出 1kHz、另一个输出 50Hz？**

A：在普通 PWM 配置下不能独立设置周期，因为同一计数器的各通道共享 PSC 和 ARR；它们可以拥有不同 CCR，也就是不同占空比，但周期相同。要输出不同频率，通常需要使用不同定时器或其他特殊模式。

#### 易错点

- 只修改 ARR 却忘记同步检查 CCR 是否超出新的周期范围。
- 把 PSC 写成目标分频数而忘记寄存器实际配置通常是“分频数减 1”。
- 计算时忽略 APB 分频对定时器时钟的影响。
- 追求极高频率导致 ARR 太小，占空比分辨率严重不足。

---

### 3. PWM 模式 1 与输出极性

#### 原理

PWM 模式决定内部参考信号 `OCxREF` 如何由 `CNT` 和 `CCR` 比较结果产生；输出极性决定该参考信号最终以高电平有效还是低电平有效出现在引脚。

因此，“PWM 模式”和“输出极性”是两个不同概念。

#### 特点

- `TIM_OCMode_PWM1` 是常见的 PWM 生成模式。
- `TIM_OCPolarity_High` 表示有效状态对应高电平输出。
- 改变极性可以反转引脚上的有效/无效电平关系。
- 对低电平点亮 LED 等负逻辑负载，输出极性会影响“占空比越大是越亮还是越暗”。

#### 面试易问

**Q：PWM1 和 PWM2 模式有什么区别？**

A：二者使用相反的比较逻辑生成参考输出。在相同计数方向、CCR 和输出极性下，PWM1 与 PWM2 的有效区间互补。实际还需结合计数方向和输出极性一起判断最终波形。

#### 易错点

- LED 越调占空比反而越暗，就误认为 PWM 配置失败，实际可能是 LED 接法或极性相反。
- 只看 `TIM_OCPolarity` 判断波形，而忽略 PWM1/PWM2 的内部比较逻辑。
- 通道编号配置错误，例如 PA2 是 TIM2_CH3 却调用 `TIM_OC1Init()`。

---

### 4. LED 呼吸灯

#### 原理

人眼不能直接看到足够高频率的 PWM 闪烁，而会感受到 LED 的平均亮度。

程序不断改变 CCR：

```text
CCR：0 → 1 → 2 → ... → 100
亮度：暗 → 逐渐变亮

CCR：100 → 99 → ... → 0
亮度：亮 → 逐渐变暗
```

于是形成“呼吸”效果。

#### 特点

- PWM 载波频率由定时器硬件产生。
- 主循环只负责较慢地修改 CCR。
- 亮度变化速度由 CCR 步进大小与步进间隔共同决定。
- 人眼感知亮度并非严格线性，因此线性改变 CCR 不一定产生完全线性的视觉亮度变化。

#### 面试易问

**Q：为什么不用软件延时直接快速开关 LED？**

A：软件翻转 GPIO 会占用 CPU，而且周期容易受其他代码影响；定时器 PWM 由硬件自动产生，CPU 只需偶尔修改 CCR，效率和稳定性更好。

**Q：为什么 PWM 频率很高时 LED 看起来没有闪烁？**

A：因为视觉系统会对快速变化进行时间平均，超过一定频率后人眼主要感受到平均亮度，而不是单独的开关脉冲。

#### 易错点

- PWM 频率过低导致肉眼可见闪烁。
- 步进延时过大，呼吸过程出现明显台阶。
- 使用 `uint8_t i` 编写 `for(i = 100; i >= 0; i--)`，由于无符号变量不会小于 0，可能导致死循环。
- 低电平点亮 LED 时没有考虑逻辑反相。

---

### 5. 舵机 PWM 控制

#### 原理

常见位置舵机不是通过“占空比百分比”直接表达角度，而是通过固定重复周期中的高电平脉宽表达目标位置。

示例映射：

```text
0°   → 约 0.5ms
90°  → 约 1.5ms
180° → 约 2.5ms
```

当计数单位为 1us 时，可直接让：

```text
CCR = 高电平时间（us）
```

因此示例函数：

```text
CCR = Angle / 180 × 2000 + 500
```

#### 特点

- 常见控制帧周期约为 20ms，即约 50Hz。
- 关键量是高电平脉宽，而不是简单的占空比百分比。
- 不同舵机的机械极限与有效脉宽范围可能不同。
- 舵机内部通常自带位置闭环，MCU 发送的是目标位置命令。

#### 面试易问

**Q：舵机控制为什么也叫 PWM，但与电机调速含义不同？**

A：二者都使用脉宽变化的周期信号，但舵机通常把脉宽编码为位置命令；直流电机 PWM 调速主要利用占空比改变平均驱动功率。控制语义不同。

**Q：为什么舵机可能抖动？**

A：可能来自电源能力不足、地线不可靠、控制脉冲不稳定、机械负载过大、脉宽超出有效范围或信号线受到干扰。

#### 易错点

- 用 0%~100% 的普通占空比思路直接控制舵机，导致脉宽严重错误。
- 舵机与 STM32 没有共地，导致控制信号没有可靠参考。
- 直接用开发板 3.3V GPIO 给舵机供电。
- 盲目使用 0.5ms～2.5ms 全范围，没有根据具体舵机机械极限校准。

---

### 6. 直流电机 PWM 调速

#### 原理

直流电机具有机械惯性。当电机驱动器以较高频率对电机供电进行开关控制时，改变 PWM 占空比会改变电机端的平均电压和平均能量输入，从而影响转速和转矩表现。

本示例把控制拆成两部分：

```text
PA4 / PA5：决定方向
PA2 PWM：决定速度大小
```

#### 特点

- 方向控制与速度控制相互独立，软件结构清晰。
- `Speed` 的正负号可用于表达方向。
- `abs(Speed)` 可映射到 PWM 占空比。
- 实际转速与占空比不一定线性，还会受到负载、电源、电机参数和摩擦影响。

#### 面试易问

**Q：为什么 MCU 不能直接驱动直流电机？**

A：电机工作和启动电流通常远大于 GPIO 可承受电流，而且感性负载会产生反向电动势，因此需要专用驱动器、MOSFET 或 H 桥功率级。

**Q：PWM 占空比 50% 是否一定代表电机转速为额定转速的 50%？**

A：不一定。开环 PWM 主要控制平均驱动能量，实际转速还受到负载、供电、电机反电动势、驱动损耗等因素影响。需要精确转速时通常要加入编码器反馈和闭环控制。

#### 易错点

- 电机直接接 MCU GPIO，可能损坏芯片。
- 电机电源与逻辑电源共地关系处理错误。
- 正反转切换过快，没有先降速或刹停，造成较大冲击电流。
- PWM 频率选择不合适，可能出现可闻噪声、发热或驱动效率下降。

---

### 7. GPIO 复用推挽输出与定时器通道映射

#### 原理

TIM 输出比较单元产生的是片上外设信号。只有将对应 GPIO 配置为复用输出，才能把这个外设信号连接到芯片引脚。

本节默认映射示例：

| TIM2 通道 | 默认引脚 | 本节用途 |
|---|---:|---|
| CH1 | PA0 | LED 呼吸灯 |
| CH2 | PA1 | 舵机 |
| CH3 | PA2 | 直流电机 PWM |

#### 特点

- 定时器通道与 GPIO 不是任意搭配的。
- 必须查阅芯片数据手册或复用功能表确认默认映射与重映射。
- F1 系列部分外设支持通过 AFIO 重映射到其他引脚。
- 使用重映射时还要关注 JTAG/SWD 调试引脚冲突。

#### 面试易问

**Q：为什么 TIM2_CH1 不能随便从任意 GPIO 输出？**

A：芯片内部外设信号通过固定的复用网络连接到特定引脚。软件只能在芯片提供的默认映射或合法重映射方案中选择，不能任意指定引脚。

#### 易错点

- 定时器配置正确，但 GPIO 引脚选错，导致没有波形。
- 忘记使用 `GPIO_Mode_AF_PP`。
- 启用引脚重映射却没有开启 AFIO 时钟。
- 重映射到调试引脚后未处理 JTAG/SWD 占用问题。

---

### 8. 同一定时器多通道共享时基

#### 原理

TIM2 只有一套核心计数器、PSC 和 ARR，但有多个 CCR 通道。

因此：

```text
TIM2_CH1、CH2、CH3、CH4
共享：CNT、PSC、ARR
独立：CCR1、CCR2、CCR3、CCR4
```

#### 特点

- 各通道可以拥有不同占空比。
- 各通道通常共享同一个 PWM 周期。
- 同时控制多个同频 PWM 负载非常方便。
- 若不同负载需要完全不同的 PWM 频率，通常应分配到不同定时器。

#### 面试易问

**Q：为什么本笔记把 LED、舵机、电机写成三个独立工程？**

A：因为三个实验选择的 PWM 周期不同。如果同时都使用 TIM2，它们会争用同一套 PSC 和 ARR。独立工程能清楚展示每个实验；实际综合项目中应重新规划定时器资源。

#### 易错点

- 在同一个工程里先为舵机把 TIM2 配成 50Hz，又在电机模块里重新把 TIM2 配成 10kHz，导致前面的舵机输出被破坏。
- 误以为每个通道都有独立 PSC 和 ARR。
- 模块初始化函数重复初始化同一外设，造成配置互相覆盖。

---

## 本节核心记忆

```text
PWM = 定时器计数 + 输出比较
```

```text
频率主要看 PSC 和 ARR
占空比主要看 CCR 与 ARR 的比例
```

```text
LED 呼吸灯：
固定较高频 PWM + 缓慢改变 CCR
```

```text
舵机：
约 50Hz 周期 + 用高电平脉宽表达目标角度
```

```text
直流电机：
方向 GPIO 决定正反转 + PWM 占空比控制平均驱动功率
```

```text
同一个定时器的多个通道共享 PSC 和 ARR，通常不能各自拥有不同 PWM 频率
```

---

## 开发过程总结

### 问题 1：定时器已经启动，但 PA0/PA1/PA2 没有 PWM 波形

现象：

- 程序能够正常编译和下载
- `TIM_Cmd(TIM2, ENABLE)` 已执行
- 示波器或逻辑分析仪看不到期望波形

排查过程：

1. 检查 GPIOA 和 TIM2 时钟是否已经开启。
2. 检查引脚是否配置为 `GPIO_Mode_AF_PP`。
3. 检查通道和引脚映射是否一致，例如 TIM2_CH3 对应 PA2。
4. 检查是否调用了正确的 `TIM_OC1Init/2Init/3Init`。
5. 检查 `TIM_OutputState_Enable` 是否使能。
6. 检查 CCR 是否为 0 或超出合理范围。

解决方案：

- 按“RCC → GPIO → 时基 → 输出比较 → 启动 TIM”的顺序检查。
- 使用示波器先验证基础固定占空比波形，再加入上层业务逻辑。

---

### 问题 2：LED 呼吸灯亮度变化方向相反或变化不明显

现象：

- CCR 增大时 LED 反而变暗
- LED 只在极少数亮度级有明显变化

排查过程：

1. 检查 LED 是高电平点亮还是低电平点亮。
2. 检查 PWM 输出极性。
3. 检查 ARR 与 CCR 范围是否匹配。
4. 检查 PWM 频率是否过低。
5. 考虑人眼亮度感知的非线性。

解决方案：

- 必要时反转 CCR 变化方向或调整输出极性。
- 需要更自然的呼吸效果时，可使用非线性亮度查表代替简单线性步进。

---

### 问题 3：舵机不转、抖动或复位 STM32

现象：

- 舵机偶尔抖动
- 转动时 STM32 复位
- 舵机无法达到指定角度

排查过程：

1. 检查 PWM 是否约为 50Hz。
2. 检查脉宽是否在舵机允许范围内。
3. 检查舵机是否使用足够电流能力的独立电源。
4. 检查舵机电源地与 STM32 是否共地。
5. 检查机械结构是否卡死或负载过大。

解决方案：

- 先用示波器确认 20ms 周期与 0.5ms～2.5ms 参考脉宽。
- 使用独立稳压电源给舵机供电并做好共地。
- 根据实际舵机缩小脉宽范围，避免撞到机械极限。

---

### 问题 4：直流电机只能单向转或速度变化不明显

现象：

- 正转正常但反转失败
- 修改 Speed 后转速几乎不变
- 小占空比时电机完全不启动

排查过程：

1. 检查 PA4、PA5 是否真正连接到驱动模块方向输入。
2. 检查 PA2 PWM 是否连接到驱动模块 PWM/使能输入。
3. 测量 PWM 占空比是否随 Speed 改变。
4. 检查电机电源电压和电流能力。
5. 考虑电机存在静摩擦和启动占空比阈值。

解决方案：

- 分别测试方向 GPIO 和 PWM 信号，不要一次排查全部模块。
- 为电机设置合理的最小启动占空比。
- 使用驱动器而不是直接由 STM32 GPIO 驱动电机。

---

### 问题 5：把三个实验合在一个工程后互相干扰

现象：

- 初始化舵机后 LED PWM 频率改变
- 初始化电机后舵机失控
- 不同模块单独运行正常，组合后异常

排查过程：

1. 检查三个模块是否都重复初始化 TIM2。
2. 检查是否分别写入了不同 PSC 和 ARR。
3. 检查通道虽然不同，但是否共享同一 TIM2 时基。

解决方案：

- 按系统需求统一规划定时器资源。
- 同频输出可共享一个 TIM 的多个通道。
- 不同频率的 PWM 优先分配到不同定时器。
- 底层定时器只初始化一次，上层模块只修改各自 CCR。

---

## 结果展示

> 实验 1：烧录 LED 呼吸灯程序后，TIM2_CH1 持续输出 PWM，程序逐步修改 CCR1，LED 呈现周期性渐亮和渐暗效果。✅

> 实验 2：烧录舵机程序后，TIM2_CH2 输出约 50Hz 控制脉冲，按键改变目标角度时，CCR2 对应脉宽随之变化，舵机转到不同位置。✅

> 实验 3：烧录直流电机程序后，PA4/PA5 控制旋转方向，TIM2_CH3 PWM 控制速度大小，按键可在负速度、停止附近和正速度档位之间切换。✅

---

## 本节小结

本节把 TIM 输出比较进一步应用到了 PWM 控制中，并通过三个典型负载理解了“同样是 PWM，控制目标却不同”的区别。

LED 呼吸灯关注的是：

```text
占空比变化 → 平均亮度变化
```

舵机关注的是：

```text
固定约 20ms 周期 → 高电平脉宽表示目标角度
```

直流电机关注的是：

```text
方向控制信号 + PWM 平均功率 → 正反转与调速
```

最重要的底层关系仍然是：

```text
定时器时基决定 PWM 周期
输出比较 CCR 决定有效电平持续时间
GPIO 复用输出负责把定时器波形送到外部引脚
```

掌握这一节后，后续学习输入捕获、PWM 输入测量、编码器测速，以及更完整的电机闭环控制时，会更容易理解定时器“输出波形”和“测量波形”的两类核心能力。

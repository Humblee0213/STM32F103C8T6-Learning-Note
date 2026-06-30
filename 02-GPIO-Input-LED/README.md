# 02 — GPIO输入：按键控制LED与光敏传感器控制蜂鸣器

## 实验概述

本实验基于 STM32F103C8T6 标准外设库，通过配置 GPIO 为输入模式，读取外部按键和光敏传感器的数字电平，并根据输入状态控制 LED 和蜂鸣器。

本节主要完成两个实验：

1. 使用两个独立按键分别控制两个 LED 翻转
2. 使用光敏传感器检测环境光线，并控制有源蜂鸣器报警

本节的核心目标是熟悉 GPIO 输入的基本流程：

> 开启 GPIO 时钟 → 配置 GPIO 输入模式 → 读取引脚电平 → 根据输入状态控制输出外设

> 说明：由于无法直接获取该视频的完整字幕/画面，本笔记基于可访问的视频标题、课程目录、公开学习笔记以及 STM32F103C8T6 标准库典型写法整理。代码部分为贴合本节实验的参考实现。

## 硬件连接

以 STM32F103C8T6 最小系统板或江协科技配套实验板为例，外设接线如下。

| 器件 | 对应引脚 | 说明 |
|---|---:|---|
| LED1 | PA1 | 低电平点亮 |
| LED2 | PA2 | 低电平点亮 |
| KEY1 | PB1 | 按下接地，低电平有效 |
| KEY2 | PB11 | 按下接地，低电平有效 |
| 有源蜂鸣器 | PB12 | 低电平鸣叫，具体极性以模块电路为准 |
| 光敏传感器 DO | PB13 | 数字量输入，光线阈值由模块电位器调节 |
| ST-Link V2 | PA13、PA14、GND、3.3V | SWD 下载与调试 |

> 注意：不同开发板或传感器模块的触发电平可能不同。如果实验现象相反，需要根据实际原理图或模块输出电平调整判断条件。

## 工程文件结构

建议工程目录按下面方式组织：

```text
02-GPIO-Input-Key-LightSensor
├── Code
│   ├── main.c
│   ├── Delay.c
│   ├── Delay.h
│   ├── LED.c
│   ├── LED.h
│   ├── Key.c
│   ├── Key.h
│   ├── Buzzer.c
│   ├── Buzzer.h
│   ├── LightSensor.c
│   └── LightSensor.h
├── Hardware
│   ├── Key_LED.png
│   └── LightSensor_Buzzer.png
├── Results
│   ├── Key_Control_LED.gif
│   └── LightSensor_Control_Buzzer.gif
└── README.md
```

## 核心代码

### 1. LED.h

```c
#ifndef __LED_H
#define __LED_H

void LED_Init(void);
void LED1_ON(void);
void LED1_OFF(void);
void LED1_Turn(void);
void LED2_ON(void);
void LED2_OFF(void);
void LED2_Turn(void);

#endif
```

### 2. LED.c

```c
#include "stm32f10x.h"

void LED_Init(void)
{
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA, ENABLE);

    GPIO_InitTypeDef GPIO_InitStructure;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_Out_PP;
    GPIO_InitStructure.GPIO_Pin = GPIO_Pin_1 | GPIO_Pin_2;
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(GPIOA, &GPIO_InitStructure);

    GPIO_SetBits(GPIOA, GPIO_Pin_1 | GPIO_Pin_2);   // 默认熄灭 LED
}

void LED1_ON(void)
{
    GPIO_ResetBits(GPIOA, GPIO_Pin_1);
}

void LED1_OFF(void)
{
    GPIO_SetBits(GPIOA, GPIO_Pin_1);
}

void LED1_Turn(void)
{
    if (GPIO_ReadOutputDataBit(GPIOA, GPIO_Pin_1) == 0)
    {
        GPIO_SetBits(GPIOA, GPIO_Pin_1);
    }
    else
    {
        GPIO_ResetBits(GPIOA, GPIO_Pin_1);
    }
}

void LED2_ON(void)
{
    GPIO_ResetBits(GPIOA, GPIO_Pin_2);
}

void LED2_OFF(void)
{
    GPIO_SetBits(GPIOA, GPIO_Pin_2);
}

void LED2_Turn(void)
{
    if (GPIO_ReadOutputDataBit(GPIOA, GPIO_Pin_2) == 0)
    {
        GPIO_SetBits(GPIOA, GPIO_Pin_2);
    }
    else
    {
        GPIO_ResetBits(GPIOA, GPIO_Pin_2);
    }
}
```

### 3. Key.h

```c
#ifndef __KEY_H
#define __KEY_H

#include "stm32f10x.h"

void Key_Init(void);
uint8_t Key_GetNum(void);

#endif
```

### 4. Key.c

```c
#include "stm32f10x.h"
#include "Delay.h"

void Key_Init(void)
{
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOB, ENABLE);

    GPIO_InitTypeDef GPIO_InitStructure;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_IPU;
    GPIO_InitStructure.GPIO_Pin = GPIO_Pin_1 | GPIO_Pin_11;
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(GPIOB, &GPIO_InitStructure);
}

uint8_t Key_GetNum(void)
{
    uint8_t KeyNum = 0;

    if (GPIO_ReadInputDataBit(GPIOB, GPIO_Pin_1) == 0)
    {
        Delay_ms(20);                                      // 消除按下抖动
        while (GPIO_ReadInputDataBit(GPIOB, GPIO_Pin_1) == 0);
        Delay_ms(20);                                      // 消除松手抖动
        KeyNum = 1;
    }

    if (GPIO_ReadInputDataBit(GPIOB, GPIO_Pin_11) == 0)
    {
        Delay_ms(20);
        while (GPIO_ReadInputDataBit(GPIOB, GPIO_Pin_11) == 0);
        Delay_ms(20);
        KeyNum = 2;
    }

    return KeyNum;
}
```

### 5. 按键控制 LED：main.c

```c
#include "stm32f10x.h"
#include "LED.h"
#include "Key.h"

uint8_t KeyNum;

int main(void)
{
    LED_Init();
    Key_Init();

    while (1)
    {
        KeyNum = Key_GetNum();

        if (KeyNum == 1)
        {
            LED1_Turn();
        }

        if (KeyNum == 2)
        {
            LED2_Turn();
        }
    }
}
```

### 6. Buzzer.h

```c
#ifndef __BUZZER_H
#define __BUZZER_H

void Buzzer_Init(void);
void Buzzer_ON(void);
void Buzzer_OFF(void);
void Buzzer_Turn(void);

#endif
```

### 7. Buzzer.c

```c
#include "stm32f10x.h"

void Buzzer_Init(void)
{
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOB, ENABLE);

    GPIO_InitTypeDef GPIO_InitStructure;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_Out_PP;
    GPIO_InitStructure.GPIO_Pin = GPIO_Pin_12;
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(GPIOB, &GPIO_InitStructure);

    GPIO_SetBits(GPIOB, GPIO_Pin_12);    // 默认关闭蜂鸣器
}

void Buzzer_ON(void)
{
    GPIO_ResetBits(GPIOB, GPIO_Pin_12);
}

void Buzzer_OFF(void)
{
    GPIO_SetBits(GPIOB, GPIO_Pin_12);
}

void Buzzer_Turn(void)
{
    if (GPIO_ReadOutputDataBit(GPIOB, GPIO_Pin_12) == 0)
    {
        GPIO_SetBits(GPIOB, GPIO_Pin_12);
    }
    else
    {
        GPIO_ResetBits(GPIOB, GPIO_Pin_12);
    }
}
```

### 8. LightSensor.h

```c
#ifndef __LIGHTSENSOR_H
#define __LIGHTSENSOR_H

#include "stm32f10x.h"

void LightSensor_Init(void);
uint8_t LightSensor_Get(void);

#endif
```

### 9. LightSensor.c

```c
#include "stm32f10x.h"

void LightSensor_Init(void)
{
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOB, ENABLE);

    GPIO_InitTypeDef GPIO_InitStructure;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_IPU;
    GPIO_InitStructure.GPIO_Pin = GPIO_Pin_13;
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(GPIOB, &GPIO_InitStructure);
}

uint8_t LightSensor_Get(void)
{
    return GPIO_ReadInputDataBit(GPIOB, GPIO_Pin_13);
}
```

### 10. 光敏传感器控制蜂鸣器：main.c

```c
#include "stm32f10x.h"
#include "Buzzer.h"
#include "LightSensor.h"

int main(void)
{
    Buzzer_Init();
    LightSensor_Init();

    while (1)
    {
        if (LightSensor_Get() == 1)
        {
            Buzzer_ON();
        }
        else
        {
            Buzzer_OFF();
        }
    }
}
```

> 如果你的光敏传感器模块输出逻辑相反，只需要把 `Buzzer_ON()` 和 `Buzzer_OFF()` 对调即可。

## 代码要点

| 行/段 | 说明 |
|---|---|
| `GPIO_Mode_IPU` | 上拉输入模式。按键未按下时由内部上拉保持高电平，按下后接地变为低电平。 |
| `GPIO_ReadInputDataBit()` | 读取指定 GPIO 引脚的输入电平，返回 0 或 1。 |
| `GPIO_ReadOutputDataBit()` | 读取指定 GPIO 引脚的输出数据状态，常用于实现 LED 翻转。 |
| `Key_GetNum()` | 封装按键扫描逻辑，返回按键编号，使主函数更简洁。 |
| `Delay_ms(20)` | 用于按键软件消抖，避免一次按下被识别成多次。 |
| `while(GPIO_ReadInputDataBit(...) == 0)` | 等待按键松手，保证一次完整按下只触发一次。 |
| `LED_Turn()` | 根据当前 LED 输出状态取反，实现按一次翻转一次。 |
| `LightSensor_Get()` | 读取光敏传感器 DO 引脚的数字输出。 |
| `Buzzer_ON()` / `Buzzer_OFF()` | 封装蜂鸣器开关控制，屏蔽具体高低电平逻辑。 |
| 模块化 `.c` / `.h` | 将 LED、按键、蜂鸣器、光敏传感器分别封装，提高代码可读性和可移植性。 |

## 关键知识点

### 1. GPIO 输入控制流程

#### 原理

GPIO 输入模式下，STM32 的引脚不再主动输出高低电平，而是读取外部电路施加到引脚上的电平状态。

程序通过读取 GPIO 输入数据寄存器 IDR 的某一位，判断对应引脚当前是高电平还是低电平。

GPIO 输入的基本流程如下：

```text
开启 GPIO 时钟 → 配置 GPIO 输入模式 → 读取输入数据寄存器 → 根据电平执行逻辑
```

#### 特点

* GPIO 输入不会主动驱动外部电路。
* 输入状态由外部电路决定。
* 适合读取按键、传感器数字输出、模块状态引脚等。
* 输入模式下常常需要上拉或下拉，避免引脚悬空。

#### 面试易问

**Q：GPIO 输入模式读取的是什么？**

A：读取的是 GPIO 输入数据寄存器 IDR 中对应引脚的电平状态。外部电路为高电平时读到 1，为低电平时读到 0。

**Q：为什么 GPIO 输入引脚不能悬空？**

A：悬空时引脚没有确定的电平来源，可能受到噪声干扰，在 0 和 1 之间随机变化，导致程序误判。

#### 易错点

* 忘记开启 GPIO 时钟，导致输入读取异常。
* 按键电路接法和输入模式不匹配。
* 引脚悬空，没有上拉或下拉，读取结果不稳定。
* 读取函数写错，把输出读取函数当作输入读取函数使用。

---

### 2. 上拉输入

#### 原理

上拉输入是在 GPIO 输入引脚内部接入一个上拉电阻，使引脚在没有外部信号驱动时默认保持高电平。

本实验中按键一端接 GPIO，另一端接 GND：

* 按键未按下时，GPIO 通过内部上拉电阻保持高电平，读取结果为 1。
* 按键按下时，GPIO 被直接接到 GND，读取结果为 0。

因此，这种按键电路属于低电平有效。

#### 特点

* 未按下时默认高电平。
* 按下时变为低电平。
* 可以避免输入引脚悬空。
* 不需要额外外部上拉电阻，适合简单按键输入。
* 常用于按键、开关量检测等场景。

#### 面试易问

**Q：什么是上拉输入？**

A：上拉输入是指在输入引脚上接入上拉电阻，使引脚在没有外部信号驱动时默认保持高电平。

**Q：为什么按键常用上拉输入？**

A：因为按键可以一端接 GPIO，一端接地。未按下时由上拉电阻保持高电平，按下后引脚被拉低，电路简单且状态明确。

#### 易错点

* 按键接地时却配置成下拉输入，导致按键状态判断错误。
* 忘记内部上拉，输入引脚悬空。
* 以为上拉输入按下后一定读 1，实际本实验按下后读 0。
* 外部电路已经有强上拉或强下拉时，仍盲目打开内部上下拉。

---

### 3. 按键消抖

#### 原理

机械按键在按下和松开瞬间，金属触点不会一次性稳定接通或断开，而是会在极短时间内反复抖动。

如果程序直接读取按键电平，可能会把一次按下误识别为多次按下。

本实验采用软件延时消抖：

```text
检测到按下 → 延时 20ms → 等待松手 → 延时 20ms → 返回按键编号
```

#### 特点

* 软件消抖实现简单。
* 适合入门实验和按键数量较少的场景。
* 会阻塞程序运行。
* 按键响应速度受延时时间影响。
* 复杂项目中常用定时器扫描或状态机消抖。

#### 面试易问

**Q：为什么按键需要消抖？**

A：因为机械按键在按下和松开瞬间会产生电平抖动。如果不消抖，一次按下可能被程序识别为多次触发。

**Q：本实验中的 `while` 等待松手有什么作用？**

A：它可以保证一次按下只返回一次按键编号，避免按住按键时程序连续触发 LED 翻转。

#### 易错点

* 不加消抖，LED 一次按下翻转多次。
* 消抖延时太短，抖动仍然存在。
* 消抖延时太长，按键响应变慢。
* 在复杂系统中过度使用阻塞式等待松手，影响其他任务执行。

---

### 4. 模块化编程

#### 原理

模块化编程是将不同外设的驱动代码分别放到独立的 `.c` 和 `.h` 文件中。

`.c` 文件负责函数实现，`.h` 文件负责对外声明。主函数只需要调用模块提供的接口，不需要关心内部细节。

#### 特点

* 主函数更简洁。
* 外设驱动更容易复用。
* 修改某个外设代码时影响范围更小。
* 便于后续工程扩展。
* 更接近实际项目开发习惯。

#### 面试易问

**Q：为什么要把 LED、按键、蜂鸣器分别封装成模块？**

A：这样可以降低主函数复杂度，提高代码复用性和可维护性。以后更换引脚或修改控制逻辑时，只需要改对应模块，不需要大范围修改主函数。

**Q：`.h` 文件和 `.c` 文件分别有什么作用？**

A：`.h` 文件通常用于函数声明、宏定义和类型声明；`.c` 文件用于实现具体函数逻辑。

#### 易错点

* 写了 `.c` 文件但没有加入 Keil 工程，导致链接错误。
* 头文件没有防止重复包含。
* 函数声明和函数定义不一致。
* 主函数没有包含对应模块的头文件。

---

### 5. 按键控制 LED

#### 原理

按键控制 LED 的本质是“输入状态改变输出状态”。

程序不断扫描按键状态：

* 如果没有按键按下，返回 0。
* 如果 KEY1 被按下，返回 1，程序翻转 LED1。
* 如果 KEY2 被按下，返回 2，程序翻转 LED2。

#### 特点

* 按键是输入设备。
* LED 是输出设备。
* 按键按下不是直接点亮 LED，而是通过程序逻辑控制 LED。
* `LED_Turn()` 根据当前输出状态取反，适合实现按一次变一次。

#### 面试易问

**Q：如何实现按键按一次 LED 状态翻转一次？**

A：先扫描按键并进行消抖，确认一次完整按下后返回按键编号；然后读取 LED 当前输出状态，如果当前亮就关，如果当前灭就开。

**Q：为什么不能直接在 `while(1)` 中判断低电平就翻转 LED？**

A：因为按键按下期间会持续保持低电平，程序循环速度很快，会导致 LED 在一次按住期间连续翻转很多次。

#### 易错点

* 忘记等待按键松手，导致一次按下触发多次。
* 低电平有效判断写反。
* LED 是低电平点亮，导致开关函数命名和实际电平容易混淆。
* `LED_Turn()` 读取的是输出状态，不是输入状态。

---

### 6. 光敏传感器数字输出

#### 原理

常见光敏传感器模块由光敏电阻、电位器、比较器和数字输出引脚 DO 组成。

光敏电阻随光照强度变化而改变阻值，模块内部比较器将模拟电压和电位器设定的阈值进行比较，最终在 DO 引脚输出高电平或低电平。

程序不需要直接计算光照强度，只需要读取 DO 引脚的数字电平即可。

#### 特点

* 输出结果只有 0 和 1。
* 判断阈值可以通过模块上的电位器调节。
* 适合做简单的明暗检测。
* 无法得到精确光照强度。
* 如果需要精确测量光照强度，应使用 ADC 读取模拟量。

#### 面试易问

**Q：光敏传感器模块的 DO 引脚输出的是什么？**

A：DO 引脚输出的是比较器处理后的数字电平，只有高电平和低电平，用于表示当前光照是否超过设定阈值。

**Q：数字量光敏传感器能不能测出具体光照强度？**

A：不能。数字输出只能判断是否达到阈值，如果要获得连续的光照强度，需要使用模拟输出并通过 ADC 采集。

#### 易错点

* 把 DO 数字输出误认为可以读出具体光照值。
* 传感器阈值电位器没有调好，导致输出一直为 0 或一直为 1。
* 不同模块的亮/暗输出逻辑可能相反。
* 只接了 VCC 和 GND，忘记连接 DO 到 STM32 输入引脚。

---

### 7. 光敏传感器控制蜂鸣器

#### 原理

光敏传感器控制蜂鸣器的本质也是“输入控制输出”。

程序持续读取光敏传感器 DO 引脚：

* 当光线低于或高于设定阈值时，传感器输出某一固定电平。
* STM32 读取到该电平后，根据判断条件打开或关闭蜂鸣器。
* 蜂鸣器的响停逻辑取决于传感器输出逻辑和蜂鸣器触发电平。

#### 特点

* 光敏传感器负责检测环境明暗。
* STM32 负责读取电平并执行判断。
* 蜂鸣器负责输出声音提示。
* 阈值由传感器模块电位器决定。
* 适合实现简单的光照报警、遮挡检测等功能。

#### 面试易问

**Q：为什么光敏传感器可以直接接 GPIO，而不一定要接 ADC？**

A：如果使用的是带比较器的光敏传感器模块，DO 引脚已经输出数字电平，GPIO 可以直接读取。如果想获取连续光照强度，才需要接 ADC。

**Q：为什么遮住光敏电阻后蜂鸣器可能不响？**

A：可能是光敏模块阈值没有调好，输出逻辑判断写反，蜂鸣器触发电平相反，或者 GPIO 引脚接线和代码不一致。

#### 易错点

* 光敏模块输出逻辑和代码判断相反。
* 蜂鸣器高低电平触发判断错误。
* 模块电位器阈值不合适，环境变化无法触发输出翻转。
* 光敏传感器输入和蜂鸣器输出使用了相同或错误的 GPIO 引脚。

---

## 本节核心记忆

```text
GPIO 输入 = RCC 时钟 + GPIO 初始化 + 读取输入数据寄存器
```

```text
上拉输入：默认高电平，按下接地后变为低电平
```

```text
按键控制 LED 的关键：消抖 + 等待松手 + 状态翻转
```

```text
光敏传感器 DO 输出的是数字量，只能判断是否达到阈值
```

```text
输入设备提供状态，输出设备执行动作
```

## 开发过程总结

### 问题 1：按键按下后 LED 没反应

现象：

* 程序编译和下载正常
* 按下按键后 LED 不亮或不翻转
* LED 单独测试可以正常点亮

排查过程：

1. 检查按键 GPIO 端口和引脚是否与接线一致
2. 检查是否开启了 GPIOB 时钟
3. 检查按键是否配置为上拉输入
4. 检查按键是否一端接 GPIO，另一端接 GND
5. 检查主函数是否调用了 `Key_Init()` 和 `LED_Init()`

解决方案：

* 确认 `GPIO_Mode_IPU` 和按键接地电路匹配
* 使用 `GPIO_ReadInputDataBit()` 读取按键输入
* 如果按键按下为高电平，则修改判断条件
* 先用简单程序读取按键状态，再加入 LED 翻转逻辑

### 问题 2：按一次按键，LED 状态变化很多次

现象：

* 按下一次按键，LED 快速闪烁
* 松手后 LED 停在随机状态
* 按键控制不稳定

排查过程：

1. 检查是否加入按键消抖
2. 检查是否等待按键松手
3. 检查 `while` 循环中是否持续触发翻转
4. 检查消抖延时时间是否过短

解决方案：

* 检测到按下后加入 `Delay_ms(20)`
* 使用 `while` 等待按键松手
* 松手后再加入一次 `Delay_ms(20)`
* 复杂项目可改用定时器扫描或状态机消抖

### 问题 3：光敏传感器不受控制

现象：

* 遮住或照亮光敏电阻，蜂鸣器状态不变
* 传感器模块指示灯有变化，但程序无反应
* 程序读取结果一直为 0 或一直为 1

排查过程：

1. 检查传感器 VCC、GND、DO 是否连接正确
2. 检查 DO 是否接到代码中的 PB13
3. 检查 GPIO 是否配置为输入模式
4. 调节光敏模块上的电位器阈值
5. 判断模块输出逻辑是否与代码相反

解决方案：

* 调整电位器，使光照变化时 DO 电平能够翻转
* 使用万用表或模块指示灯判断 DO 输出状态
* 若逻辑相反，交换 `Buzzer_ON()` 和 `Buzzer_OFF()`
* 确认光敏传感器模块输出的是数字量 DO，而不是模拟量 AO

### 问题 4：蜂鸣器不响或一直响

现象：

* 光敏传感器状态变化后蜂鸣器没有响应
* 蜂鸣器一直鸣叫，无法关闭
* 蜂鸣器响停逻辑与预期相反

排查过程：

1. 检查蜂鸣器是否为有源蜂鸣器
2. 检查蜂鸣器控制引脚是否为 PB12
3. 检查蜂鸣器是高电平触发还是低电平触发
4. 检查 `Buzzer_ON()` 和 `Buzzer_OFF()` 是否写反
5. 检查蜂鸣器供电和 GND 是否正常

解决方案：

* 先单独测试蜂鸣器开关函数
* 根据实际模块触发电平调整 `SetBits` 和 `ResetBits`
* 如果是无源蜂鸣器，需要使用 PWM 或方波驱动
* 电流较大时，使用三极管或 MOS 管驱动蜂鸣器

## 结果展示

> 实验 1：烧录按键控制 LED 程序后，按下 KEY1，LED1 状态翻转；按下 KEY2，LED2 状态翻转。✅

> 实验 2：烧录光敏传感器控制蜂鸣器程序后，遮挡光敏电阻或改变环境光线，蜂鸣器根据传感器输出状态响起或停止。✅

> 实验 3：调整光敏模块电位器后，可以改变蜂鸣器触发的光照阈值。✅

## 本节小结

本节通过按键控制 LED 和光敏传感器控制蜂鸣器两个实验，掌握了 STM32 GPIO 输入的基本使用方法。

最重要的知识点是：

```text
GPIO 输入读取外部状态，GPIO 输出控制外部设备
```

按键实验帮助我们理解了上拉输入、低电平有效和按键消抖；光敏传感器实验帮助我们理解了传感器数字输出、阈值判断和输入输出联动控制。

后续学习外部中断、定时器、OLED 显示、传感器采集等内容时，也会继续沿用这种“外设初始化 + 状态读取 + 业务判断 + 输出控制”的程序结构。

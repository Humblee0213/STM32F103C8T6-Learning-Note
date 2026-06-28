# 01 — GPIO输出：LED闪烁、LED流水灯与蜂鸣器

## 实验概述

本实验基于 STM32F103C8T6 标准外设库，通过配置 GPIO 为推挽输出模式，实现三个基础输出控制功能：

1. 单个 LED 周期闪烁
2. 多个 LED 依次点亮，形成流水灯效果
3. 使用 GPIO 控制有源蜂鸣器间歇鸣叫

本节的核心目标是熟悉 GPIO 输出的基本流程：

> 开启 GPIO 时钟 → 配置 GPIO 输出模式 → 调用 GPIO 输出函数 → 配合延时形成可观察现象

## 硬件连接

以 STM32F103C8T6 最小系统板或江协科技配套实验板为例，外设接线如下。

| 器件         |               对应引脚 | 说明                |
| ---------- | -----------------: | ----------------- |
| LED1       |                PA0 | 低电平点亮             |
| LED 流水灯    |          PA0 ~ PA7 | 8 个 LED 依次点亮      |
| 有源蜂鸣器      |               PB12 | 低电平鸣叫，具体极性以模块电路为准 |
| ST-Link V2 | PA13、PA14、GND、3.3V | SWD 下载与调试         |

> 注意：不同开发板的 LED 和蜂鸣器触发电平可能不同。如果实验现象相反，只需要将 `GPIO_SetBits()` 和 `GPIO_ResetBits()` 对调即可。

## 工程文件结构

建议工程目录按下面方式组织：

```text
02-GPIO-Output-LED-Buzzer
├── Code
│   ├── main.c
│   ├── Delay.c
│   └── Delay.h
├── Hardware
│   └── GPIO_Output_LED_Buzzer.png
├── Results
│   ├── LED_Blink.gif
│   ├── LED_Waterfall.gif
│   └── Buzzer_Test.gif
└── README.md
```

## 核心代码

### 1. Delay.h

```c
#ifndef __DELAY_H
#define __DELAY_H

#include "stm32f10x.h"

void Delay_us(uint32_t xus);
void Delay_ms(uint32_t xms);
void Delay_s(uint32_t xs);

#endif
```

### 2. Delay.c

```c
#include "Delay.h"

/**
  * @brief  微秒级延时
  * @param  xus 延时时长，单位：us
  * @retval 无
  */
void Delay_us(uint32_t xus)
{
    SysTick->LOAD = 72 * xus;
    SysTick->VAL = 0x00;
    SysTick->CTRL = 0x00000005;

    while (!(SysTick->CTRL & 0x00010000));

    SysTick->CTRL = 0x00000004;
}

/**
  * @brief  毫秒级延时
  * @param  xms 延时时长，单位：ms
  * @retval 无
  */
void Delay_ms(uint32_t xms)
{
    while (xms--)
    {
        Delay_us(1000);
    }
}

/**
  * @brief  秒级延时
  * @param  xs 延时时长，单位：s
  * @retval 无
  */
void Delay_s(uint32_t xs)
{
    while (xs--)
    {
        Delay_ms(1000);
    }
}
```

### 3. LED 闪烁

```c
#include "stm32f10x.h"
#include "Delay.h"

int main(void)
{
    // 1. 开启 GPIOA 时钟
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA, ENABLE);

    // 2. 配置 PA0 为推挽输出
    GPIO_InitTypeDef GPIO_InitStructure;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_Out_PP;
    GPIO_InitStructure.GPIO_Pin = GPIO_Pin_0;
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(GPIOA, &GPIO_InitStructure);

    // 3. 循环控制 LED 闪烁
    while (1)
    {
        GPIO_ResetBits(GPIOA, GPIO_Pin_0);    // PA0 输出低电平，LED 点亮
        Delay_ms(500);

        GPIO_SetBits(GPIOA, GPIO_Pin_0);      // PA0 输出高电平，LED 熄灭
        Delay_ms(500);
    }
}
```

### 4. LED 流水灯

```c
#include "stm32f10x.h"
#include "Delay.h"

int main(void)
{
    // 1. 开启 GPIOA 时钟
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA, ENABLE);

    // 2. 配置 PA0 ~ PA7 为推挽输出
    GPIO_InitTypeDef GPIO_InitStructure;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_Out_PP;
    GPIO_InitStructure.GPIO_Pin =
        GPIO_Pin_0 | GPIO_Pin_1 | GPIO_Pin_2 | GPIO_Pin_3 |
        GPIO_Pin_4 | GPIO_Pin_5 | GPIO_Pin_6 | GPIO_Pin_7;
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(GPIOA, &GPIO_InitStructure);

    // 3. 依次点亮 LED
    while (1)
    {
        GPIO_Write(GPIOA, ~0x0001);
        Delay_ms(200);

        GPIO_Write(GPIOA, ~0x0002);
        Delay_ms(200);

        GPIO_Write(GPIOA, ~0x0004);
        Delay_ms(200);

        GPIO_Write(GPIOA, ~0x0008);
        Delay_ms(200);

        GPIO_Write(GPIOA, ~0x0010);
        Delay_ms(200);

        GPIO_Write(GPIOA, ~0x0020);
        Delay_ms(200);

        GPIO_Write(GPIOA, ~0x0040);
        Delay_ms(200);

        GPIO_Write(GPIOA, ~0x0080);
        Delay_ms(200);
    }
}
```

### 5. 蜂鸣器控制

```c
#include "stm32f10x.h"
#include "Delay.h"

int main(void)
{
    // 1. 开启 GPIOB 时钟
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOB, ENABLE);

    // 2. 配置 PB12 为推挽输出
    GPIO_InitTypeDef GPIO_InitStructure;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_Out_PP;
    GPIO_InitStructure.GPIO_Pin = GPIO_Pin_12;
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(GPIOB, &GPIO_InitStructure);

    // 3. 控制蜂鸣器间歇鸣叫
    while (1)
    {
        GPIO_ResetBits(GPIOB, GPIO_Pin_12);   // PB12 输出低电平，蜂鸣器响
        Delay_ms(500);

        GPIO_SetBits(GPIOB, GPIO_Pin_12);     // PB12 输出高电平，蜂鸣器停
        Delay_ms(500);
    }
}
```

## 代码要点

| 行/段                        | 说明                                                       |
| -------------------------- | -------------------------------------------------------- |
| `RCC_APB2PeriphClockCmd()` | 开启 GPIO 外设时钟。GPIOA、GPIOB、GPIOC 均挂载在 APB2 总线上，使用前必须先开启时钟。 |
| `GPIO_InitTypeDef`         | GPIO 初始化结构体，用于配置模式、引脚、输出速度。                              |
| `GPIO_Mode_Out_PP`         | 推挽输出模式，可以主动输出高电平和低电平，适合 LED、蜂鸣器等普通输出场景。                  |
| `GPIO_SetBits()`           | 将指定引脚置为高电平。                                              |
| `GPIO_ResetBits()`         | 将指定引脚置为低电平。                                              |
| `GPIO_WriteBit()`          | 按第三个参数写入高/低电平，适合单个引脚控制。                                  |
| `GPIO_Write()`             | 一次性写入整个 GPIO 端口的 16 位数据，适合流水灯这类多引脚同步控制。                  |
| `Delay_ms()`               | 软件延时函数，用来控制闪烁、流水灯和蜂鸣器的节奏。                                |

## 关键知识点

### 1. GPIO 输出控制流程

#### 原理

STM32 的 GPIO 引脚本质上由片上寄存器控制。程序通过写寄存器，改变 GPIO 输出数据寄存器中的某一位，从而控制对应引脚输出高电平或低电平。

GPIO 输出的基本流程如下：

```text
开启 GPIO 时钟 → 配置 GPIO 模式 → 写输出寄存器 → 引脚输出高/低电平
```

在标准外设库中，通常通过以下函数完成：

```c
RCC_APB2PeriphClockCmd();   // 开启 GPIO 时钟
GPIO_Init();                // 初始化 GPIO
GPIO_SetBits();             // 输出高电平
GPIO_ResetBits();           // 输出低电平
GPIO_Write();               // 向整个端口写入数据
```

#### 特点

* GPIO 使用前必须先开启对应外设时钟。
* GPIOA、GPIOB、GPIOC 等端口挂载在 APB2 总线上。
* GPIO 可以配置成输入、输出、复用、模拟等多种模式。
* 输出模式下，可以通过程序控制引脚电平。

#### 面试易问

**Q：为什么使用 GPIO 前必须先开启 RCC 时钟？**

A：STM32 为了降低功耗，外设默认时钟是关闭的。如果没有开启 GPIO 对应的时钟，即使配置了 GPIO 寄存器，GPIO 外设也无法正常工作。

**Q：GPIO 输出高低电平的本质是什么？**

A：本质是程序修改 GPIO 输出数据寄存器的对应位。寄存器位为 1 时，引脚输出高电平；寄存器位为 0 时，引脚输出低电平。

#### 易错点

* 忘记开启 GPIO 时钟，导致程序编译通过但硬件没有反应。
* GPIO 端口和引脚写错，例如硬件接在 PA0，代码却初始化 PB0。
* 初始化结构体没有完整赋值，导致 GPIO 工作异常。
* 只写了控制电平代码，但没有先初始化 GPIO。

---

### 2. 推挽输出

#### 原理

推挽输出由输出寄存器通过一对互补的 MOSFET 驱动引脚。

可以简单理解为：GPIO 内部有一个上管和一个下管。

* 当输出寄存器写 1 时，上管导通、下管关闭，引脚被主动拉到高电平。
* 当输出寄存器写 0 时，上管关闭、下管导通，引脚被主动拉到低电平。

因此，推挽输出既能主动输出高电平，也能主动输出低电平。

#### 特点

* 可以主动输出高电平。
* 可以主动输出低电平。
* 驱动能力较强。
* 响应速度快。
* 一般不需要外部上拉电阻。
* 适合控制 LED、蜂鸣器、片选信号、普通数字输出等场景。

#### 面试易问

**Q：推挽输出和开漏输出有什么区别？**

A：推挽输出可以主动输出高电平和低电平；开漏输出只能主动输出低电平，输出高电平时需要依靠上拉电阻。

**Q：什么时候使用推挽输出？**

A：当 GPIO 需要直接输出稳定的高低电平，并且不需要多个设备共用一根线时，通常使用推挽输出。例如控制 LED、蜂鸣器、继电器驱动信号等。

**Q：推挽输出为什么驱动能力比开漏强？**

A：因为推挽输出内部既有上拉驱动管，也有下拉驱动管，输出高低电平均由芯片内部主动驱动；而开漏输出高电平依赖外部上拉电阻，驱动能力较弱。

#### 易错点

* 推挽输出引脚不能直接短接到另一个推挽输出引脚。
* 如果一个推挽输出引脚输出高电平，另一个输出低电平，短接后会形成大电流，可能损坏芯片。
* 不要把推挽输出直接挂到需要线与功能的总线上。
* 不适合多个设备共用一根信号线的场景。

---

### 3. 开漏输出

#### 原理

开漏输出内部只有下拉 MOSFET，没有主动输出高电平的上拉 MOSFET。

* 当输出寄存器写 0 时，下管导通，引脚被拉到低电平。
* 当输出寄存器写 1 时，下管关闭，引脚处于高阻态。
* 如果需要得到高电平，必须依靠外部上拉电阻或内部上拉电阻。

所以开漏输出并不是“主动输出高电平”，而是“释放总线，由上拉电阻拉高”。

#### 特点

* 可以主动输出低电平。
* 不能主动输出高电平。
* 输出高电平时需要上拉电阻。
* 可以实现多个器件共用一根信号线。
* 常用于 I2C 总线、单总线通信、电平转换等场景。

#### 面试易问

**Q：为什么 I2C 通常使用开漏输出？**

A：因为 I2C 总线上可能有多个设备同时连接到 SDA 和 SCL 线上。使用开漏输出可以避免多个设备同时驱动总线时发生冲突，同时还能实现线与功能。

**Q：开漏输出能不能输出高电平？**

A：不能主动输出高电平。开漏输出写 1 时，引脚进入高阻态，需要依靠上拉电阻把电平拉高。

**Q：开漏输出为什么可以实现电平转换？**

A：因为输出高电平由外部上拉电阻决定。如果上拉到 5V，总线高电平就是 5V；如果上拉到 3.3V，总线高电平就是 3.3V。

#### 易错点

* 忘记接上拉电阻，导致引脚无法得到稳定高电平。
* 把开漏输出误认为可以主动输出高电平。
* 上拉电阻阻值选择不合适，阻值太大上升沿慢，阻值太小功耗增加。
* 普通 LED 控制一般不需要开漏输出，推挽输出更直接。

---

### 4. LED 的高低电平触发

#### 原理

LED 是否点亮，取决于是否有正向电流流过 LED。

常见接法有两种：

| 接法                   | GPIO 输出 | LED 状态 |
| -------------------- | ------: | ------ |
| LED 正极接 VCC，负极接 GPIO |     低电平 | 点亮     |
| LED 正极接 GPIO，负极接 GND |     高电平 | 点亮     |

本实验默认采用低电平点亮方式：

```c
GPIO_ResetBits(GPIOA, GPIO_Pin_0);  // 输出低电平，LED 点亮
GPIO_SetBits(GPIOA, GPIO_Pin_0);    // 输出高电平，LED 熄灭
```

#### 特点

* LED 是有方向的，必须正向导通才会发光。
* 长脚通常是正极，短脚通常是负极。
* LED 电路中一般需要串联限流电阻。
* 不同开发板的 LED 触发电平可能不同。

#### 面试易问

**Q：为什么有些开发板的 LED 是低电平点亮？**

A：因为 LED 的正极接到了 VCC，负极接到了 GPIO。当 GPIO 输出低电平时，电流从 VCC 流过 LED 再流入 GPIO，引脚相当于灌电流，所以 LED 点亮。

**Q：GPIO 控制 LED 时，是高电平点亮还是低电平点亮？**

A：不一定，要看硬件接法。如果 LED 正极接 GPIO、负极接 GND，则高电平点亮；如果 LED 正极接 VCC、负极接 GPIO，则低电平点亮。

#### 易错点

* 没看原理图，默认认为 LED 一定是高电平点亮。
* LED 正负极接反，导致无法点亮。
* 没有限流电阻，可能烧坏 LED 或 GPIO。
* 代码逻辑和硬件触发电平相反，导致亮灭现象与预期相反。

---

### 5. GPIO 输出函数

#### 原理

标准外设库对寄存器操作进行了封装。我们不直接操作寄存器，而是调用库函数来控制 GPIO 输出。

常用函数如下：

```c
GPIO_SetBits(GPIOx, GPIO_Pin);       // 指定引脚输出高电平
GPIO_ResetBits(GPIOx, GPIO_Pin);     // 指定引脚输出低电平
GPIO_WriteBit(GPIOx, GPIO_Pin, BitVal); // 指定引脚写入高/低电平
GPIO_Write(GPIOx, PortVal);          // 向整个 GPIO 端口写入 16 位数据
```

#### 特点

* `GPIO_SetBits()` 适合单独拉高某个引脚。
* `GPIO_ResetBits()` 适合单独拉低某个引脚。
* `GPIO_WriteBit()` 适合根据变量控制某个引脚。
* `GPIO_Write()` 可以一次性控制整个端口，适合流水灯等多引脚场景。

#### 面试易问

**Q：GPIO_SetBits 和 GPIO_ResetBits 的区别是什么？**

A：`GPIO_SetBits()` 用来将指定引脚置 1，输出高电平；`GPIO_ResetBits()` 用来将指定引脚清 0，输出低电平。

**Q：GPIO_Write 和 GPIO_SetBits 有什么区别？**

A：`GPIO_SetBits()` 只影响指定引脚；`GPIO_Write()` 会一次性写入整个 GPIO 端口的 16 位输出数据，可能影响同一端口上的其他引脚。

#### 易错点

* 使用 `GPIO_Write()` 时，没有意识到它会修改整个端口。
* 同一端口上有多个外设时，随意使用 `GPIO_Write()` 可能影响其他引脚。
* 低电平点亮 LED 时，`GPIO_SetBits()` 反而是熄灭。
* 函数参数中的 GPIO 端口和 GPIO 引脚不匹配。

---

### 6. 流水灯

#### 原理

流水灯的本质是多个 GPIO 按顺序输出不同的电平状态。

例如 8 个 LED 接在 PA0 ~ PA7 上，每次只点亮一个 LED：

```text
1111 1110
1111 1101
1111 1011
1111 0111
```

如果 LED 是低电平点亮，那么哪一位为 0，哪一个 LED 就点亮。程序不断改变输出数据，再加入适当延时，就形成了 LED 依次移动的视觉效果。

#### 特点

* 本质上仍然是 GPIO 输出实验。
* 需要同时配置多个 GPIO 引脚。
* 可以用多个 `GPIO_SetBits()` / `GPIO_ResetBits()` 实现。
* 也可以用 `GPIO_Write()` 一次性写整个端口。
* 通过修改延时时间，可以改变流水速度。

#### 面试易问

**Q：流水灯的实现原理是什么？**

A：通过程序依次改变多个 GPIO 引脚的输出电平，使多个 LED 按顺序点亮和熄灭，再利用人眼视觉暂留形成流动效果。

**Q：为什么流水灯常用 GPIO_Write？**

A：因为流水灯需要同时控制多个引脚状态，`GPIO_Write()` 可以一次性写入整个端口的数据，代码更简洁。

#### 易错点

* 使用 `GPIO_Write()` 时影响了同一端口上的其他引脚。
* 没有初始化所有 LED 对应的 GPIO 引脚。
* LED 是低电平点亮时，输出数据需要取反。
* 延时时间太短，看不清流水效果；延时时间太长，流动效果不明显。

---

### 7. 蜂鸣器控制

#### 原理

蜂鸣器可以分为有源蜂鸣器和无源蜂鸣器。

| 类型    | 原理       | 控制方式          |
| ----- | -------- | ------------- |
| 有源蜂鸣器 | 内部自带振荡电路 | 给高/低电平即可发声    |
| 无源蜂鸣器 | 内部没有振荡电路 | 需要外部提供一定频率的方波 |

本实验使用的是有源蜂鸣器，所以控制方式类似 LED：

```c
GPIO_ResetBits(GPIOB, GPIO_Pin_12);  // 蜂鸣器响
GPIO_SetBits(GPIOB, GPIO_Pin_12);    // 蜂鸣器停
```

如果蜂鸣器模块是高电平触发，则上述逻辑相反。

#### 特点

* 有源蜂鸣器控制简单，只需要 GPIO 输出固定电平。
* 无源蜂鸣器需要 PWM 或定时器产生方波。
* 蜂鸣器一般电流较大，实际项目中常使用三极管或 MOS 管驱动。
* 蜂鸣器触发电平需要根据模块电路判断。

#### 面试易问

**Q：有源蜂鸣器和无源蜂鸣器有什么区别？**

A：有源蜂鸣器内部自带振荡源，通电即可发声；无源蜂鸣器内部没有振荡源，需要外部输入一定频率的方波才能发声。

**Q：为什么实际项目中不建议 GPIO 直接驱动蜂鸣器？**

A：因为蜂鸣器工作电流可能超过 GPIO 的安全驱动能力，长期直接驱动可能损坏芯片。实际项目中通常使用三极管、MOS 管或驱动芯片进行隔离和放大。

#### 易错点

* 把无源蜂鸣器当作有源蜂鸣器使用，只给固定电平，结果不响。
* 蜂鸣器触发电平判断错误，导致响停逻辑相反。
* 蜂鸣器电流较大时，直接用 GPIO 驱动不安全。
* GPIO 引脚和实际接线不一致，导致程序没有控制到蜂鸣器。

---

### 8. 软件延时

#### 原理

软件延时通过 CPU 执行空循环或使用 SysTick 计数器，让程序暂停一段时间。

本实验中的 LED 闪烁、流水灯、蜂鸣器间歇鸣叫，都依赖延时函数控制节奏。

例如：

```c
GPIO_ResetBits(GPIOA, GPIO_Pin_0);
Delay_ms(500);

GPIO_SetBits(GPIOA, GPIO_Pin_0);
Delay_ms(500);
```

#### 特点

* 实现简单，适合入门实验。
* 可以直观控制 LED 闪烁速度和蜂鸣器响停时间。
* 延时期间 CPU 一直被占用，无法执行其他任务。
* 精度受系统时钟配置、编译优化等因素影响。

#### 面试易问

**Q：软件延时有什么缺点？**

A：软件延时会阻塞 CPU。在延时期间，CPU 不能处理其他任务，因此不适合复杂项目。

**Q：实际项目中一般用什么代替软件延时？**

A：通常使用定时器中断、SysTick 定时、RTOS 延时函数等方式代替阻塞式软件延时。

#### 易错点

* 系统时钟频率与延时函数假设不一致，导致延时时间不准确。
* 在复杂程序中过度使用软件延时，导致系统响应变慢。
* 延时函数写在中断服务函数中，影响系统实时性。
* 以为 `Delay_ms(500)` 期间程序还能同时执行其他任务。

---

## 本节核心记忆

```text
GPIO 输出 = RCC 时钟 + GPIO 初始化 + 输出寄存器控制
```

```text
推挽输出：能主动输出高，也能主动输出低
开漏输出：只能主动输出低，高电平依靠上拉电阻
```

```text
LED 是否点亮，不看代码函数名，而要看硬件接法
```

```text
有源蜂鸣器给固定电平即可响，无源蜂鸣器需要方波
```


## 开发过程总结

### 问题 1：代码编译成功，但 LED 不亮

现象：

* Keil 编译 0 Error
* 程序可以下载
* LED 没有反应

排查过程：

1. 检查 GPIO 端口和引脚是否写对
2. 检查是否开启了对应 GPIO 的 RCC 时钟
3. 检查 LED 是高电平点亮还是低电平点亮
4. 检查接线方向，LED 长脚为正极、短脚为负极
5. 检查是否真的运行了 `main()` 函数

解决方案：

* 确认 `RCC_APB2PeriphClockCmd()` 已开启对应端口时钟
* 若 LED 逻辑反了，交换 `SetBits` 和 `ResetBits`
* 烧录完成后手动按 Reset 键重新运行程序

### 问题 2：流水灯只有部分 LED 亮

可能原因：

* 部分 GPIO 引脚未初始化
* `GPIO_Pin` 没有把 PA0 ~ PA7 全部包含进去
* 面包板或杜邦线接触不良
* LED 极性接反

解决方案：

* 使用按位或 `|` 同时选择多个引脚
* 用万用表或单灯测试逐个排查
* 先让所有 LED 常亮，确认硬件无误后再写流水灯逻辑

### 问题 3：蜂鸣器不响

可能原因：

* 蜂鸣器接线错误
* 蜂鸣器触发电平与代码相反
* 使用的是无源蜂鸣器，但代码只输出固定电平
* GPIO 引脚和实际接线不一致

解决方案：

* 先确认蜂鸣器是有源还是无源
* 直接给蜂鸣器供电测试是否能响
* 将 `GPIO_SetBits()` 和 `GPIO_ResetBits()` 对调测试
* 按实际原理图修改蜂鸣器控制引脚

## 结果展示

> 实验 1：烧录 LED 闪烁程序后，PA0 连接的 LED 以 500ms 周期亮灭交替。✅

> 实验 2：烧录流水灯程序后，PA0 ~ PA7 连接的 LED 从左到右依次点亮。✅

> 实验 3：烧录蜂鸣器程序后，PB12 控制蜂鸣器每隔 500ms 响一次、停一次。✅

## 本节小结

本节通过 LED 闪烁、LED 流水灯和蜂鸣器三个实验，掌握了 STM32 GPIO 输出的基本使用方法。

最重要的知识点是：

```text
GPIO 输出 = RCC 时钟 + GPIO 初始化 + 高低电平控制
```

后续学习按键输入、OLED、外部中断、定时器、PWM 等内容时，也都会沿用这种“先初始化外设，再调用函数控制外设”的思路。

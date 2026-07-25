# 13-2 — PWR：修改主频、睡眠模式、停止模式与待机模式

> 视频来源：[江协科技 STM32 入门教程 - [13-2] 修改主频&睡眠模式&停止模式&待机模式](https://www.bilibili.com/video/BV1th411z7sn?p=45)
>
> 说明：B 站公开视频信息接口可确认本节标题、分 P、UP 主和时长，但该分 P 未提供可直接访问的字幕列表。因此以下内容基于可访问的视频标题、课程上下文、STM32F10x 标准外设库用法、ST 官方参考手册与低功耗应用笔记整理，核心代码为参考实现，不标注为视频逐字源码。

## 实验概述

本节围绕 STM32F103C8T6 的 RCC (Reset and Clock Control) 时钟系统和 PWR (Power Control) 电源控制外设，完成四个核心实验：

1. 修改系统主频，理解 HSE、HSI、PLL、AHB、APB1、APB2 之间的时钟关系。
2. 进入睡眠模式 (Sleep Mode)，观察 CPU 停止、外设继续运行的低功耗状态。
3. 进入停止模式 (Stop Mode)，观察 HSE 和 PLL 关闭后系统功耗进一步降低，以及唤醒后需要恢复系统时钟。
4. 进入待机模式 (Standby Mode)，理解最低功耗状态下 SRAM 和寄存器丢失、唤醒等价于复位的特点。

本节的核心目标是把“主频越高性能越强、功耗越高”和“低功耗模式越深，唤醒代价越大”这两条线串起来。

```text
性能需求高 → 提高主频
任务空闲短 → Sleep
任务空闲较长但还要回到现场 → Stop
长期休眠且允许重新启动 → Standby
```

## 硬件连接

本节大部分内容依赖芯片内部 RCC、PWR、EXTI 和 NVIC 资源，外部硬件主要用于观察现象和触发唤醒。

| 器件 | 对应引脚 | 说明 |
|---|---:|---|
| STM32F103C8T6 | 内部 RCC / PWR | 修改主频、进入低功耗模式均由芯片内部外设完成 |
| OLED 显示屏 | PB8 / PB9 | 课程常用软件 I2C OLED，用于显示主频状态或运行计数 |
| LED | PA0 | 用于观察程序是否仍在运行，具体触发电平以开发板原理图为准 |
| 按键 | PA0 / PB1 等 | 可用于外部中断唤醒，实际引脚按自己的 Key 驱动配置为准 |
| WKUP 唤醒引脚 | PA0 | Standby 模式常用唤醒脚，上升沿可唤醒芯片 |
| ST-Link V2 | PA13、PA14、GND、3.3V | SWD 下载与调试 |
| 电流表或 USB 电流检测仪 | 电源输入端 | 用于比较不同主频和低功耗模式下的电流变化 |

> 注意：低功耗实验最重要的是“唤醒源”。Sleep 和 Stop 一般可由中断唤醒；Standby 常用 WKUP 引脚、RTC 闹钟、IWDG 复位或 NRST 唤醒，醒来后程序从复位流程重新开始。

## 工程文件结构

建议本节工程按下面方式组织：

```text
PWR-Frequency-LowPower
├── Code
│   ├── main.c
│   ├── MyRCC.c
│   ├── MyRCC.h
│   ├── MyPWR.c
│   └── MyPWR.h
├── Hardware
│   └── PWR_LowPower_Connection.jpg
├── Results
│   ├── Sleep_Mode_Result.mp4
│   ├── Stop_Mode_Result.mp4
│   └── Standby_Mode_Result.mp4
└── README.md
```

## 核心代码

### 1. 修改系统主频到 72 MHz

```c
#include "stm32f10x.h"                                                                  // 引入 STM32F10x 标准外设库主头文件，提供 RCC 和 FLASH 配置接口

void MyRCC_SetSysClockTo72MHz(void)                                                      // 定义系统时钟配置函数，把系统主频配置为常见的 72 MHz
{                                                                                       // 函数体开始，下面按“复位 RCC → 开 HSE → 配总线 → 配 PLL → 切系统时钟”的顺序执行
    ErrorStatus HSEStartUpStatus;                                                       // 定义 HSE 起振状态变量，用于判断外部高速晶振是否启动成功

    RCC_DeInit();                                                                        // 将 RCC 配置恢复到复位状态，避免旧时钟配置影响本次主频设置
    RCC_HSEConfig(RCC_HSE_ON);                                                          // 打开 HSE 外部高速晶振，常见频率为 8 MHz
    HSEStartUpStatus = RCC_WaitForHSEStartUp();                                          // 等待 HSE 起振完成，并保存启动结果

    if (HSEStartUpStatus == SUCCESS)                                                     // 判断 HSE 是否成功起振，只有成功后才继续使用 HSE 作为 PLL 输入
    {                                                                                   // HSE 正常分支开始，下面配置 Flash 等待周期和各级总线分频
        FLASH_PrefetchBufferCmd(FLASH_PrefetchBuffer_Enable);                           // 使能 Flash 预取缓冲，提高高主频下的取指效率
        FLASH_SetLatency(FLASH_Latency_2);                                               // 设置 Flash 等待周期为 2，适配 72 MHz 主频访问 Flash 的时序要求

        RCC_HCLKConfig(RCC_SYSCLK_Div1);                                                 // 设置 AHB 时钟 HCLK 等于 SYSCLK，使内核和大部分总线运行在 72 MHz
        RCC_PCLK2Config(RCC_HCLK_Div1);                                                  // 设置 APB2 时钟 PCLK2 等于 HCLK，GPIO、USART1、ADC 等外设挂载在 APB2
        RCC_PCLK1Config(RCC_HCLK_Div2);                                                  // 设置 APB1 时钟 PCLK1 为 HCLK 的 1/2，保证 APB1 不超过 36 MHz

        RCC_PLLConfig(RCC_PLLSource_HSE_Div1, RCC_PLLMul_9);                             // 选择 HSE 作为 PLL 输入，并把 8 MHz 倍频 9 倍得到 72 MHz
        RCC_PLLCmd(ENABLE);                                                             // 使能 PLL，开始产生倍频后的高速时钟
        while (RCC_GetFlagStatus(RCC_FLAG_PLLRDY) == RESET)                              // 循环等待 PLL 稳定，PLL 未就绪前不能切换为系统时钟
        {                                                                               // 等待循环开始，此处不做其他任务，确保时钟切换前 PLL 已稳定
        }                                                                               // 等待循环结束，退出时说明 PLL 已经就绪

        RCC_SYSCLKConfig(RCC_SYSCLKSource_PLLCLK);                                      // 将系统时钟 SYSCLK 切换到 PLL 输出，即 72 MHz
        while (RCC_GetSYSCLKSource() != 0x08)                                            // 等待系统时钟源状态变为 PLL，0x08 表示 PLL 已作为 SYSCLK
        {                                                                               // 等待循环开始，防止后续代码在切换未完成时运行
        }                                                                               // 等待循环结束，系统主频已经切换完成
    }                                                                                   // HSE 正常分支结束
}                                                                                       // 系统时钟配置函数结束
```

### 2. 进入睡眠模式

```c
#include "stm32f10x.h"                                                                  // 引入 STM32F10x 标准外设库主头文件，提供 NVIC、EXTI 等接口

void MyPWR_EnterSleepMode(void)                                                          // 定义进入睡眠模式函数，适合短时间空闲省电
{                                                                                       // 函数体开始，下面通过 Cortex-M 内核指令进入 Sleep
    __WFI();                                                                            // 执行 Wait For Interrupt 指令，CPU 停止运行，任意已使能中断到来后唤醒
}                                                                                       // 睡眠模式函数结束，唤醒后会从 __WFI 后面继续执行
```

### 3. 进入停止模式并恢复主频

```c
#include "stm32f10x.h"                                                                  // 引入 STM32F10x 标准外设库主头文件，提供 PWR 和 RCC 外设接口

void MyRCC_SetSysClockTo72MHz(void);                                                     // 声明系统时钟恢复函数，Stop 唤醒后需要重新把主频切回 72 MHz

void MyPWR_EnterStopMode(void)                                                           // 定义进入停止模式函数，适合中等时长空闲并希望唤醒后继续执行
{                                                                                       // 函数体开始，下面先打开 PWR，再进入 Stop，醒来后恢复系统时钟
    RCC_APB1PeriphClockCmd(RCC_APB1Periph_PWR, ENABLE);                                  // 开启 PWR 外设时钟，进入 Stop 模式需要通过 PWR 控制
    PWR_EnterSTOPMode(PWR_Regulator_ON, PWR_STOPEntry_WFI);                              // 进入 Stop 模式，主调压器保持开启，用 WFI 等待中断唤醒
    MyRCC_SetSysClockTo72MHz();                                                          // Stop 唤醒后 HSE 和 PLL 通常被关闭，需要重新配置系统主频
}                                                                                       // 停止模式函数结束，程序从 Stop 唤醒后会继续向下运行
```

### 4. 进入待机模式

```c
#include "stm32f10x.h"                                                                  // 引入 STM32F10x 标准外设库主头文件，提供 PWR 外设接口

void MyPWR_EnterStandbyMode(void)                                                        // 定义进入待机模式函数，适合长时间休眠并追求最低功耗
{                                                                                       // 函数体开始，下面配置 WKUP 唤醒脚并进入 Standby
    RCC_APB1PeriphClockCmd(RCC_APB1Periph_PWR, ENABLE);                                  // 开启 PWR 外设时钟，待机模式控制由 PWR 外设完成
    PWR_WakeUpPinCmd(ENABLE);                                                           // 使能 WKUP 引脚唤醒功能，通常 PA0 上升沿可以唤醒芯片
    PWR_ClearFlag(PWR_FLAG_WU);                                                          // 清除唤醒标志，避免历史唤醒事件影响本次进入待机
    PWR_ClearFlag(PWR_FLAG_SB);                                                          // 清除待机标志，便于下次启动时准确判断是否从 Standby 唤醒
    PWR_EnterSTANDBYMode();                                                             // 进入 Standby 模式，唤醒后不会从这里继续，而是像复位一样重新启动
}                                                                                       // 待机模式函数形式上的结束，实际进入 Standby 后通常不会执行到这里
```

### 5. 主函数测试框架

```c
#include "stm32f10x.h"                                                                  // 引入 STM32F10x 标准外设库主头文件，提供芯片外设定义
#include "OLED.h"                                                                       // 引入 OLED 显示驱动，用于观察程序运行状态
#include "Delay.h"                                                                      // 引入延时函数，用于制造可观察的计数刷新节奏

void MyRCC_SetSysClockTo72MHz(void);                                                     // 声明主频配置函数，用于启动或 Stop 唤醒后恢复系统时钟
void MyPWR_EnterSleepMode(void);                                                         // 声明进入 Sleep 模式函数，用于短空闲低功耗测试
void MyPWR_EnterStopMode(void);                                                          // 声明进入 Stop 模式函数，用于中等深度低功耗测试
void MyPWR_EnterStandbyMode(void);                                                       // 声明进入 Standby 模式函数，用于最低功耗测试

int main(void)                                                                           // 主函数入口，芯片复位后从这里开始执行
{                                                                                       // 主函数体开始，下面先初始化时钟和显示模块
    uint32_t Count = 0;                                                                  // 定义运行计数变量，用于 OLED 显示程序是否仍在继续运行

    MyRCC_SetSysClockTo72MHz();                                                          // 配置系统主频为 72 MHz，作为正常运行状态
    OLED_Init();                                                                         // 初始化 OLED，方便显示当前模式和计数变化

    OLED_ShowString(1, 1, "PWR Test");                                                    // 在 OLED 第 1 行显示实验名称，提示当前是 PWR 低功耗测试
    OLED_ShowString(2, 1, "Run:");                                                        // 在 OLED 第 2 行显示运行计数标签，便于观察唤醒后是否继续

    while (1)                                                                            // 进入主循环，单片机应用通常长期停留在循环中执行任务
    {                                                                                   // 主循环体开始，下面不断刷新计数并可插入不同低功耗模式测试
        OLED_ShowNum(2, 5, Count, 6);                                                     // 显示当前计数值，若程序继续运行则数字会不断增加
        Count++;                                                                         // 计数自增，用于区分“从原位置继续”和“复位后重新开始”
        Delay_ms(500);                                                                   // 延时 500 ms，让 OLED 变化节奏肉眼可观察

        /* MyPWR_EnterSleepMode(); */                                                     // 取消注释后测试 Sleep，唤醒后通常从该语句后继续执行
        /* MyPWR_EnterStopMode(); */                                                      // 取消注释后测试 Stop，唤醒后继续执行，但需要恢复主频
        /* MyPWR_EnterStandbyMode(); */                                                   // 取消注释后测试 Standby，唤醒后程序会重新从 main 开始
    }                                                                                   // 主循环体结束，程序回到 while 起点继续执行
}                                                                                       // 主函数结束，实际嵌入式程序一般不会运行到这里
```

## 代码要点

| 行/段 | 说明 |
|---|---|
| `RCC_DeInit()` | 修改主频前先把 RCC 恢复到默认状态，避免旧配置残留影响 PLL 和分频设置。 |
| `RCC_HSEConfig(RCC_HSE_ON)` | 打开外部高速晶振 HSE，江协课程常用 8 MHz HSE 作为 PLL 输入。 |
| `FLASH_SetLatency(FLASH_Latency_2)` | 高主频下访问 Flash 需要等待周期，72 MHz 通常配置 2 个等待周期。 |
| `RCC_HCLKConfig(RCC_SYSCLK_Div1)` | 设置 AHB 总线时钟，通常让 HCLK 等于 SYSCLK。 |
| `RCC_PCLK1Config(RCC_HCLK_Div2)` | APB1 最大通常为 36 MHz，72 MHz 主频下必须 2 分频。 |
| `RCC_PLLConfig(RCC_PLLSource_HSE_Div1, RCC_PLLMul_9)` | 使用 8 MHz HSE 经过 PLL 倍频 9 倍得到 72 MHz 系统时钟。 |
| `__WFI()` | Cortex-M 内核等待中断指令，可让芯片进入 Sleep，收到中断后继续执行。 |
| `PWR_EnterSTOPMode()` | 进入 Stop 模式，CPU、HSE、PLL 停止，唤醒后程序继续执行但系统时钟需要恢复。 |
| `PWR_EnterSTANDBYMode()` | 进入 Standby 模式，功耗最低，SRAM 和大部分寄存器丢失，唤醒后等价于复位。 |
| `PWR_WakeUpPinCmd(ENABLE)` | 使能 WKUP 引脚唤醒 Standby，常用 PA0 上升沿唤醒。 |
| `PWR_ClearFlag(PWR_FLAG_WU)` | 清除唤醒标志，防止旧标志影响当前待机流程。 |
| `Count` 显示变量 | 用于判断唤醒方式：Sleep/Stop 唤醒后变量通常继续变化，Standby 唤醒后变量重新初始化。 |

## 关键知识点

### 1. STM32 主频修改流程

#### 原理

STM32 的系统时钟 SYSCLK 可以来自 HSI、HSE 或 PLL。常见的 72 MHz 主频通常来自 8 MHz HSE 经过 PLL 9 倍频。

时钟修改不是简单改一个数字，而是一个有顺序的过程：

```text
复位 RCC → 打开 HSE → 等待 HSE 就绪 → 配置 Flash 等待周期 → 配置 AHB/APB 分频 → 配置 PLL → 等待 PLL 就绪 → 切换 SYSCLK
```

#### 特点

- 主频越高，执行速度通常越快，但功耗也会上升。
- APB1 总线频率不能随意拉满，72 MHz 主频下常设为 36 MHz。
- Flash 等待周期必须和主频匹配，否则可能运行异常。
- 修改系统主频会影响延时函数、串口波特率、定时器频率等依赖时钟的模块。

#### 面试易问

**Q：STM32F103 为什么常配置成 72 MHz？**

A：因为 STM32F103 常用 8 MHz HSE，经过 PLL 9 倍频后得到 72 MHz，这是该系列常见最高主频配置，性能较高且标准库默认工程也常按这个频率设计。

**Q：修改主频后为什么串口波特率或 Delay 可能不准？**

A：这些模块的计算都依赖系统时钟或外设总线时钟。如果主频改变后没有同步更新 `SystemCoreClock`、外设分频或初始化参数，实际时序就会和预期不一致。

#### 易错点

- 忘记等待 HSE 或 PLL 就绪就切换系统时钟。
- APB1 没有分频，导致超过外设允许频率。
- Flash 等待周期配置过低，高主频下程序跑飞。
- 修改主频后没有重新初始化定时器、串口等依赖时钟的外设。

---

### 2. Sleep 睡眠模式

#### 原理

Sleep 是最浅的低功耗模式。进入 Sleep 后，CPU 内核停止执行指令，但外设时钟一般仍可继续运行。只要有已使能的中断到来，CPU 就会被唤醒，并从 `__WFI()` 后面的代码继续执行。

它适合“短时间没事做，但外设还要工作”的场景，例如等待串口接收、等待定时器中断、等待按键中断。

#### 特点

- 唤醒速度快。
- 程序现场保持，唤醒后继续执行。
- 外设通常继续运行，功耗降低有限。
- 任意已使能中断都可能唤醒 CPU。

#### 面试易问

**Q：Sleep 模式和普通延时等待有什么区别？**

A：普通延时期间 CPU 仍在执行循环，功耗没有明显降低；Sleep 通过 `WFI` 停止 CPU 内核，等待中断唤醒，空闲期间功耗更低。

**Q：Sleep 模式唤醒后程序从哪里执行？**

A：从进入 Sleep 的 `__WFI()` 指令后面继续执行，不会重新从 `main()` 开始。

#### 易错点

- SysTick 中断没有关闭，芯片可能刚进入 Sleep 就被周期性唤醒。
- 没有配置有效中断源，程序进入 Sleep 后无法按预期唤醒。
- 误以为 Sleep 会关闭所有外设，实际很多外设仍在运行。
- 用 OLED 刷新现象判断 Sleep 时，要注意中断是否频繁打断睡眠。

---

### 3. Stop 停止模式

#### 原理

Stop 模式比 Sleep 更深。进入 Stop 后，CPU 停止，HSE 和 PLL 通常关闭，内部调压器可选择保持开启或进入低功耗状态，SRAM 和寄存器内容仍保留。

Stop 被中断或事件唤醒后，程序会从进入 Stop 的语句后继续执行。但是系统时钟通常会回到 HSI，因此如果工程原本依赖 72 MHz HSE+PLL，唤醒后必须重新配置系统时钟。

#### 特点

- 功耗明显低于 Sleep。
- SRAM 和寄存器内容保持。
- 唤醒后不是复位，而是继续执行。
- 唤醒后需要恢复系统时钟，尤其是 HSE、PLL 和总线分频。

#### 面试易问

**Q：Stop 模式唤醒后为什么要重新配置系统时钟？**

A：进入 Stop 后 HSE 和 PLL 会关闭，唤醒后系统通常先使用 HSI 运行。如果不重新配置到原来的 72 MHz，延时、串口、定时器等时序都会变化。

**Q：Stop 和 Sleep 最大区别是什么？**

A：Sleep 主要停 CPU，外设时钟多保持；Stop 会关闭更多时钟资源，功耗更低，但唤醒后需要恢复系统时钟。

#### 易错点

- Stop 唤醒后没有恢复 PLL，导致程序“能跑但时序全乱”。
- 没有配置外部中断或 RTC 等唤醒源，进入 Stop 后无法唤醒。
- 把 Stop 当作复位处理，实际上变量和 SRAM 通常仍保持。
- 在 Stop 前没有处理好外设状态，唤醒后外设通信异常。

---

### 4. Standby 待机模式

#### 原理

Standby 是 STM32F1 中最深的常用低功耗模式。进入 Standby 后，1.8V 内核区域断电，CPU 停止，SRAM 和大部分寄存器内容丢失，只保留备份域相关内容。

Standby 唤醒后不会从进入待机的位置继续执行，而是像复位一样重新启动程序。程序可以通过待机标志判断本次启动是否来自 Standby 唤醒。

#### 特点

- 功耗最低。
- 唤醒后等价于复位，程序从启动流程重新开始。
- SRAM 和普通外设寄存器内容丢失。
- BKP 和 RTC 在 VBAT 或备份域供电存在时可保持。
- 常用 WKUP 引脚、RTC 闹钟、NRST、IWDG 等方式唤醒。

#### 面试易问

**Q：Standby 和 Stop 最大区别是什么？**

A：Stop 唤醒后程序从原位置继续执行，SRAM 内容保持；Standby 唤醒后相当于复位，程序重新启动，SRAM 和大部分寄存器内容丢失，但功耗更低。

**Q：如何判断芯片是否从 Standby 模式唤醒？**

A：启动后读取 `PWR_FLAG_SB` 待机标志。如果该标志被置位，说明之前进入过 Standby 并从待机状态唤醒；读取后通常要清除该标志。

#### 易错点

- 期望 Standby 唤醒后继续执行原来的下一行代码，这是错误的。
- 忘记使能 WKUP 引脚，导致 PA0 无法唤醒待机状态。
- 没有清除 WU 或 SB 标志，影响后续判断。
- 进入 Standby 前没有把必要状态保存到 BKP、RTC 或 Flash。

---

### 5. 低功耗模式选型

#### 原理

低功耗模式本质是在“功耗、唤醒速度、现场保持、外设运行能力”之间做取舍。越深的低功耗模式，功耗越低，但恢复成本越高。

#### 特点

- Sleep 适合短等待，唤醒最快。
- Stop 适合较长空闲，能保持 SRAM 和程序现场。
- Standby 适合长期休眠，功耗最低但唤醒相当于重启。
- 工程中常结合 RTC、EXTI、低功耗定时器或外部按键设计唤醒策略。

#### 面试易问

**Q：低功耗设计时应该怎么选 Sleep、Stop、Standby？**

A：如果只是短时间等待中断，用 Sleep；如果需要较低功耗但还想保留 RAM 和执行现场，用 Stop；如果设备长时间休眠且可以重新启动，用 Standby。

**Q：进入低功耗前一般要做哪些准备？**

A：要关闭不需要的外设和时钟，配置可靠唤醒源，保存必要状态，处理通信外设的收发状态，并确认唤醒后是否需要重新初始化时钟和外设。

#### 易错点

- 只调用低功耗函数，不关外设，实际电流下降不明显。
- 唤醒源设计不可靠，设备进入低功耗后回不来。
- Stop 唤醒后忘记恢复时钟，导致后续外设异常。
- Standby 前没保存关键状态，唤醒后业务流程丢失。

---

## 本节核心记忆

```text
主频修改：HSE → PLL → SYSCLK，别忘 Flash 等待周期和 APB1 分频
```

```text
Sleep：停 CPU，外设多保持，中断唤醒后继续执行
```

```text
Stop：停更多时钟，RAM 保持，中断唤醒后继续执行，但要恢复系统时钟
```

```text
Standby：功耗最低，RAM 丢失，唤醒等价于复位，关键状态要提前保存
```

## 开发过程总结

### 问题 1：修改主频后串口、定时器或 Delay 不准

现象：

- 程序能运行，但串口波特率异常。
- 定时器中断频率不对。
- `Delay_ms()` 的实际延时时间变长或变短。

排查过程：

1. 检查系统时钟是否真的切换到了目标频率。
2. 检查 `SystemCoreClock` 是否同步更新。
3. 检查 APB1、APB2 分频是否改变。
4. 检查串口、定时器初始化是否仍按旧主频计算。

解决方案：

- 修改主频后重新确认 `SystemCoreClock`。
- 重新初始化依赖时钟的外设。
- APB1 外设尤其要注意 36 MHz 上限和定时器倍频规则。

### 问题 2：进入 Sleep 后立刻醒来

现象：

- 调用 `__WFI()` 后电流几乎没有下降。
- 程序看起来没有真正停住。
- OLED 计数仍然快速刷新。

排查过程：

1. 检查 SysTick 是否持续产生中断。
2. 检查是否有串口、定时器、EXTI 等中断源处于挂起状态。
3. 检查 NVIC 中是否开启了不需要的中断。

解决方案：

- 进入 Sleep 前关闭或暂停不需要的周期中断。
- 清除 EXTI 等外设的中断挂起标志。
- 保留真正需要的唤醒中断即可。

### 问题 3：Stop 唤醒后程序异常或通信错误

现象：

- 按键或外部中断可以唤醒芯片。
- OLED 或串口显示异常。
- 延时函数明显变慢，定时器频率不对。

排查过程：

1. 检查 Stop 唤醒后是否重新配置 HSE 和 PLL。
2. 检查系统时钟源是否仍停留在 HSI。
3. 检查外设是否需要重新初始化。

解决方案：

- 在 `PWR_EnterSTOPMode()` 返回后立刻调用系统时钟恢复函数。
- 必要时重新初始化串口、定时器、OLED 等依赖时钟的外设。
- 用计数显示确认 Stop 唤醒后程序是否从原位置继续执行。

### 问题 4：Standby 唤醒后变量全部归零

现象：

- PA0 WKUP 可以唤醒芯片。
- 程序从开机界面重新运行。
- 全局变量、计数值等运行状态全部丢失。

排查过程：

1. 确认当前进入的是 Standby，而不是 Stop。
2. 检查是否理解 Standby 唤醒等价于复位。
3. 检查关键状态是否保存到了 BKP、RTC 或 Flash。

解决方案：

- 把 Standby 当作“低功耗关机”来设计，而不是普通暂停。
- 进入 Standby 前保存必要状态。
- 启动后读取 `PWR_FLAG_SB` 和保存的数据，恢复业务状态。

## 结果展示

> 实验 1：修改系统主频后，程序仍可正常运行，但依赖时钟的延时、串口、定时器需要按新频率重新确认。✅

> 实验 2：进入 Sleep 模式后，CPU 暂停执行；中断到来后，程序从 `__WFI()` 后继续运行。✅

> 实验 3：进入 Stop 模式后，功耗进一步降低；外部中断唤醒后程序继续执行，但需要恢复 HSE 和 PLL 主频配置。✅

> 实验 4：进入 Standby 模式后，芯片进入最低功耗状态；通过 WKUP 或复位唤醒后，程序从启动流程重新开始。✅

## 本节小结

本节的价值不只是会调用几个 PWR 函数，而是建立“时钟、功耗、唤醒代价”之间的工程直觉。

面试复习时建议重点记住三句话：

```text
Sleep 是暂停 CPU，Stop 是暂停更多时钟，Standby 是接近重新开机。
```

```text
Stop 唤醒后还能接着跑，但系统时钟通常要重新配。
```

```text
Standby 唤醒后从 main 重新开始，所以关键状态要提前保存。
```

后续做低功耗项目时，真正拉开差距的不是“会不会进低功耗”，而是能不能把外设关闭、唤醒源、状态保存、唤醒恢复这些细节设计完整。这一节正好是从裸机实验走向工程化低功耗设计的入口。

## 参考资料

- [Bilibili：STM32入门教程-2023版，第 45 分 P](https://www.bilibili.com/video/BV1th411z7sn?p=45)
- [Bilibili 视频信息接口：BV1th411z7sn](https://api.bilibili.com/x/web-interface/view?bvid=BV1th411z7sn)
- [STMicroelectronics：STM32F10xxx Reference Manual RM0008](https://www.st.com/resource/en/reference_manual/cd00171190.pdf)
- [STMicroelectronics：AN2629 STM32F10xxx low-power modes](https://www.st.com/resource/en/application_note/cd00171691.pdf)

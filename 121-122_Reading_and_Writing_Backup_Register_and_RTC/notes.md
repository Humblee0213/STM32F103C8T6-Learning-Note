# 0123 — BKP 与 RTC：读写备份寄存器与实时时钟

> 视频来源：[江协科技 STM32 入门教程 - [12-3] 读写备份寄存器&实时时钟](https://www.bilibili.com/video/BV1th411z7sn?p=43)
>
> 说明：公开视频信息接口可确认本节标题、分 P 和时长，但未提供完整字幕列表。因此本文档基于可访问的视频信息、STM32F103 标准外设库用法、课程上下文和典型实验流程整理，核心代码为参考实现。

## 实验概述

本实验基于 STM32F103C8T6 标准外设库，围绕备份域完成两个核心功能：

1. 使用 BKP (Backup Registers) 备份寄存器保存少量掉电保持数据。
2. 使用 RTC (Real Time Clock) 实时时钟计数器记录 Unix 时间戳，并通过 `time.h` 转换成年月日时分秒，在 OLED 上实时显示。

本节的核心目标是理解：

```text
备份域访问权限 → BKP 数据读写 → RTC 时钟源选择 → RTC 秒计数 → 时间格式转换
```

BKP 和 RTC 的共同特点是都位于备份域。只要 VBAT 供电保持，备份寄存器和 RTC 计数器就可以在主电源断开后继续保存或运行，这也是它们在掉电保存、时间保持、低功耗系统中的价值。

## 硬件连接

本节主要使用 STM32F103C8T6 板载资源和 OLED 显示模块。BKP 与 RTC 本身属于芯片内部外设，不需要像 LED、按键那样额外占用普通 GPIO。

| 器件 | 对应引脚 | 说明 |
|---|---:|---|
| STM32F103C8T6 | 内部 BKP / RTC | BKP 和 RTC 位于备份域，需要开启 PWR、BKP 时钟并使能备份域写访问 |
| 32.768 kHz 晶振 | PC14 / PC15 | LSE (Low Speed External) 外部低速时钟，常作为 RTC 时钟源 |
| OLED 显示屏 | PB8 / PB9 | 课程常用软件 I2C OLED 接法，具体以自己的工程 OLED 驱动为准 |
| VBAT 后备电源 | VBAT / GND | 若要验证掉电保持，需要接纽扣电池或后备电源 |
| ST-Link V2 | PA13、PA14、GND、3.3V | SWD 下载与调试 |

> 注意：如果只用 USB 或 3.3V 主电源供电，不接 VBAT，那么完全断电后 RTC 和 BKP 通常不能保持；如果只是按 Reset 键复位，备份域不会被清空。

## 工程文件结构

建议本节工程按下面方式组织：

```text
03-BKP-RTC-ReadWrite
├── Code
│   └── main.c
├── Hardware
│   └── .gitkeep
├── Results
│   └── .gitkeep
└── README.md
```

## 核心代码

### 1. 备份寄存器读写

```c
#include "stm32f10x.h"                                      // 引入 STM32F10x 标准外设库主头文件，提供 RCC、PWR、BKP 等外设接口
#include "OLED.h"                                           // 引入课程 OLED 显示驱动，用于把读出的备份寄存器值显示出来
                                                            // 空行用于分隔头文件和全局变量，提升代码可读性
uint16_t BKP_Data = 0;                                      // 定义全局变量，保存从 BKP_DR2 读出的 16 位备份数据
                                                            // 空行用于分隔全局变量和函数定义，便于定位代码结构
static void BackupRegister_Demo(void)                       // 定义备份寄存器演示函数，static 表示该函数只在当前文件内部使用
{                                                           // 函数体开始，下面依次完成备份域访问准备、写入和读出
    RCC_APB1PeriphClockCmd(RCC_APB1Periph_PWR, ENABLE);     // 开启 PWR 电源控制外设时钟，否则无法配置备份域写访问权限
    RCC_APB1PeriphClockCmd(RCC_APB1Periph_BKP, ENABLE);     // 开启 BKP 备份寄存器外设时钟，否则无法读写备份寄存器
    PWR_BackupAccessCmd(ENABLE);                            // 允许访问备份域写权限，这是写 BKP 和配置 RTC 前必须执行的一步
                                                            // 空行用于分隔初始化动作和实际读写动作
    BKP_WriteBackupRegister(BKP_DR2, 0x1234);                // 向第 2 个备份数据寄存器写入测试值 0x1234，避免占用 RTC 初始化标志
    BKP_Data = BKP_ReadBackupRegister(BKP_DR2);              // 从第 2 个备份数据寄存器读回数据，用于验证写入是否成功
}                                                           // 备份寄存器演示函数结束
```

### 2. RTC 初始化

```c
#include "stm32f10x.h"                                      // 引入标准外设库头文件，提供 RTC、RCC、BKP、PWR 等函数声明
#include <time.h>                                           // 引入 C 标准时间库，用于时间戳和年月日时分秒之间的转换
                                                            // 空行用于分隔头文件和全局变量，避免代码挤在一起
uint16_t RTC_Time[] = {2023, 1, 1, 23, 59, 55};              // 定义默认时间数组，依次表示年、月、日、时、分、秒
                                                            // 空行用于分隔全局变量和函数声明，突出模块边界
static void MyRTC_SetTime(void);                            // 声明设置 RTC 时间的函数，初始化 RTC 时会调用它写入默认时间
                                                            // 空行用于分隔声明和函数定义，使阅读顺序更清楚
static void MyRTC_Init(void)                                // 定义 RTC 初始化函数，负责配置备份域、LSE 时钟源、预分频器和初始时间
{                                                           // 函数体开始，下面先打开外设时钟和备份域写权限
    RCC_APB1PeriphClockCmd(RCC_APB1Periph_PWR, ENABLE);     // 开启 PWR 外设时钟，后续需要通过 PWR 打开备份域写访问
    RCC_APB1PeriphClockCmd(RCC_APB1Periph_BKP, ENABLE);     // 开启 BKP 外设时钟，后续要读取 BKP_DR1 判断 RTC 是否已初始化
    PWR_BackupAccessCmd(ENABLE);                            // 允许写入备份域，否则 BKP、RTC 相关配置会被硬件保护
                                                            // 空行用于分隔权限配置和初始化状态判断
    if (BKP_ReadBackupRegister(BKP_DR1) != 0xA5A5)           // 读取 BKP_DR1 标志位，如果不是 0xA5A5，说明 RTC 还没有按本程序初始化过
    {                                                       // 首次初始化分支开始，需要完整配置备份域和 RTC
        BKP_DeInit();                                       // 复位备份域，把 BKP 和 RTC 相关配置恢复到默认状态，避免历史配置干扰
                                                            // 空行用于分隔备份域复位和低速时钟配置
        RCC_LSEConfig(RCC_LSE_ON);                          // 打开 LSE 低速外部晶振，通常频率为 32.768 kHz，适合作为 RTC 时钟源
        while (RCC_GetFlagStatus(RCC_FLAG_LSERDY) == RESET); // 等待 LSE 稳定起振，未就绪前不能可靠地提供 RTC 时钟
                                                            // 空行用于分隔 LSE 就绪等待和 RTC 时钟源选择
        RCC_RTCCLKConfig(RCC_RTCCLKSource_LSE);              // 选择 LSE 作为 RTC 时钟源，保证 RTC 按 32.768 kHz 基准计数
        RCC_RTCCLKCmd(ENABLE);                              // 使能 RTC 时钟，让 RTC 外设真正获得时钟输入
                                                            // 空行用于分隔时钟配置和 RTC 寄存器同步
        RTC_WaitForSynchro();                               // 等待 RTC 寄存器与 APB1 总线同步，确保后续读写值有效
        RTC_WaitForLastTask();                              // 等待上一次 RTC 写操作完成，避免连续写入造成配置丢失
                                                            // 空行用于分隔同步等待和预分频配置
        RTC_SetPrescaler(32768 - 1);                        // 设置 RTC 预分频器，使 32768 Hz 的 LSE 分频后得到 1 Hz 秒脉冲
        RTC_WaitForLastTask();                              // 等待预分频器写入完成，确保 RTC 按新的 1 秒节拍计数
                                                            // 空行用于分隔预分频配置和初始时间写入
        MyRTC_SetTime();                                    // 将默认年月日时分秒转换为时间戳，并写入 RTC 计数器
                                                            // 空行用于分隔 RTC 初始化动作和初始化完成标志
        BKP_WriteBackupRegister(BKP_DR1, 0xA5A5);            // 在备份寄存器写入初始化标志，后续复位时可跳过重复初始化
    }                                                       // 首次初始化分支结束
    else                                                    // 如果 BKP_DR1 已经保存初始化标志，说明备份域中已有有效 RTC 配置
    {                                                       // 非首次初始化分支开始，不重置 RTC，避免每次复位都把时间改回默认值
        RTC_WaitForSynchro();                               // 复位后重新等待 RTC 与 APB1 同步，保证读计数器时数据可靠
        RTC_WaitForLastTask();                              // 等待可能尚未完成的 RTC 操作结束，保持访问顺序安全
    }                                                       // 非首次初始化分支结束
}                                                           // RTC 初始化函数结束
```

### 3. RTC 时间写入与读取

```c
static void MyRTC_SetTime(void)                             // 定义设置 RTC 时间函数，把 RTC_Time 数组中的日历时间写入 RTC 计数器
{                                                           // 函数体开始，下面先构造 C 标准库的 struct tm 时间结构体
    time_t Time_Count;                                      // 定义时间戳变量，用于保存 mktime 转换得到的秒计数
    struct tm Time_Date = {0};                              // 定义并清零日历时间结构体，避免 mktime 读取未初始化字段
                                                            // 空行用于分隔变量定义和字段赋值
    Time_Date.tm_year = RTC_Time[0] - 1900;                 // 设置年份字段，struct tm 要求年份从 1900 年开始计数
    Time_Date.tm_mon = RTC_Time[1] - 1;                     // 设置月份字段，struct tm 要求月份范围为 0 到 11
    Time_Date.tm_mday = RTC_Time[2];                        // 设置日期字段，表示一个月中的第几天，范围通常为 1 到 31
    Time_Date.tm_hour = RTC_Time[3];                        // 设置小时字段，采用 24 小时制，范围通常为 0 到 23
    Time_Date.tm_min = RTC_Time[4];                         // 设置分钟字段，范围通常为 0 到 59
    Time_Date.tm_sec = RTC_Time[5];                         // 设置秒字段，范围通常为 0 到 59
                                                            // 空行用于分隔日历字段填写和时间戳转换
    Time_Count = mktime(&Time_Date);                        // 调用 mktime 把日历时间转换为 Unix 时间戳秒数
                                                            // 空行用于分隔时间戳转换和 RTC 写入
    RTC_SetCounter(Time_Count);                             // 把时间戳写入 RTC 计数器，此后 RTC 每秒自动加 1
    RTC_WaitForLastTask();                                  // 等待 RTC 计数器写入完成，防止立刻读取得到旧值
}                                                           // RTC 时间写入函数结束
                                                            // 空行用于分隔设置函数和读取函数
static void MyRTC_ReadTime(void)                            // 定义读取 RTC 时间函数，把 RTC 计数器秒数转换回年月日时分秒
{                                                           // 函数体开始，下面先准备时间戳变量和日历结构体指针
    time_t Time_Count;                                      // 定义时间戳变量，用于保存 RTC 当前秒计数
    struct tm *Time_Date;                                   // 定义日历时间结构体指针，localtime 会返回转换后的时间字段地址
                                                            // 空行用于分隔变量定义和 RTC 计数器读取
    Time_Count = RTC_GetCounter();                          // 读取 RTC 当前计数器值，该值可以理解为从 Unix 起点累计的秒数
    Time_Date = localtime(&Time_Count);                     // 调用 localtime 把秒计数转换为年月日时分秒结构体
                                                            // 空行用于分隔时间转换和数组回填
    RTC_Time[0] = Time_Date->tm_year + 1900;                // 把 struct tm 年份字段还原为真实年份，例如 2023
    RTC_Time[1] = Time_Date->tm_mon + 1;                    // 把 struct tm 月份字段还原为 1 到 12 的正常月份
    RTC_Time[2] = Time_Date->tm_mday;                       // 读取日期字段，表示当前是本月第几天
    RTC_Time[3] = Time_Date->tm_hour;                       // 读取小时字段，用于 OLED 显示当前小时
    RTC_Time[4] = Time_Date->tm_min;                        // 读取分钟字段，用于 OLED 显示当前分钟
    RTC_Time[5] = Time_Date->tm_sec;                        // 读取秒字段，用于 OLED 显示当前秒
}                                                           // RTC 时间读取函数结束
```

### 4. OLED 显示主循环

```c
int main(void)                                              // 主函数入口，单片机复位后从这里开始执行用户程序
{                                                           // 主函数体开始，下面先初始化显示和备份域相关功能
    OLED_Init();                                            // 初始化 OLED 显示屏，方便观察 BKP 数据和 RTC 时间
    MyRTC_Init();                                           // 初始化 RTC，如果已初始化过则不会重复设置默认时间
    BackupRegister_Demo();                                  // 执行备份寄存器读写测试，把读回值保存到 BKP_Data，避免被首次初始化时的 BKP_DeInit 清掉
                                                            // 空行用于分隔初始化流程和静态显示内容
    OLED_ShowString(1, 1, "BKP:");                           // 在 OLED 第 1 行显示 BKP 标签，提示后面是备份寄存器读回值
    OLED_ShowHexNum(1, 5, BKP_Data, 4);                      // 在 OLED 第 1 行显示 4 位十六进制数据，例如 1234
                                                            // 空行用于分隔固定显示和循环刷新
    while (1)                                               // 进入主循环，单片机程序通常不会主动退出 main
    {                                                       // 主循环体开始，下面持续读取并刷新 RTC 当前时间
        MyRTC_ReadTime();                                   // 从 RTC 计数器读取秒数，并转换为年月日时分秒
                                                            // 空行用于分隔读取动作和日期显示
        OLED_ShowNum(2, 1, RTC_Time[0], 4);                  // 在第 2 行第 1 列显示年份，占 4 位
        OLED_ShowString(2, 5, "-");                          // 在年份和月份之间显示短横线，形成 yyyy-mm-dd 格式
        OLED_ShowNum(2, 6, RTC_Time[1], 2);                  // 在第 2 行第 6 列显示月份，占 2 位
        OLED_ShowString(2, 8, "-");                          // 在月份和日期之间显示短横线，提升可读性
        OLED_ShowNum(2, 9, RTC_Time[2], 2);                  // 在第 2 行第 9 列显示日期，占 2 位
                                                            // 空行用于分隔日期显示和时间显示
        OLED_ShowNum(3, 1, RTC_Time[3], 2);                  // 在第 3 行第 1 列显示小时，占 2 位
        OLED_ShowString(3, 3, ":");                          // 在小时和分钟之间显示冒号，形成 hh:mm:ss 格式
        OLED_ShowNum(3, 4, RTC_Time[4], 2);                  // 在第 3 行第 4 列显示分钟，占 2 位
        OLED_ShowString(3, 6, ":");                          // 在分钟和秒之间显示冒号，提升时间显示辨识度
        OLED_ShowNum(3, 7, RTC_Time[5], 2);                  // 在第 3 行第 7 列显示秒，占 2 位，每秒变化一次
    }                                                       // 主循环体结束，程序会回到 while 起点继续刷新
}                                                           // 主函数结束，实际运行中通常不会执行到这里
```

## 代码要点

| 行/段 | 说明 |
|---|---|
| `RCC_APB1PeriphClockCmd(RCC_APB1Periph_PWR, ENABLE)` | 开启 PWR 电源控制外设时钟。备份域写保护由 PWR 控制，所以只开 BKP 时钟还不够。 |
| `RCC_APB1PeriphClockCmd(RCC_APB1Periph_BKP, ENABLE)` | 开启 BKP 外设时钟，允许程序访问备份寄存器相关接口。 |
| `PWR_BackupAccessCmd(ENABLE)` | 解除备份域写保护。写 BKP、配置 RTC 前必须执行，否则写操作不会生效。 |
| `BKP_WriteBackupRegister()` | 向指定备份寄存器写入 16 位数据，常用于保存校验标志、少量配置或 RTC 初始化标志。 |
| `BKP_ReadBackupRegister()` | 读取指定备份寄存器的 16 位数据，常用于判断掉电前保存的信息是否仍然有效。 |
| `BKP_DeInit()` | 复位备份域。它会清除 BKP 寄存器和 RTC 配置，只有首次初始化或需要重配 RTC 时才应该调用。 |
| `RCC_LSEConfig(RCC_LSE_ON)` | 打开 LSE 低速外部晶振，RTC 通常使用 32.768 kHz 晶振作为稳定时钟源。 |
| `RCC_RTCCLKConfig(RCC_RTCCLKSource_LSE)` | 选择 RTC 时钟源。RTC 时钟源一旦配置，通常需要复位备份域才能重新选择。 |
| `RTC_SetPrescaler(32768 - 1)` | 把 32.768 kHz 分频为 1 Hz，使 RTC 计数器每秒加 1。 |
| `RTC_WaitForSynchro()` | 等待 RTC 寄存器同步到 APB1 总线，复位后读取 RTC 前尤其重要。 |
| `RTC_WaitForLastTask()` | 等待 RTC 上一次写操作完成，连续配置 RTC 时不能省略。 |
| `mktime()` | 将年月日时分秒转换为 Unix 时间戳，便于写入 RTC 秒计数器。 |
| `localtime()` | 将 RTC 秒计数器转换回年月日时分秒，便于 OLED 显示。 |
| `struct tm Time_Date = {0}` | 使用前先清零时间结构体，避免未初始化字段影响 `mktime()` 的转换结果。 |
| `BKP_DR1 = 0xA5A5` | 用 `BKP_DR1` 保存“RTC 已初始化”标志，避免复位后反复把时间重置为默认值。 |
| `BKP_DR2 = 0x1234` | 用 `BKP_DR2` 做备份寄存器读写演示，避免覆盖 RTC 初始化标志。 |

## 关键知识点

### 1. BKP 备份寄存器

#### 原理

BKP (Backup Registers) 是 STM32 备份域中的一组 16 位寄存器。它们不适合保存大量数据，但适合保存少量关键状态，例如 RTC 是否已经初始化、掉电前的运行标志、少量用户配置等。

备份寄存器和普通 SRAM 最大的区别是：只要 VBAT 后备电源存在，主电源掉电后它仍然可以保留内容。

#### 特点

- 位于备份域，和 RTC、LSE 等资源联系紧密。
- STM32F103C8T6 常用备份寄存器为 `BKP_DR1` 到 `BKP_DR10`。
- 每个备份数据寄存器通常可保存 16 位数据。
- 写入前需要开启 PWR、BKP 时钟，并使能备份域写访问。
- 适合保存“状态标志”，不适合作为大容量存储。

#### 面试易问

**Q：BKP 备份寄存器和 Flash 保存参数有什么区别？**

A：BKP 备份寄存器容量很小，但读写快、无需擦除，并且在 VBAT 供电下可掉电保持；Flash 容量更大，但写入前通常要擦除，写入次数也有限，更适合保存较多的长期配置。

**Q：为什么写 BKP 前要调用 `PWR_BackupAccessCmd(ENABLE)`？**

A：备份域默认受写保护，防止程序误操作破坏 RTC 和备份数据。只有开启 PWR 时钟并解除备份域写保护后，写 BKP 和配置 RTC 才会生效。

#### 易错点

- 只开启 `BKP` 时钟，忘记开启 `PWR` 时钟。
- 忘记调用 `PWR_BackupAccessCmd(ENABLE)`，导致写入看似执行但实际无效。
- 误调用 `BKP_DeInit()`，把备份寄存器和 RTC 配置一起清掉。
- 把 BKP 当作大容量存储使用，设计上不合适。

---

### 2. RTC 秒计数器

#### 原理

STM32F1 的 RTC (Real Time Clock) 更接近一个独立秒计数器，而不是直接提供年月日时分秒寄存器的日历 RTC。

典型做法是让 RTC 每秒自增 1，把它当作 Unix 时间戳计数器使用。程序需要显示日期时，再把秒计数转换为年月日时分秒。

#### 特点

- RTC 位于备份域，可以配合 VBAT 在主电源掉电后继续运行。
- 常用 LSE 32.768 kHz 晶振作为时钟源，便于精确分频到 1 Hz。
- STM32F1 RTC 本身主要维护计数器，不自动帮你维护日历格式。
- 需要软件完成“时间戳”和“年月日时分秒”的相互转换。

#### 面试易问

**Q：STM32F1 的 RTC 和带日历功能的 RTC 有什么区别？**

A：STM32F1 的 RTC 主要是秒计数器，不直接提供年月日时分秒日历寄存器；很多后续系列或专用 RTC 芯片会带硬件日历寄存器，可以直接读取日期和时间字段。

**Q：为什么 RTC 常用 32.768 kHz 晶振？**

A：32768 等于 2 的 15 次方，很容易通过二进制分频得到 1 Hz 秒脉冲，功耗也较低，适合实时时钟长期运行。

#### 易错点

- 以为 STM32F1 RTC 能直接读出年、月、日、时、分、秒。
- 忘记设置预分频器，导致 RTC 计数速度不是 1 秒加 1。
- 复位后没有等待同步，读出的 RTC 值可能不可靠。
- 忽略 LSE 起振时间，刚打开 LSE 就立即配置 RTC。

---

### 3. 备份域初始化标志

#### 原理

RTC 初始化最容易踩的坑是“每次复位都重新设置时间”。如果程序一启动就无条件调用 `RTC_SetCounter()`，那么按一次 Reset 后时间会回到默认值，RTC 就失去了持续计时意义。

解决办法是使用 BKP 某个寄存器保存初始化标志：

```text
首次运行：BKP_DR1 != 0xA5A5 → 初始化 RTC → 写入 BKP_DR1 = 0xA5A5
再次复位：BKP_DR1 == 0xA5A5 → 跳过重新设时 → 只同步并读取 RTC
```

#### 特点

- 初始化标志通常选择一个不容易误出现的固定值，例如 `0xA5A5`。
- 该方法可以区分“首次上电初始化”和“普通复位继续运行”。
- 如果备份域被复位或 VBAT 掉电，标志会丢失，程序会重新初始化 RTC。
- 标志寄存器应避免和其他业务数据混用。

#### 面试易问

**Q：为什么 RTC 初始化时要读一个 BKP 标志位？**

A：为了判断 RTC 是否已经初始化过。若已经初始化，就不能每次复位都重设时间，否则 RTC 的持续计时功能会被破坏。

**Q：什么时候需要调用 `BKP_DeInit()`？**

A：首次初始化备份域、需要清除旧 RTC 配置、或需要重新选择 RTC 时钟源时可以调用。正常复位启动时不应该反复调用。

#### 易错点

- 把 `BKP_DeInit()` 放在无条件路径里，导致每次启动都清空 RTC。
- 初始化标志和业务数据使用同一个 BKP 寄存器，互相覆盖。
- 只判断标志，不处理备份电源掉电后的重新初始化。
- 修改 RTC 时钟源后忘记备份域需要复位才能重新配置。

---

### 4. `time.h` 时间转换

#### 原理

本节用 `time.h` 提供的 `mktime()` 和 `localtime()` 完成日历时间和 Unix 时间戳之间的转换。

- `mktime()`：年月日时分秒 → 秒计数。
- `localtime()`：秒计数 → 年月日时分秒。

这样做的好处是不用自己处理大小月、闰年、跨年等复杂日历规则。

#### 特点

- `struct tm` 中的 `tm_year` 不是实际年份，而是从 1900 年开始的偏移。
- `struct tm` 中的 `tm_mon` 不是 1 到 12，而是 0 到 11。
- `mktime()` 会根据字段自动归一化日期时间。
- 不同 C 库对时区处理可能存在差异，嵌入式工程中要保持工具链和显示逻辑一致。

#### 面试易问

**Q：为什么设置 2023 年时要写 `tm_year = 2023 - 1900`？**

A：因为 C 标准库的 `struct tm` 约定 `tm_year` 表示从 1900 年开始经过的年数，所以 2023 年要写成 123。

**Q：为什么设置 1 月时要写 `tm_mon = 1 - 1`？**

A：因为 `struct tm` 的月份字段从 0 开始，0 表示 1 月，11 表示 12 月。

#### 易错点

- 直接把 `2023` 赋给 `tm_year`，会得到错误年份。
- 直接把 `1` 赋给 `tm_mon`，实际表示 2 月。
- 没有注意 `mktime()` 和 `localtime()` 的时区假设，导致显示时间偏移。
- 没有初始化 `struct tm` 的相关字段，复杂场景下可能引入不可预期结果。

---

### 5. RTC 时钟源与预分频

#### 原理

RTC 的时钟源可以来自 LSE、LSI 或 HSE 分频。入门实验中通常选择 LSE，因为 32.768 kHz 晶振适合长期计时。

RTC 计数器想要每秒加 1，就要把输入时钟分频到 1 Hz：

```text
32768 Hz / 32768 = 1 Hz
```

标准外设库中预分频器写入的是“分频系数减 1”，所以代码常写：

```text
RTC_SetPrescaler(32768 - 1)
```

#### 特点

- LSE 精度较好，适合 RTC。
- LSI 是内部低速 RC，成本低但误差通常更大。
- HSE 分频依赖外部高速晶振，低功耗保持时不如 LSE 常用。
- 预分频配置错误会直接导致 RTC 走时速度错误。

#### 面试易问

**Q：RTC 使用 LSE 和 LSI 有什么区别？**

A：LSE 是外部 32.768 kHz 晶振，精度通常更好，适合长期计时；LSI 是内部低速 RC，使用方便但精度较差，更常用于看门狗等对精确时间要求不高的场景。

**Q：为什么预分频器设置为 `32768 - 1` 而不是 `32768`？**

A：硬件预分频器寄存器一般保存的是分频计数的重装值，实际分频系数等于写入值加 1，所以要得到 32768 分频，需要写 32767。

#### 易错点

- LSE 没有起振成功，程序卡在等待 `RCC_FLAG_LSERDY` 的循环中。
- 预分频值写错，导致时间过快或过慢。
- 误以为 RTC 时钟源可以随时切换，实际通常要复位备份域后重新配置。
- 板子没有焊接 32.768 kHz 晶振，却仍然选择 LSE。

---

## 本节核心记忆

```text
BKP 写入三件套：开 PWR 时钟 + 开 BKP 时钟 + 允许备份域写访问
```

```text
STM32F1 RTC 本质是秒计数器，年月日时分秒需要软件转换
```

```text
LSE = 32.768 kHz，RTC_SetPrescaler(32768 - 1) 后得到 1 Hz 计数
```

```text
BKP_DR1 = 0xA5A5 常用作 RTC 已初始化标志，避免复位后反复重设时间
```

## 开发过程总结

### 问题 1：备份寄存器写入后读不回来

现象：

- 程序编译和下载都正常。
- `BKP_WriteBackupRegister()` 已经调用。
- OLED 上显示的读回值不是预期的 `0x1234`。

排查过程：

1. 检查是否开启了 `RCC_APB1Periph_PWR` 时钟。
2. 检查是否开启了 `RCC_APB1Periph_BKP` 时钟。
3. 检查是否调用了 `PWR_BackupAccessCmd(ENABLE)`。
4. 检查是否在后面误调用了 `BKP_DeInit()`。

解决方案：

- 写 BKP 前固定先执行 PWR、BKP 时钟使能和备份域写访问使能。
- 只在需要重新初始化备份域时调用 `BKP_DeInit()`。
- 把 BKP 测试值和 RTC 初始化标志分开规划，例如 `BKP_DR1` 存标志、`BKP_DR2` 存测试值，避免互相覆盖。

### 问题 2：按 Reset 后 RTC 时间又回到默认值

现象：

- 程序运行时 OLED 时间可以正常递增。
- 一按复位键，时间又变回 `2023-01-01 23:59:55` 一类默认值。
- 看起来 RTC 在运行，但没有“持续计时”的效果。

排查过程：

1. 检查 `RTC_SetCounter()` 是否每次启动都被无条件调用。
2. 检查是否使用 BKP 寄存器保存初始化标志。
3. 检查 `BKP_DeInit()` 是否放在了每次启动都会执行的位置。

解决方案：

- 使用 `BKP_DR1` 保存类似 `0xA5A5` 的初始化标志。
- 只有标志不存在时才初始化 RTC 并设置默认时间。
- 标志存在时只执行 `RTC_WaitForSynchro()` 和必要等待，不再重置计数器。

### 问题 3：年月或月份显示明显不对

现象：

- 设置的是 2023 年，显示出来却是异常年份。
- 设置的是 1 月，显示出来像是 2 月。
- 秒数在走，但日期格式转换错误。

排查过程：

1. 检查 `struct tm` 的 `tm_year` 是否写成“年份减 1900”。
2. 检查 `struct tm` 的 `tm_mon` 是否写成“月份减 1”。
3. 检查从 `localtime()` 读回后是否把年份和月份加回去。

解决方案：

- 写入时使用 `tm_year = year - 1900`。
- 写入时使用 `tm_mon = month - 1`。
- 读取时使用 `year = tm_year + 1900`，`month = tm_mon + 1`。

### 问题 4：程序卡在等待 LSE 就绪

现象：

- 程序停在 `while (RCC_GetFlagStatus(RCC_FLAG_LSERDY) == RESET);`。
- OLED 不继续刷新，RTC 初始化没有完成。

排查过程：

1. 检查开发板是否焊接 32.768 kHz 晶振。
2. 检查晶振负载电容是否匹配。
3. 检查焊接质量和板级硬件是否正常。
4. 临时改用 LSI 验证 RTC 主流程是否正确。

解决方案：

- 确认硬件上存在可用 LSE 晶振。
- 如果只是验证软件流程，可以临时使用 LSI，但要接受精度较差的问题。
- 在正式 RTC 项目中优先保证 LSE 硬件可靠。

## 结果展示

> 实验 1：烧录 BKP 测试程序后，OLED 能显示从 `BKP_DR2` 读回的十六进制数据，例如 `1234`。✅

> 实验 2：烧录 RTC 程序后，OLED 第 2 行显示日期，第 3 行显示时间，秒数每秒递增。✅

> 实验 3：按 Reset 复位后，如果备份域未清空，RTC 不会回到默认时间，而是继续显示当前计数换算后的时间。✅

## 本节小结

本节把前面学过的“外设初始化流程”推进到了备份域。BKP 解决的是少量数据掉电保持问题，RTC 解决的是时间持续运行问题，两者都要求先理解备份域写保护和时钟源配置。

复习时最重要的是抓住这条主线：

```text
先解除备份域写保护，再配置 LSE 和 RTC；用 BKP 标志判断是否首次初始化；用 RTC 秒计数保存时间；用 time.h 做日历转换。
```

面试中如果被问到 RTC，重点不要只背函数名，而要能讲清楚：为什么要用 LSE、为什么要设置预分频、为什么要用 BKP 标志、为什么 STM32F1 需要软件转换年月日时分秒。这些才是能体现你真的做过实验、也理解底层机制的地方。

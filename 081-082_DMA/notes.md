# 08 — DMA直接存储器存取：DMA数据转运与DMA+AD多通道

## 实验概述

本节对应江协科技《STM32入门教程-2023版》的 **[8-2] DMA数据转运 & DMA+AD多通道**，视频时长约 **53:09**。

本节通过两个实验理解 DMA（Direct Memory Access，直接存储器存取）的实际使用方法：

1. 使用 DMA 将 SRAM 中的源数组 `DataA` 复制到目标数组 `DataB`
2. 使用 ADC1 扫描 PA0～PA3 四个模拟输入通道，并由 DMA1 通道 1 自动把转换结果循环搬运到 SRAM 数组

本节的核心目标是掌握：

> 配置源地址与目标地址 → 设置数据宽度和地址自增 → 选择普通/循环模式 → 选择软件触发或外设请求 → 启动 DMA 完成自动搬运

> 说明：无法直接获取视频的完整字幕和全部画面，以下笔记根据可访问的分集标题、公开课程笔记、STM32F103 参考手册体系和标准外设库接口整理。代码为贴合课程内容的标准外设库参考实现，不宣称是视频源码的逐字转录。

---

## 硬件连接

### 实验一：DMA 数组数据转运

该实验主要验证 STM32 内部 SRAM 数组之间的数据复制，不需要额外传感器。OLED 用于观察源数组、目标数组及其地址。

| 器件 | 对应引脚 | 说明 |
|---|---:|---|
| STM32F103C8T6 | — | 运行 DMA 数据转运程序 |
| OLED SCL | PB8 | 软件 I²C 时钟线，具体以已有 OLED 驱动为准 |
| OLED SDA | PB9 | 软件 I²C 数据线，具体以已有 OLED 驱动为准 |
| OLED VCC | 3.3V | OLED 供电 |
| OLED GND | GND | 与 STM32 共地 |
| ST-Link V2 | PA13、PA14、GND、3.3V | SWD 下载与调试 |

### 实验二：DMA + ADC 多通道

| 模拟输入器件 | ADC 引脚 | ADC 通道 | 说明 |
|---|---:|---:|---|
| 电位器模拟输出 | PA0 | ADC1_IN0 | 输出 0～3.3V 模拟电压 |
| 光敏传感器模拟输出 | PA1 | ADC1_IN1 | 光照变化对应模拟电压变化 |
| 热敏传感器模拟输出 | PA2 | ADC1_IN2 | 温度变化对应模拟电压变化 |
| 反射式红外传感器模拟输出 | PA3 | ADC1_IN3 | 反射强度对应模拟电压变化 |
| OLED SCL | PB8 | — | 软件 I²C 时钟线 |
| OLED SDA | PB9 | — | 软件 I²C 数据线 |
| 各模块 VCC | 3.3V | — | 模拟输入不得超过 ADC 允许范围 |
| 各模块 GND | GND | — | 所有模块必须共地 |

> 注意：不同传感器模块的模拟输出引脚可能标记为 `AO`、`AOUT` 或 `OUT`。具体接线和输出范围应以模块原理图为准，不要将 5V 模拟信号直接送入 STM32 ADC 引脚。

---

## 工程文件结构

建议将两个实验分别建立工程，避免不同 DMA 配置互相影响：

```text
08-DMA-Transfer-ADC
├── DMA数据转运
│   ├── Code
│   │   ├── main.c
│   │   ├── MyDMA.c
│   │   ├── MyDMA.h
│   │   ├── OLED.c
│   │   ├── OLED.h
│   │   ├── Delay.c
│   │   └── Delay.h
│   ├── Hardware
│   │   └── DMA_Memory_To_Memory.png
│   ├── Results
│   │   └── DMA_Array_Copy.gif
│   └── README.md
├── DMA+AD多通道
│   ├── Code
│   │   ├── main.c
│   │   ├── AD.c
│   │   ├── AD.h
│   │   ├── OLED.c
│   │   ├── OLED.h
│   │   ├── Delay.c
│   │   └── Delay.h
│   ├── Hardware
│   │   └── ADC_DMA_4Channels.png
│   ├── Results
│   │   └── ADC_DMA_Display.gif
│   └── README.md
└── README.md
```

---

## 核心代码

### 1. MyDMA.h

```c
#ifndef __MYDMA_H                                                        // 判断是否尚未定义 __MYDMA_H，防止头文件被重复包含
#define __MYDMA_H                                                        // 定义头文件保护宏，使本文件在一次编译中只展开一次

#include "stm32f10x.h"                                                   // 引入 STM32F10x 类型、寄存器映射和标准外设库声明

void MyDMA_Init(uint32_t AddrA, uint32_t AddrB, uint16_t Size);          // 声明 DMA 初始化函数，参数依次为源地址、目标地址和传输数量
void MyDMA_Transfer(void);                                               // 声明启动一次 DMA 搬运并等待完成的函数

#endif                                                                   // 结束 __MYDMA_H 对应的条件编译区域
```

### 2. MyDMA.c

```c
#include "MyDMA.h"                                                       // 引入本模块头文件，获得函数声明和 STM32 基础类型

static uint16_t MyDMA_Size;                                              // 保存初始化时的传输数量，便于每次重新装载 DMA 传输计数器

void MyDMA_Init(uint32_t AddrA, uint32_t AddrB, uint16_t Size)           // 定义 DMA 初始化函数，配置从 AddrA 到 AddrB 的存储器复制
{                                                                        // 进入 MyDMA_Init 函数体
    DMA_InitTypeDef DMA_InitStructure;                                   // 定义 DMA 初始化结构体，用于集中配置通道参数

    MyDMA_Size = Size;                                                   // 记录本次任务的数据数量，后续重复传输时重新写入 CNDTR
    RCC_AHBPeriphClockCmd(RCC_AHBPeriph_DMA1, ENABLE);                   // 开启 DMA1 的 AHB 外设时钟，否则 DMA 寄存器配置不会生效
    DMA_DeInit(DMA1_Channel1);                                           // 将 DMA1 通道 1 恢复为复位状态，避免旧工程配置残留

    DMA_InitStructure.DMA_PeripheralBaseAddr = AddrA;                    // 把 DMA 的“外设站点”地址设置为源数组首地址
    DMA_InitStructure.DMA_MemoryBaseAddr = AddrB;                        // 把 DMA 的“存储器站点”地址设置为目标数组首地址
    DMA_InitStructure.DMA_DIR = DMA_DIR_PeripheralSRC;                   // 设置传输方向为外设站点读取、存储器站点写入
    DMA_InitStructure.DMA_BufferSize = Size;                             // 设置本轮需要搬运的数据单元数量
    DMA_InitStructure.DMA_PeripheralInc = DMA_PeripheralInc_Enable;      // 每搬运一个字节后源地址自增，依次读取源数组各元素
    DMA_InitStructure.DMA_MemoryInc = DMA_MemoryInc_Enable;              // 每搬运一个字节后目标地址自增，依次写入目标数组各元素
    DMA_InitStructure.DMA_PeripheralDataSize = DMA_PeripheralDataSize_Byte; // 设置源站点一次读取 8 位，与 uint8_t 数组元素宽度一致
    DMA_InitStructure.DMA_MemoryDataSize = DMA_MemoryDataSize_Byte;      // 设置目标站点一次写入 8 位，避免数组元素错位
    DMA_InitStructure.DMA_Mode = DMA_Mode_Normal;                        // 使用普通模式，计数器减到 0 后本轮传输停止
    DMA_InitStructure.DMA_Priority = DMA_Priority_Medium;                // 设置通道为中等优先级，参与多个 DMA 请求时的仲裁
    DMA_InitStructure.DMA_M2M = DMA_M2M_Enable;                          // 开启存储器到存储器模式，使 DMA 无需外设请求即可连续搬运
    DMA_Init(DMA1_Channel1, &DMA_InitStructure);                         // 将结构体参数写入 DMA1 通道 1 的配置寄存器

    DMA_Cmd(DMA1_Channel1, DISABLE);                                     // 初始化后先保持通道关闭，由传输函数决定具体启动时机
}                                                                        // 结束 MyDMA_Init 函数

void MyDMA_Transfer(void)                                                // 定义执行一次完整 DMA 数据转运的函数
{                                                                        // 进入 MyDMA_Transfer 函数体
    DMA_Cmd(DMA1_Channel1, DISABLE);                                     // 修改传输计数器前先关闭 DMA 通道，满足硬件配置要求
    DMA_SetCurrDataCounter(DMA1_Channel1, MyDMA_Size);                   // 将保存的数据数量重新装载到当前传输计数器 CNDTR
    DMA_ClearFlag(DMA1_FLAG_GL1);                                        // 清除通道 1 的全局、完成、半传输和错误等旧标志
    DMA_Cmd(DMA1_Channel1, ENABLE);                                      // 重新使能 DMA，存储器到存储器模式立即开始搬运

    while (DMA_GetFlagStatus(DMA1_FLAG_TC1) == RESET)                    // 持续查询通道 1 传输完成标志，等待全部数据搬运结束
    {                                                                    // 进入轮询等待循环
    }                                                                    // DMA 完成后退出等待循环
    DMA_ClearFlag(DMA1_FLAG_TC1);                                        // 清除传输完成标志，为下一次搬运做好准备
}                                                                        // 结束 MyDMA_Transfer 函数
```

### 3. DMA 数据转运 main.c

```c
#include "stm32f10x.h"                                                   // 引入 STM32F10x 芯片定义和标准外设库接口
#include "Delay.h"                                                       // 引入毫秒延时函数，用于分隔转运前后的显示现象
#include "OLED.h"                                                        // 引入 OLED 显示函数，用于观察数组内容和地址
#include "MyDMA.h"                                                       // 引入自定义 DMA 初始化与传输函数

uint8_t DataA[4] = {0x01, 0x02, 0x03, 0x04};                            // 定义 SRAM 中的源数组，DMA 将从这里读取数据
uint8_t DataB[4] = {0x00, 0x00, 0x00, 0x00};                            // 定义 SRAM 中的目标数组，初始值全部清零

int main(void)                                                           // 定义程序入口函数
{                                                                        // 进入 main 函数体
    OLED_Init();                                                         // 初始化 OLED 显示屏，准备显示数组地址和元素
    MyDMA_Init((uint32_t)DataA, (uint32_t)DataB, 4);                     // 配置 DMA 将 DataA 的 4 个字节复制到 DataB

    OLED_ShowString(1, 1, "DataA");                                      // 在第 1 行显示源数组名称
    OLED_ShowString(3, 1, "DataB");                                      // 在第 3 行显示目标数组名称
    OLED_ShowHexNum(1, 8, (uint32_t)DataA, 8);                           // 以 8 位十六进制显示 DataA 首地址，通常位于 0x20000000 SRAM 区
    OLED_ShowHexNum(3, 8, (uint32_t)DataB, 8);                           // 以 8 位十六进制显示 DataB 首地址，验证两个数组地址不同

    while (1)                                                            // 进入主循环，持续修改源数组并重复验证 DMA 复制
    {                                                                    // 进入 while 循环体
        DataA[0]++;                                                       // 将源数组第 0 个元素加 1，制造新的待转运数据
        DataA[1]++;                                                       // 将源数组第 1 个元素加 1，便于观察数组整体变化
        DataA[2]++;                                                       // 将源数组第 2 个元素加 1，验证源地址自动递增
        DataA[3]++;                                                       // 将源数组第 3 个元素加 1，验证四个字节均被复制

        OLED_ShowHexNum(2, 1, DataA[0], 2);                              // 显示转运前源数组第 0 个元素
        OLED_ShowHexNum(2, 4, DataA[1], 2);                              // 显示转运前源数组第 1 个元素
        OLED_ShowHexNum(2, 7, DataA[2], 2);                              // 显示转运前源数组第 2 个元素
        OLED_ShowHexNum(2, 10, DataA[3], 2);                             // 显示转运前源数组第 3 个元素
        OLED_ShowHexNum(4, 1, DataB[0], 2);                              // 显示转运前目标数组第 0 个元素
        OLED_ShowHexNum(4, 4, DataB[1], 2);                              // 显示转运前目标数组第 1 个元素
        OLED_ShowHexNum(4, 7, DataB[2], 2);                              // 显示转运前目标数组第 2 个元素
        OLED_ShowHexNum(4, 10, DataB[3], 2);                             // 显示转运前目标数组第 3 个元素
        Delay_ms(1000);                                                   // 保留一秒观察时间，确认此时 DataB 仍是上一次结果

        MyDMA_Transfer();                                                 // 启动一次 DMA 传输，并等待 4 个字节全部复制完成

        OLED_ShowHexNum(2, 1, DataA[0], 2);                              // 再次显示源数组第 0 个元素，验证 DMA 不会清空源数据
        OLED_ShowHexNum(2, 4, DataA[1], 2);                              // 再次显示源数组第 1 个元素
        OLED_ShowHexNum(2, 7, DataA[2], 2);                              // 再次显示源数组第 2 个元素
        OLED_ShowHexNum(2, 10, DataA[3], 2);                             // 再次显示源数组第 3 个元素
        OLED_ShowHexNum(4, 1, DataB[0], 2);                              // 显示复制后的目标数组第 0 个元素，应与 DataA[0] 相同
        OLED_ShowHexNum(4, 4, DataB[1], 2);                              // 显示复制后的目标数组第 1 个元素，应与 DataA[1] 相同
        OLED_ShowHexNum(4, 7, DataB[2], 2);                              // 显示复制后的目标数组第 2 个元素，应与 DataA[2] 相同
        OLED_ShowHexNum(4, 10, DataB[3], 2);                             // 显示复制后的目标数组第 3 个元素，应与 DataA[3] 相同
        Delay_ms(1000);                                                   // 保留一秒观察复制后的结果，再进入下一轮
    }                                                                    // 结束主循环体
}                                                                        // 结束 main 函数
```

### 4. AD.h

```c
#ifndef __AD_H                                                           // 判断是否尚未定义 __AD_H，避免头文件重复展开
#define __AD_H                                                           // 定义 AD 模块头文件保护宏

#include "stm32f10x.h"                                                   // 引入 uint16_t、ADC 和 DMA 等标准库定义

extern volatile uint16_t AD_Value[4];                                    // 声明四通道 ADC 结果数组，volatile 表示数据会被 DMA 异步更新
void AD_Init(void);                                                      // 声明 ADC 扫描模式与 DMA 循环搬运的初始化函数

#endif                                                                   // 结束 AD 模块头文件保护
```

### 5. AD.c

```c
#include "AD.h"                                                          // 引入 AD 模块声明及 STM32 标准外设库定义

volatile uint16_t AD_Value[4] = {0, 0, 0, 0};                            // 定义四路 ADC 结果缓冲区，DMA 按 Rank 顺序循环写入

void AD_Init(void)                                                       // 定义 ADC1 四通道扫描与 DMA 自动搬运初始化函数
{                                                                        // 进入 AD_Init 函数体
    GPIO_InitTypeDef GPIO_InitStructure;                                 // 定义 GPIO 初始化结构体，用于配置 PA0～PA3
    ADC_InitTypeDef ADC_InitStructure;                                   // 定义 ADC 初始化结构体，用于配置扫描和连续转换
    DMA_InitTypeDef DMA_InitStructure;                                   // 定义 DMA 初始化结构体，用于配置 ADC_DR 到数组的搬运

    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA, ENABLE);                // 开启 GPIOA 时钟，使 PA0～PA3 配置能够生效
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_ADC1, ENABLE);                 // 开启 ADC1 外设时钟
    RCC_AHBPeriphClockCmd(RCC_AHBPeriph_DMA1, ENABLE);                   // 开启 DMA1 的 AHB 时钟
    RCC_ADCCLKConfig(RCC_PCLK2_Div6);                                    // 将 72MHz PCLK2 六分频为 12MHz，满足 ADC 时钟上限要求

    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_AIN;                        // 将引脚配置为模拟输入，关闭数字输入输出缓冲以减少干扰
    GPIO_InitStructure.GPIO_Pin = GPIO_Pin_0 | GPIO_Pin_1 | GPIO_Pin_2 | GPIO_Pin_3; // 同时选择 PA0、PA1、PA2、PA3 四个 ADC 输入引脚
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;                    // 填写结构体速度字段；模拟输入模式下该字段不参与输出驱动
    GPIO_Init(GPIOA, &GPIO_InitStructure);                               // 将模拟输入配置写入 GPIOA 寄存器

    ADC_InitStructure.ADC_Mode = ADC_Mode_Independent;                   // 配置 ADC1 独立工作，不与 ADC2 组成双 ADC 模式
    ADC_InitStructure.ADC_ScanConvMode = ENABLE;                         // 开启扫描模式，使规则组按 Rank 顺序转换多个通道
    ADC_InitStructure.ADC_ContinuousConvMode = ENABLE;                   // 开启连续转换，一轮扫描结束后自动开始下一轮
    ADC_InitStructure.ADC_ExternalTrigConv = ADC_ExternalTrigConv_None;  // 不使用外部事件触发，后续通过软件启动首次转换
    ADC_InitStructure.ADC_DataAlign = ADC_DataAlign_Right;               // 将 12 位结果右对齐，数组中直接得到 0～4095
    ADC_InitStructure.ADC_NbrOfChannel = 4;                              // 指定规则组序列包含 4 个有效 Rank
    ADC_Init(ADC1, &ADC_InitStructure);                                  // 将上述模式参数写入 ADC1 控制寄存器

    ADC_RegularChannelConfig(ADC1, ADC_Channel_0, 1, ADC_SampleTime_55Cycles5); // 将 PA0/通道0放入规则组 Rank1，并设置 55.5 周期采样
    ADC_RegularChannelConfig(ADC1, ADC_Channel_1, 2, ADC_SampleTime_55Cycles5); // 将 PA1/通道1放入规则组 Rank2，结果写入 AD_Value[1]
    ADC_RegularChannelConfig(ADC1, ADC_Channel_2, 3, ADC_SampleTime_55Cycles5); // 将 PA2/通道2放入规则组 Rank3，结果写入 AD_Value[2]
    ADC_RegularChannelConfig(ADC1, ADC_Channel_3, 4, ADC_SampleTime_55Cycles5); // 将 PA3/通道3放入规则组 Rank4，结果写入 AD_Value[3]

    DMA_DeInit(DMA1_Channel1);                                           // 复位 DMA1 通道 1，清除先前工程可能遗留的配置
    DMA_InitStructure.DMA_PeripheralBaseAddr = (uint32_t)&ADC1->DR;      // 将源地址固定为 ADC1 数据寄存器 DR
    DMA_InitStructure.DMA_MemoryBaseAddr = (uint32_t)AD_Value;           // 将目标地址设置为四元素 ADC 结果数组首地址
    DMA_InitStructure.DMA_DIR = DMA_DIR_PeripheralSRC;                   // 设置方向为从 ADC 外设读取并写入 SRAM
    DMA_InitStructure.DMA_BufferSize = 4;                                // 每轮搬运 4 个半字，对应规则组的四个 Rank
    DMA_InitStructure.DMA_PeripheralInc = DMA_PeripheralInc_Disable;     // ADC_DR 地址固定不变，每次都从同一寄存器读取
    DMA_InitStructure.DMA_MemoryInc = DMA_MemoryInc_Enable;              // SRAM 地址逐次递增，使四个结果依次进入数组各元素
    DMA_InitStructure.DMA_PeripheralDataSize = DMA_PeripheralDataSize_HalfWord; // 设置源数据宽度为 16 位，覆盖 ADC 12 位结果
    DMA_InitStructure.DMA_MemoryDataSize = DMA_MemoryDataSize_HalfWord;  // 设置目标数据宽度为 16 位，与 uint16_t 数组一致
    DMA_InitStructure.DMA_Mode = DMA_Mode_Circular;                      // 使用循环模式，计数到 0 后自动重装并从数组开头继续写
    DMA_InitStructure.DMA_Priority = DMA_Priority_Medium;                // 设置 DMA 通道中等优先级，满足本实验持续采样需求
    DMA_InitStructure.DMA_M2M = DMA_M2M_Disable;                         // 关闭存储器到存储器模式，改由 ADC1 硬件请求触发每次搬运
    DMA_Init(DMA1_Channel1, &DMA_InitStructure);                         // 将参数写入与 ADC1 请求映射对应的 DMA1 通道 1

    DMA_Cmd(DMA1_Channel1, ENABLE);                                      // 先使能 DMA 通道，使其准备接收 ADC1 的 DMA 请求
    ADC_DMACmd(ADC1, ENABLE);                                            // 开启 ADC1 的 DMA 请求输出，每次转换完成请求搬运结果
    ADC_Cmd(ADC1, ENABLE);                                               // 开启 ADC1 转换器电源和工作逻辑

    ADC_ResetCalibration(ADC1);                                          // 启动 ADC 校准寄存器复位，清除旧校准参数
    while (ADC_GetResetCalibrationStatus(ADC1) == SET)                   // 轮询等待硬件完成校准复位
    {                                                                    // 进入校准复位等待循环
    }                                                                    // 复位完成后退出等待循环

    ADC_StartCalibration(ADC1);                                          // 启动 ADC 自校准，以减小内部偏置造成的转换误差
    while (ADC_GetCalibrationStatus(ADC1) == SET)                        // 轮询等待 ADC 自校准过程完成
    {                                                                    // 进入自校准等待循环
    }                                                                    // 校准完成后退出等待循环

    ADC_SoftwareStartConvCmd(ADC1, ENABLE);                              // 软件触发首次转换，连续扫描模式随后会自动不断运行
}                                                                        // 结束 AD_Init 函数
```

### 6. DMA + AD 多通道 main.c

```c
#include "stm32f10x.h"                                                   // 引入 STM32F10x 芯片和标准外设库定义
#include "Delay.h"                                                       // 引入延时函数，控制 OLED 刷新频率
#include "OLED.h"                                                        // 引入 OLED 显示接口
#include "AD.h"                                                          // 引入 ADC-DMA 初始化函数和结果数组声明

int main(void)                                                           // 定义程序入口函数
{                                                                        // 进入 main 函数体
    OLED_Init();                                                         // 初始化 OLED 显示屏
    AD_Init();                                                           // 初始化 PA0～PA3、ADC1 扫描模式和 DMA1 通道 1 循环搬运

    OLED_ShowString(1, 1, "AD0:");                                       // 在第 1 行显示 ADC 通道 0 标签
    OLED_ShowString(2, 1, "AD1:");                                       // 在第 2 行显示 ADC 通道 1 标签
    OLED_ShowString(3, 1, "AD2:");                                       // 在第 3 行显示 ADC 通道 2 标签
    OLED_ShowString(4, 1, "AD3:");                                       // 在第 4 行显示 ADC 通道 3 标签

    while (1)                                                            // 进入主循环，CPU 只负责读取数组并刷新显示
    {                                                                    // 进入 while 循环体
        OLED_ShowNum(1, 5, AD_Value[0], 4);                              // 显示 Rank1/ADC1_IN0 的最新 12 位转换结果
        OLED_ShowNum(2, 5, AD_Value[1], 4);                              // 显示 Rank2/ADC1_IN1 的最新 12 位转换结果
        OLED_ShowNum(3, 5, AD_Value[2], 4);                              // 显示 Rank3/ADC1_IN2 的最新 12 位转换结果
        OLED_ShowNum(4, 5, AD_Value[3], 4);                              // 显示 Rank4/ADC1_IN3 的最新 12 位转换结果
        Delay_ms(100);                                                    // 每 100ms 刷新一次，避免 OLED 更新过快造成闪烁
    }                                                                    // 结束主循环体
}                                                                        // 结束 main 函数
```

---

## 代码要点

| 行/段 | 说明 |
|---|---|
| `RCC_AHBPeriphClockCmd(RCC_AHBPeriph_DMA1, ENABLE)` | DMA1 挂载在 AHB 总线上，使用前必须开启 AHB 外设时钟。 |
| `DMA_PeripheralBaseAddr` / `DMA_MemoryBaseAddr` | DMA 把传输两端抽象成“外设站点”和“存储器站点”；在 M2M 模式下，两端都可以实际指向 SRAM。 |
| `DMA_DIR_PeripheralSRC` | 表示数据从外设站点地址流向存储器站点地址，名称描述的是 DMA 两个逻辑站点，不代表源地址必须真是外设寄存器。 |
| `DMA_PeripheralInc` / `DMA_MemoryInc` | 数组端通常需要自增，固定外设寄存器端通常必须禁止自增。 |
| `DMA_PeripheralDataSize` / `DMA_MemoryDataSize` | 数据宽度必须与源数据、目标对象的实际元素类型匹配，否则会出现丢高位、补零、错位或越界。 |
| `DMA_Mode_Normal` | 普通模式传输计数减到 0 后停止，适合一次性数组复制。 |
| `DMA_Mode_Circular` | 循环模式在计数到 0 后自动重装初值，适合 ADC 连续扫描等持续数据流。 |
| `DMA_M2M_Enable` | 开启存储器到存储器模式后，不等待外设 DMA 请求，通道使能后主动连续搬运。 |
| `DMA_M2M_Disable` | 由固定映射的外设 DMA 请求驱动传输；ADC1 在 STM32F103 中使用 DMA1 通道 1。 |
| `DMA_SetCurrDataCounter()` | 重新写入传输计数器前必须关闭 DMA 通道。 |
| `DMA1_FLAG_TC1` | DMA1 通道 1 的传输完成标志，轮询模式下完成后应主动清除。 |
| `ADC_ScanConvMode = ENABLE` | 开启规则组扫描，ADC 按 Rank 顺序依次转换多个通道。 |
| `ADC_ContinuousConvMode = ENABLE` | 一轮扫描结束后自动开始下一轮，与 DMA 循环模式配合形成持续采集。 |
| `ADC_DMACmd(ADC1, ENABLE)` | 打开 ADC1 到 DMA 控制器的请求通路，否则 ADC 转换完成不会触发 DMA 搬运。 |
| `volatile uint16_t AD_Value[4]` | 数组会被 DMA 在 CPU 指令流之外修改，`volatile` 防止编译器错误地长期缓存读取结果。 |

---

## 关键知识点

### 1. DMA 的基本工作机制

#### 原理

DMA 是独立于 CPU 的数据搬运硬件。CPU 只负责提前配置源地址、目标地址、数据宽度、传输数量和触发方式；真正的数据读写由 DMA 控制器通过总线完成。

DMA 的典型工作流程是：

```text
CPU 配置 DMA → 触发 DMA 请求 → DMA 获得总线访问权 → 读取源数据 → 写入目标地址 → 计数器减 1 → 传输结束或继续下一次
```

#### 特点

- 可以在不逐字节占用 CPU 指令的情况下搬运数据。
- 适合 ADC、USART、SPI 等高速或连续数据流。
- 可以产生半传输、传输完成和传输错误标志或中断。
- DMA 仍然占用系统总线带宽，并不是“完全没有成本”。

#### 面试易问

**Q：DMA 为什么能降低 CPU 负担？**

A：CPU 只负责初始化和处理完成事件，重复的数据读取与写入由 DMA 硬件执行，避免 CPU 在高频数据流中不断轮询和复制。

**Q：使用 DMA 后 CPU 是否完全不参与？**

A：不是。CPU 仍需配置 DMA、启动外设、处理完成或错误事件，并在必要时协调缓冲区；只是数据搬运过程不再由 CPU 逐项执行。

#### 易错点

- 认为 DMA 不占用总线，忽略其与 CPU 或其他主设备的总线竞争。
- 没有开启 DMA 外设时钟。
- 源地址、目标地址或传输数量配置错误，造成越界写入。
- DMA 传输尚未完成就读取目标缓冲区。

---

### 2. STM32 存储器映像与地址

#### 原理

STM32 使用统一地址空间。Flash、SRAM、外设寄存器和内核外设寄存器都有各自固定的地址区域，CPU 和 DMA 都通过地址访问它们。

常见地址区域可概括为：

| 地址前缀 | 典型区域 | 用途 |
|---|---|---|
| `0x0800 0000` | 主 Flash | 存放程序代码和只读常量 |
| `0x1FFF F000` 附近 | 系统存储器 | 存放芯片内置 BootLoader |
| `0x2000 0000` | SRAM | 存放运行时变量、栈、堆和数组 |
| `0x4000 0000` | 外设寄存器 | GPIO、ADC、TIM、USART 等寄存器 |
| `0xE000 0000` | Cortex-M3 内核外设 | NVIC、SysTick、调试组件等 |

#### 特点

- 普通全局变量通常位于 SRAM。
- `const` 数据通常由链接器安排在 Flash，但最终位置取决于链接脚本和编译器设置。
- 外设寄存器地址由芯片硬件固定。
- DMA 配置中的地址本质上都是 32 位地址值。

#### 面试易问

**Q：为什么 `(uint32_t)DataA` 可以作为 DMA 地址？**

A：数组名在表达式中通常退化为首元素地址，将其转换为 32 位整数后可以写入 DMA 地址寄存器。

**Q：`ADC1->DR` 和 `&ADC1->DR` 有什么区别？**

A：`ADC1->DR` 表示读取数据寄存器中的值，`&ADC1->DR` 表示取得该寄存器的地址；DMA 需要配置地址，因此应使用后者。

#### 易错点

- 把寄存器的“值”误填为寄存器“地址”。
- 直接显示指针却未按显示函数要求转换类型。
- 假定所有 `const` 对象一定在某个固定地址，忽略链接器配置。
- 把无效地址或局部变量失效后的地址交给 DMA。

---

### 3. DMA 的两个站点与传输方向

#### 原理

STM32F1 DMA 将传输两端命名为“外设站点”和“存储器站点”。这是一种寄存器接口命名：

- 外设站点地址写入 `CPAR`
- 存储器站点地址写入 `CMAR`
- `DIR` 决定两端的数据流向

在存储器到存储器模式中，外设站点同样可以填入 SRAM 地址，因此不要被“Peripheral”字样误导。

#### 特点

- `DMA_DIR_PeripheralSRC`：外设站点为源，存储器站点为目标。
- `DMA_DIR_PeripheralDST`：存储器站点为源，外设站点为目标。
- M2M 模式下两端都可指向存储器。
- 外设请求映射模式下，通道选择受硬件映射限制。

#### 面试易问

**Q：DMA 的外设地址一定必须是外设寄存器吗？**

A：在普通外设请求模式下通常如此；但在存储器到存储器模式中，该寄存器只是逻辑上的一个站点，也可以填写 SRAM 或 Flash 地址。

**Q：为什么 DataA 放在 PeripheralBaseAddr？**

A：因为代码选择 `DMA_DIR_PeripheralSRC`，所以逻辑外设站点被定义为源；开启 M2M 后，该站点允许指向存储器数组。

#### 易错点

- 配置方向与两端地址相反，导致数据流向错误。
- 误认为 M2M 模式仍需某个真实外设触发。
- 在外设请求模式中随意选择 DMA 通道。
- 把只读 Flash 区域配置为 DMA 目标地址。

---

### 4. 数据宽度与对齐

#### 原理

DMA 两端都可以分别配置为 8 位字节、16 位半字或 32 位字。每次请求到来时，DMA 按配置宽度读取源端并写入目标端。

本节中：

- `uint8_t` 数组复制：源端和目标端都配置为 Byte
- ADC 结果搬运：ADC_DR 与 `uint16_t` 数组都配置为 HalfWord

#### 特点

- 两端宽度相同最容易理解，也最不容易出错。
- 目标宽度大于源宽度时，硬件会按规则扩展数据。
- 目标宽度小于源宽度时，高位可能被舍弃。
- 地址自增的步长由对应站点的数据宽度决定。

#### 面试易问

**Q：ADC 是 12 位，为什么 DMA 配置为 16 位？**

A：标准 DMA 没有 12 位宽度选项，ADC 结果存放在 16 位可访问的数据寄存器中，因此使用 16 位半字搬运。

**Q：数据宽度配错会发生什么？**

A：可能造成高位丢失、数据补零、元素错位、地址步长错误，严重时会写越界并破坏其他变量。

#### 易错点

- 用 Byte 搬运 ADC 数据，导致只保留低 8 位。
- `uint16_t` 数组却把目标宽度配置成 Word。
- 忽略地址自增步长随数据宽度变化。
- 缓冲区未按数据宽度对齐。

---

### 5. 地址自增

#### 原理

每完成一次数据单元传输，DMA 可以选择保持地址不变，或按数据宽度自动移动到下一个地址。

数组复制时：

```text
DataA[0] → DataB[0]
DataA[1] → DataB[1]
DataA[2] → DataB[2]
DataA[3] → DataB[3]
```

因此两端都需要自增。

ADC 搬运时，源始终是同一个 `ADC1->DR`，而目标依次是 `AD_Value[0]`～`AD_Value[3]`，所以源端不自增、目标端自增。

#### 特点

- 固定外设寄存器通常关闭地址自增。
- 数组、缓冲区通常开启地址自增。
- 自增步长自动匹配 Byte、HalfWord 或 Word。
- 地址自增可让 DMA 自动完成连续缓冲区读写。

#### 面试易问

**Q：ADC1->DR 为什么不能开启外设地址自增？**

A：ADC 每次转换结果都写入同一个 DR 寄存器。若开启自增，下一次 DMA 会读取 DR 后面的其他寄存器，得到错误数据。

**Q：目标数组为什么必须开启存储器自增？**

A：否则每次结果都会写在同一个数组元素中，后续转换不断覆盖前一个数据。

#### 易错点

- 数组端忘记开启自增，所有数据堆在第一个元素。
- 固定寄存器端误开启自增。
- 缓冲区长度小于传输计数，DMA 写出数组边界。
- 把结构体或非连续数据误当作普通连续数组搬运。

---

### 6. 普通模式与循环模式

#### 原理

DMA 的传输计数器每搬运一个数据单元就减 1。

- 普通模式：计数减到 0 后停止，需重新关闭通道、装载计数器并再次使能。
- 循环模式：计数减到 0 后自动恢复初始值，存储器地址也回到起点，继续下一轮。

#### 特点

- 普通模式适合一次性数组复制、单帧发送或固定数据块搬运。
- 循环模式适合 ADC 连续采样、音频流和环形数据刷新。
- 循环模式会持续覆盖旧数据。
- 读取循环缓冲区时需要考虑 DMA 正在更新数据。

#### 面试易问

**Q：为什么数组复制使用普通模式？**

A：复制一轮即可完成任务，不需要自动重复；普通模式结束后状态明确，便于程序控制下一次启动。

**Q：为什么 ADC 连续转换要配合 DMA 循环模式？**

A：ADC 每轮扫描都会继续产生新结果，DMA 也需要在搬完一轮后自动重新从数组开头接收下一轮数据。

#### 易错点

- ADC 连续转换配普通 DMA，只有第一轮数组数据有效。
- 普通模式再次启动前没有重新装载 CNDTR。
- 在 DMA 通道仍使能时修改计数器。
- 循环缓冲区被 DMA 更新时执行不安全的整块处理。

---

### 7. 软件触发与外设 DMA 请求

#### 原理

DMA 的启动来源分为两类：

- 存储器到存储器模式：通道使能后由 DMA 主动连续执行，不等待外设请求。
- 外设请求模式：每次外设事件产生一个 DMA 请求，DMA 才搬运一个数据单元。

ADC 多通道实验中，每次规则通道转换完成后，ADC1 发出 DMA 请求，DMA1 通道 1 立即把 `ADC1->DR` 搬到当前数组位置。

#### 特点

- M2M 模式适合内存块快速复制。
- 外设请求模式能让数据搬运与硬件事件严格同步。
- 外设到 DMA 通道的映射由芯片硬件规定。
- DMA 不会自动替代外设自身的使能和触发配置。

#### 面试易问

**Q：`DMA_M2M_Enable` 是否等同于“软件调用一次搬运函数”？**

A：更准确地说，它使 DMA 不依赖外设请求；通道使能后 DMA 会主动执行传输。软件函数只是完成通道使能和状态等待。

**Q：为什么 ADC1 必须使用 DMA1 通道 1？**

A：STM32F103 的 DMA 请求映射是固定的，ADC1 请求连接到 DMA1 通道 1，不是任意通道都能收到该硬件请求。

#### 易错点

- 配置了 ADC 和 DMA，却忘记调用 `ADC_DMACmd()` 打开请求通路。
- 使用错误的 DMA 通道，导致永远收不到外设请求。
- 只使能 DMA，未启动 ADC 转换。
- M2M 模式和外设请求模式混淆。

---

### 8. ADC 扫描模式与 DMA 配合

#### 原理

ADC 扫描模式按照规则组 Rank 顺序依次转换多个通道，但转换结果共用同一个 `ADC_DR` 数据寄存器。若不及时搬走，后一个通道的结果会覆盖前一个结果。

DMA 的解决方案是：

```text
Rank1 转换完成 → ADC_DR → DMA → AD_Value[0]
Rank2 转换完成 → ADC_DR → DMA → AD_Value[1]
Rank3 转换完成 → ADC_DR → DMA → AD_Value[2]
Rank4 转换完成 → ADC_DR → DMA → AD_Value[3]
```

目标地址每次自增，四次传输后循环回数组起点。

#### 特点

- ADC 负责按顺序转换，DMA 负责及时保存每个结果。
- Rank 顺序决定数组元素与物理通道的对应关系。
- 连续扫描 + DMA 循环模式可持续刷新最新数据。
- CPU 主循环不再需要逐通道切换和等待 EOC。

#### 面试易问

**Q：为什么 ADC 多通道扫描常常必须配合 DMA？**

A：多个规则通道共用一个数据寄存器，转换速度又很快。DMA 能在每个结果产生时立即搬走数据，避免覆盖并显著降低 CPU 负担。

**Q：数组下标与 ADC 通道号一定相同吗？**

A：不一定。数组顺序由规则组 Rank 决定。例如把通道 3 配为 Rank1，那么它的结果会进入数组第一个元素。

#### 易错点

- `ADC_NbrOfChannel` 与 DMA 缓冲区长度不一致。
- Rank 重复、跳号或与数组解释顺序不一致。
- 开启扫描但没有配置全部规则通道。
- ADC 连续模式与 DMA 循环模式没有配套设置。

---

### 9. DMA 标志位与轮询完成

#### 原理

DMA 通道可产生多种状态标志：

- `GL`：全局标志
- `TC`：传输完成
- `HT`：半传输
- `TE`：传输错误

数组复制实验采用轮询 `TC1` 的方式确认目标数组已经写完。

#### 特点

- 轮询简单，适合入门和短数据块。
- 中断方式可避免 CPU 忙等。
- 半传输标志适合双半缓冲处理。
- 标志位需要按标准库接口及时清除。

#### 面试易问

**Q：轮询 DMA 完成和 DMA 中断有什么区别？**

A：轮询会让 CPU 在等待循环中占用执行时间；中断允许 CPU 先执行其他任务，完成后再进入中断处理。

**Q：为什么传输前要清理旧标志？**

A：旧的完成标志可能让程序误以为新一轮传输已经完成，从而提前读取尚未更新的数据。

#### 易错点

- 忘记清除完成标志，下一次判断立即成立。
- 只等待 TC，不考虑 TE 传输错误。
- 在中断服务函数中忘记清除挂起位。
- 大数据块仍使用阻塞轮询，抵消 DMA 降低 CPU 占用的优势。

---

### 10. `volatile` 与 DMA 缓冲区

#### 原理

DMA 会在 CPU 指令执行之外修改 SRAM。编译器若不知道这一点，可能把某个数组元素读取一次后长期保存在寄存器中，而不再次访问内存。

使用 `volatile` 告诉编译器：

> 每次读取都必须真正访问该内存位置，因为它可能被硬件异步修改。

#### 特点

- `volatile` 约束编译器优化，但不提供互斥。
- 它不保证多个元素来自同一轮 DMA 扫描。
- 16 位对齐读写在 Cortex-M3 上通常可作为单次总线访问。
- 复杂缓冲区仍需双缓冲、半传输中断或临界区策略。

#### 面试易问

**Q：DMA 缓冲区为什么常声明为 `volatile`？**

A：因为数据不是由当前 CPU 代码显式写入，必须防止编译器把内存读取优化掉或缓存旧值。

**Q：加了 `volatile` 就能保证整组 ADC 数据一致吗？**

A：不能。DMA 可能在 CPU 逐个读取数组时开始下一轮覆盖。需要严格同一时刻快照时，应在安全时机复制缓冲区或采用双缓冲方案。

#### 易错点

- 认为 `volatile` 等同于原子操作或线程安全。
- 对 DMA 缓冲区使用不合适的缓存策略；在带数据缓存的 MCU 上还需维护 Cache 一致性。
- CPU 与 DMA 同时修改同一缓冲区。
- 未规划“生产者—消费者”关系，导致读取到混合批次数据。

---

## 本节核心记忆

```text
DMA = 源地址 + 目标地址 + 数据宽度 + 地址自增 + 传输数量 + 模式 + 触发源
```

```text
数组复制：
源地址自增 + 目标地址自增 + Byte + Normal + M2M
```

```text
ADC扫描：
ADC_DR不自增 + SRAM数组自增 + HalfWord + Circular + ADC硬件请求
```

```text
ADC1 扫描四通道时：
Rank 顺序决定 AD_Value[] 的数据顺序
```

```text
重新设置 DMA 传输计数器前，必须先关闭 DMA 通道
```

---

## 开发过程总结

### 问题 1：DataB 始终保持为 0

现象：

- `DataA` 在 OLED 上持续变化
- 调用 `MyDMA_Transfer()` 后 `DataB` 没有更新
- 程序没有明显报错

排查过程：

1. 检查是否开启 `RCC_AHBPeriph_DMA1`
2. 检查源地址和目标地址是否填写正确
3. 检查 `DMA_M2M` 是否设置为 `Enable`
4. 检查 DMA 通道是否真正使能
5. 检查传输计数器是否为 0

解决方案：

- 开启 DMA1 时钟并在初始化前执行 `DMA_DeInit()`
- 传输前关闭通道、重装 CNDTR、清除旧标志，再重新使能
- 确认 `DataA` 和 `DataB` 都是有效且容量足够的数组

### 问题 2：DataB 只有第一个元素变化

现象：

- `DataB[0]` 被反复覆盖
- `DataB[1]`～`DataB[3]` 始终不变

排查过程：

1. 检查目标端 `DMA_MemoryInc`
2. 检查 `DMA_BufferSize`
3. 检查目标数据宽度与数组元素类型
4. 检查目标数组实际长度

解决方案：

- 将目标地址自增设置为 `DMA_MemoryInc_Enable`
- 将缓冲区长度设置为数组元素数量
- `uint8_t` 数组使用 Byte，`uint16_t` 数组使用 HalfWord

### 问题 3：ADC 四路数据全部相同或顺序混乱

现象：

- 四个数组元素显示相同数值
- 改变某一路传感器却影响错误的显示行
- 数值顺序与预期不一致

排查过程：

1. 检查四个 `ADC_RegularChannelConfig()` 的 Rank
2. 检查 `ADC_NbrOfChannel` 是否为 4
3. 检查 DMA 存储器地址自增是否开启
4. 检查源端 ADC_DR 地址是否禁止自增
5. 检查 PA0～PA3 实际接线

解决方案：

- 按 Rank1～Rank4 顺序配置通道 0～3
- 保证 `AD_Value[0]`～`AD_Value[3]` 与 Rank 一一对应
- 固定外设地址、开启存储器地址自增

### 问题 4：ADC 数据只更新一次

现象：

- 上电后 OLED 有数值
- 后续转动电位器或改变光照，显示不再变化

排查过程：

1. 检查 ADC 是否开启连续转换
2. 检查 DMA 是否使用循环模式
3. 检查 ADC 的 DMA 请求输出是否开启
4. 检查软件触发是否执行
5. 检查 DMA 通道是否选择 DMA1_Channel1

解决方案：

- 使用 `ADC_ContinuousConvMode = ENABLE`
- 使用 `DMA_Mode_Circular`
- 调用 `ADC_DMACmd(ADC1, ENABLE)`
- 在初始化末尾调用一次 `ADC_SoftwareStartConvCmd()`

### 问题 5：ADC 数值跳动大或接近满量程

现象：

- 未接传感器时数值随机跳动
- 接入模块后数值长期为 4095
- 数值与实际电压明显不符

排查过程：

1. 检查模拟输入是否悬空
2. 检查输入电压是否超过 3.3V
3. 检查模块是否与 STM32 共地
4. 检查 GPIO 是否配置为模拟输入
5. 检查 ADC 时钟和采样时间

解决方案：

- 不使用的 ADC 引脚不要悬空，必要时接确定电平
- 确保模拟输入处于 0～VDDA 范围
- 使用 `GPIO_Mode_AIN`
- 适当增加采样时间，并保证传感器输出阻抗合适

### 问题 6：第二次 DMA 数组转运立即结束但数据未更新

现象：

- 第一次复制正常
- 第二次调用函数时等待循环立即退出
- `DataB` 仍是旧数据或只有部分更新

排查过程：

1. 检查 TC 标志是否在新传输前清除
2. 检查通道是否先关闭再重装 CNDTR
3. 检查当前传输计数器是否重新写入
4. 检查是否在 DMA 尚未完成时修改源数组

解决方案：

- 每次传输前执行“关闭通道 → 重装计数器 → 清除标志 → 使能通道”
- 传输结束后再次清除 `DMA1_FLAG_TC1`
- 若使用中断方式，同时正确清除中断挂起位

---

## 结果展示

> 实验 1：OLED 显示 `DataA` 和 `DataB` 的 SRAM 地址。每次源数组递增后，调用 DMA 传输，目标数组四个元素会一次性更新为与源数组相同的值，而源数组内容不会被清除。✅

> 实验 2：ADC1 按 Rank1～Rank4 连续扫描 PA0～PA3，DMA1 通道 1 自动将每次转换结果写入 `AD_Value[0]`～`AD_Value[3]`。改变电位器、光照、温度或红外反射条件时，对应 OLED 数值持续变化。✅

> CPU 主循环不需要逐通道启动 ADC、轮询 EOC 或手动读取 `ADC_DR`，只需从 SRAM 数组中读取 DMA 已更新的结果。✅

---

## 本节小结

本节通过“数组复制”和“ADC 多通道采集”两个实验，完成了 DMA 从软件触发到硬件请求触发的完整实践。

最重要的两套配置思路是：

```text
SRAM → SRAM：
两端地址自增、8位宽度、普通模式、M2M使能
```

```text
ADC_DR → SRAM数组：
外设地址固定、存储器地址自增、16位宽度、循环模式、M2M失能
```

DMA 的价值不是简单地让程序“更快”，而是把高频、重复、规则的数据搬运工作交给专用硬件，让 CPU 专注于控制逻辑、算法和人机交互。

本节也是后续学习以下内容的重要基础：

- USART + DMA 收发
- SPI + DMA 高速传输
- ADC 定时触发 + DMA 固定采样率采集
- DMA 半传输/完成中断
- 双缓冲与环形缓冲
- 音频、波形和多传感器数据采集

---

## 资料核对

- 江协科技《STM32入门教程-2023版》选集：[8-2] DMA数据转运 & DMA+AD多通道
- STMicroelectronics《RM0008 STM32F10xxx Reference Manual》
- STMicroelectronics《AN2548 Introduction to DMA controller for STM32 MCUs》
- STM32F10x Standard Peripheral Library V3.5.x 接口体系

# 11 — SPI通信：硬件SPI读写W25Q64

> 资料说明：未能直接获取该视频的完整字幕与画面。以下内容依据分集标题、课程上下文、STM32F103 官方资料、W25Q64 数据手册及标准外设库的典型用法整理。核心代码属于与课程内容一致的参考实现，不标称为视频逐字源码。

## 实验概述

本实验基于 STM32F103C8T6 标准外设库，使用 STM32 内部的 **SPI1 硬件外设**驱动 W25Q64 串行 NOR Flash，实现以下功能：

1. 初始化 SPI1 主机和 W25Q64 片选引脚。
2. 通过 `0x9F` 指令读取 JEDEC ID，验证 SPI 通信是否正常。
3. 擦除指定的 4KB 扇区。
4. 使用页编程指令向 Flash 写入测试数据。
5. 从同一地址读回数据，并通过 OLED 显示读写结果。
6. 对比软件 SPI 与硬件 SPI 的实现方式、执行效率和可扩展性。

本节的核心目标是掌握硬件 SPI 的完整使用流程：

> 开启时钟 → 配置 GPIO 复用功能 → 初始化 SPI 外设 → 使能 SPI → 手动控制片选 → 通过发送与接收数据寄存器交换字节 → 封装 W25Q64 指令

与上一节软件 SPI 相比，W25Q64 的上层指令和读写流程基本不变，主要变化集中在底层字节交换函数：

```text
软件 SPI：程序逐次翻转 SCK、写 MOSI、读取 MISO
硬件 SPI：程序写入数据寄存器，由 SPI 外设自动产生时钟并完成移位
```

## 硬件连接

本实验使用 SPI1 默认引脚连接 W25Q64。具体接线以开发板原理图和模块丝印为准。

| 器件/信号 | STM32F103C8T6 引脚 | W25Q64 引脚 | 说明 |
|---|---:|---:|---|
| SPI1_SCK | PA5 | CLK | 硬件 SPI 时钟输出 |
| SPI1_MISO | PA6 | DO / IO1 | Flash 向 STM32 返回数据 |
| SPI1_MOSI | PA7 | DI / IO0 | STM32 向 Flash 发送数据 |
| 软件片选 CS | PA4 | /CS | 低电平选中 W25Q64 |
| 电源 | 3.3V | VCC | W25Q64 使用 3.3V 供电 |
| 地 | GND | GND | 通信双方必须共地 |
| 写保护 | 3.3V | /WP / IO2 | 标准 SPI 模式下保持高电平 |
| 保持 | 3.3V | /HOLD / IO3 | 标准 SPI 模式下保持高电平 |
| OLED SCL | PB8 | SCL | 用于显示调试信息，具体以 OLED 驱动为准 |
| OLED SDA | PB9 | SDA | 用于显示调试信息，具体以 OLED 驱动为准 |
| ST-Link V2 | PA13、PA14、GND、3.3V | — | SWD 下载与调试 |

> 注意：PA4 在本实验中作为普通 GPIO 手动控制 W25Q64 的 `/CS`。SPI1 虽配置为软件管理 NSS，但软件 NSS 与外部 Flash 的片选控制不是同一件事。

## 工程文件结构

建议工程目录按下面方式组织：

```text
SPI-Hardware-W25Q64
├── Code
│   └── main.c
├── Hardware
│   ├── MySPI.c
│   ├── MySPI.h
│   ├── W25Q64.c
│   ├── W25Q64.h
│   ├── W25Q64_Ins.h
│   ├── OLED.c
│   └── OLED.h
├── Hardware_Diagram
│   └── SPI1_W25Q64_Connection.png
├── Results
│   └── W25Q64_Read_Write_Test.gif
└── README.md
```

模块职责如下：

```text
MySPI.c / MySPI.h       → 配置 SPI1、控制片选、交换一个字节
W25Q64.c / W25Q64.h     → 封装读 ID、写使能、忙等待、擦除、编程和读取
W25Q64_Ins.h            → 集中保存 W25Q64 常用指令码
main.c                  → 执行读写测试，并通过 OLED 显示结果
```

## 核心代码

### 1. MySPI.h

```c
#ifndef __MYSPI_H                                          // 判断通信层头文件是否尚未定义，避免同一头文件被重复展开
#define __MYSPI_H                                          // 定义头文件保护宏，使后续重复包含不会产生重复声明

#include "stm32f10x.h"                                     // 引入 STM32F10x 设备定义、标准外设库数据类型和外设寄存器声明

void MySPI_Init(void);                                     // 声明硬件 SPI 初始化函数，用于配置 GPIO、SPI1 和默认片选状态
void MySPI_Start(void);                                    // 声明通信开始函数，通过拉低 PA4 选中 W25Q64
void MySPI_Stop(void);                                     // 声明通信结束函数，通过拉高 PA4 释放 W25Q64
uint8_t MySPI_SwapByte(uint8_t ByteSend);                  // 声明全双工字节交换函数，发送一个字节并返回同时接收到的字节

#endif                                                     // 结束头文件保护条件编译区域
```

### 2. MySPI.c

```c
#include "stm32f10x.h"                                     // 引入 STM32F10x 标准外设库总头文件，提供 RCC、GPIO 和 SPI 接口
#include "MySPI.h"                                         // 引入本模块函数声明，确保定义与外部调用接口保持一致

static void MySPI_W_SS(uint8_t BitValue)                   // 定义内部片选写函数，统一封装 PA4 的高低电平控制
{                                                          // 进入片选写函数体
    GPIO_WriteBit(GPIOA, GPIO_Pin_4,                       // 调用标准库函数修改 PA4 输出电平，将 PA4 用作 W25Q64 的外部片选
                  (BitAction)BitValue);                    // 把 0 或 1 转换为 BitAction 枚举值，分别对应复位或置位 GPIO
}                                                          // 结束片选写函数

void MySPI_Init(void)                                      // 定义 SPI1 硬件通信初始化函数
{                                                          // 进入初始化函数体
    GPIO_InitTypeDef GPIO_InitStructure;                   // 定义 GPIO 初始化结构体，用于配置片选、时钟和数据引脚
    SPI_InitTypeDef SPI_InitStructure;                     // 定义 SPI 初始化结构体，用于设置主从模式、时序和分频参数

    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA, ENABLE);  // 开启 GPIOA 时钟，使 PA4、PA5、PA6、PA7 的配置寄存器能够工作
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_SPI1, ENABLE);   // 开启 SPI1 外设时钟，使 SPI1 的移位器、波特率和状态逻辑开始工作

    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_Out_PP;       // 将片选引脚配置为通用推挽输出，以便主动输出高电平和低电平
    GPIO_InitStructure.GPIO_Pin = GPIO_Pin_4;              // 选择 PA4 作为 W25Q64 的软件控制片选引脚
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;      // 设置 PA4 输出速度等级，满足片选信号边沿需求
    GPIO_Init(GPIOA, &GPIO_InitStructure);                 // 将片选 GPIO 配置写入 GPIOA 对应寄存器

    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_AF_PP;        // 将时钟和主机输出数据引脚配置为复用推挽输出，由 SPI1 外设接管
    GPIO_InitStructure.GPIO_Pin = GPIO_Pin_5 | GPIO_Pin_7; // 同时选择 PA5 作为 SCK、PA7 作为 MOSI
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;      // 设置复用输出速度等级，为 SPI 时钟和数据边沿提供足够带宽
    GPIO_Init(GPIOA, &GPIO_InitStructure);                 // 将 PA5 和 PA7 的复用推挽配置写入 GPIOA

    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_IPU;          // 将 PA6 配置为上拉输入，用于接收 W25Q64 从 MISO 返回的数据
    GPIO_InitStructure.GPIO_Pin = GPIO_Pin_6;              // 选择 PA6 作为 SPI1_MISO 输入引脚
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;      // 填写速度字段以保持结构体完整，输入模式下该字段不决定采样频率
    GPIO_Init(GPIOA, &GPIO_InitStructure);                 // 将 PA6 输入配置写入 GPIOA

    SPI_InitStructure.SPI_Direction = SPI_Direction_2Lines_FullDuplex; // 配置双线全双工模式，使 MOSI 和 MISO 可同时移位
    SPI_InitStructure.SPI_Mode = SPI_Mode_Master;          // 将 STM32 的 SPI1 配置为主机，由主机主动产生 SCK 时钟
    SPI_InitStructure.SPI_DataSize = SPI_DataSize_8b;      // 设置每一帧为 8 位，匹配 W25Q64 按字节传输的标准 SPI 指令
    SPI_InitStructure.SPI_CPOL = SPI_CPOL_Low;             // 设置时钟空闲状态为低电平，对应 SPI 模式 0 的 CPOL=0
    SPI_InitStructure.SPI_CPHA = SPI_CPHA_1Edge;           // 设置第一个有效边沿采样数据，对应 SPI 模式 0 的 CPHA=0
    SPI_InitStructure.SPI_NSS = SPI_NSS_Soft;              // 使用软件管理内部 NSS，避免依赖硬件 NSS 引脚进行主机模式判定
    SPI_InitStructure.SPI_BaudRatePrescaler = SPI_BaudRatePrescaler_128; // 将 SPI1 外设时钟分频，使用较低速率提高面包板实验稳定性
    SPI_InitStructure.SPI_FirstBit = SPI_FirstBit_MSB;     // 设置高位先发送，符合 W25Q64 指令、地址和数据的位序要求
    SPI_InitStructure.SPI_CRCPolynomial = 7;               // 填写 CRC 多项式默认值，本实验未启用 SPI 硬件 CRC 功能
    SPI_Init(SPI1, &SPI_InitStructure);                    // 把上述参数写入 SPI1 控制寄存器，完成 SPI 工作模式配置

    SPI_NSSInternalSoftwareConfig(SPI1, SPI_NSSInternalSoft_Set); // 将内部 SSI 位置 1，保证软件 NSS 主机模式下不会触发模式错误
    SPI_Cmd(SPI1, ENABLE);                                 // 置位 SPE，使能 SPI1 外设并允许其执行后续字节交换
    MySPI_W_SS(1);                                         // 初始化完成后把外部片选拉高，确保 W25Q64 处于未选中状态
}                                                          // 结束 SPI 初始化函数

void MySPI_Start(void)                                     // 定义一次 W25Q64 指令帧的开始函数
{                                                          // 进入通信开始函数体
    MySPI_W_SS(0);                                         // 将 PA4 拉低，选中 W25Q64 并允许其解析后续时钟和数据
}                                                          // 结束通信开始函数

void MySPI_Stop(void)                                      // 定义一次 W25Q64 指令帧的结束函数
{                                                          // 进入通信结束函数体
    MySPI_W_SS(1);                                         // 将 PA4 拉高，结束当前指令帧并让 W25Q64 执行已接收的命令
}                                                          // 结束通信结束函数

uint8_t MySPI_SwapByte(uint8_t ByteSend)                   // 定义硬件 SPI 全双工交换函数
{                                                          // 进入字节交换函数体
    while (SPI_I2S_GetFlagStatus(SPI1, SPI_I2S_FLAG_TXE) == RESET) // 等待发送数据寄存器为空，避免覆盖尚未装入移位器的数据
    {                                                      // 进入 TXE 轮询等待循环
    }                                                      // TXE 置位后退出循环，表示可以写入下一个待发送字节

    SPI_I2S_SendData(SPI1, ByteSend);                      // 把待发送字节写入 SPI1 数据寄存器，硬件随后自动产生 8 个时钟脉冲

    while (SPI_I2S_GetFlagStatus(SPI1, SPI_I2S_FLAG_RXNE) == RESET) // 等待接收数据寄存器非空，确保一个完整字节已经交换完成
    {                                                      // 进入 RXNE 轮询等待循环
    }                                                      // RXNE 置位后退出循环，表示返回字节已经可以读取

    return (uint8_t)SPI_I2S_ReceiveData(SPI1);             // 读取并返回接收寄存器低 8 位，同时完成 RXNE 标志的正常清除流程
}                                                          // 结束硬件 SPI 字节交换函数
```

### 3. W25Q64_Ins.h

```c
#ifndef __W25Q64_INS_H                                     // 判断 W25Q64 指令头文件是否尚未定义，防止宏被重复声明
#define __W25Q64_INS_H                                     // 定义指令头文件保护宏

#define W25Q64_WRITE_ENABLE              0x06              // 定义写使能指令，执行擦除或编程前必须先将 WEL 位置 1
#define W25Q64_WRITE_DISABLE             0x04              // 定义写禁止指令，用于主动清除写使能锁存状态
#define W25Q64_READ_STATUS_REGISTER_1    0x05              // 定义状态寄存器 1 读取指令，其中最低位 BUSY 表示器件忙状态
#define W25Q64_PAGE_PROGRAM              0x02              // 定义页编程指令，用于在单个 256 字节页内写入数据
#define W25Q64_SECTOR_ERASE_4KB          0x20              // 定义 4KB 扇区擦除指令，地址位于目标扇区内即可
#define W25Q64_JEDEC_ID                  0x9F              // 定义 JEDEC ID 读取指令，可依次获得厂商、类型和容量代码
#define W25Q64_READ_DATA                 0x03              // 定义普通数据读取指令，发送 24 位地址后可连续读取数据
#define W25Q64_DUMMY_BYTE                0xFF              // 定义读取阶段发送的占位字节，用于产生时钟而不携带有效命令

#endif                                                     // 结束 W25Q64 指令头文件保护区域
```

### 4. W25Q64.h

```c
#ifndef __W25Q64_H                                         // 判断 W25Q64 驱动头文件是否尚未定义，避免重复包含
#define __W25Q64_H                                         // 定义 W25Q64 驱动头文件保护宏

#include "stm32f10x.h"                                     // 引入固定宽度整数类型和 STM32F10x 基础定义

void W25Q64_Init(void);                                    // 声明 W25Q64 初始化函数，内部完成硬件 SPI 通信层初始化
void W25Q64_ReadID(uint8_t *MID, uint16_t *DID);           // 声明 JEDEC ID 读取函数，分别返回厂商 ID 和 16 位器件 ID
void W25Q64_PageProgram(uint32_t Address,                  // 声明页编程函数的起始地址参数，W25Q64 使用低 24 位地址
                         uint8_t *DataArray,               // 声明待写入数据缓冲区指针，数据从该数组依次发送
                         uint16_t Count);                  // 声明本次写入字节数，必须满足单页和页边界限制
void W25Q64_SectorErase(uint32_t Address);                 // 声明 4KB 扇区擦除函数，传入扇区内任意地址即可定位目标扇区
void W25Q64_ReadData(uint32_t Address,                     // 声明连续读取函数的起始地址参数
                      uint8_t *DataArray,                  // 声明接收缓冲区指针，读取结果依次保存到该数组
                      uint32_t Count);                     // 声明需要连续读取的总字节数

#endif                                                     // 结束 W25Q64 驱动头文件保护区域
```

### 5. W25Q64.c

```c
#include "stm32f10x.h"                                     // 引入 STM32F10x 基础类型和标准外设库定义
#include "MySPI.h"                                         // 引入底层硬件 SPI 初始化、片选和字节交换接口
#include "W25Q64.h"                                        // 引入 W25Q64 对外函数声明，确保接口定义保持一致
#include "W25Q64_Ins.h"                                    // 引入 W25Q64 指令码宏，避免在驱动代码中直接散落魔法数字

void W25Q64_Init(void)                                     // 定义 W25Q64 驱动初始化函数
{                                                          // 进入 W25Q64 初始化函数体
    MySPI_Init();                                          // 初始化 SPI1 和片选 GPIO，为后续 Flash 指令通信建立底层通道
}                                                          // 结束 W25Q64 初始化函数

void W25Q64_ReadID(uint8_t *MID, uint16_t *DID)            // 定义 JEDEC ID 读取函数，用于检查芯片身份和总线连通性
{                                                          // 进入读取 ID 函数体
    MySPI_Start();                                         // 拉低片选，开始一帧独立的 JEDEC ID 读取指令
    MySPI_SwapByte(W25Q64_JEDEC_ID);                       // 发送 0x9F 指令，通知 W25Q64 输出 JEDEC 标识信息
    *MID = MySPI_SwapByte(W25Q64_DUMMY_BYTE);              // 发送占位字节产生时钟，并接收 8 位厂商 ID
    *DID = MySPI_SwapByte(W25Q64_DUMMY_BYTE);              // 接收器件 ID 的高 8 位，通常表示存储器类型
    *DID <<= 8;                                            // 将已接收的高字节左移到 16 位结果的高半部分
    *DID |= MySPI_SwapByte(W25Q64_DUMMY_BYTE);             // 接收容量代码并合并到器件 ID 的低 8 位
    MySPI_Stop();                                          // 拉高片选，结束 JEDEC ID 读取指令帧
}                                                          // 结束读取 ID 函数

static void W25Q64_WriteEnable(void)                       // 定义仅供本文件使用的写使能辅助函数
{                                                          // 进入写使能函数体
    MySPI_Start();                                         // 拉低片选，开始写使能命令帧
    MySPI_SwapByte(W25Q64_WRITE_ENABLE);                   // 发送 0x06 指令，将状态寄存器中的 WEL 写使能锁存位置 1
    MySPI_Stop();                                          // 拉高片选，使写使能命令正式结束并生效
}                                                          // 结束写使能函数

static void W25Q64_WaitBusy(void)                          // 定义仅供本文件使用的忙状态轮询函数
{                                                          // 进入忙状态等待函数体
    uint32_t Timeout;                                      // 定义软件超时计数器，避免器件异常时永久阻塞在等待循环
    uint8_t StatusRegister;                                // 定义状态寄存器缓存变量，用于保存每次读取到的 BUSY 状态
    Timeout = 1000000;                                     // 设置足够大的轮询次数，实际工程应结合主频和最坏擦除时间调整

    MySPI_Start();                                         // 拉低片选，开始连续读取状态寄存器 1 的命令帧
    MySPI_SwapByte(W25Q64_READ_STATUS_REGISTER_1);         // 发送 0x05 指令，使 W25Q64 后续持续输出状态寄存器 1

    do                                                     // 至少读取一次状态寄存器，然后根据 BUSY 位决定是否继续等待
    {                                                      // 进入忙状态轮询循环体
        StatusRegister = MySPI_SwapByte(W25Q64_DUMMY_BYTE); // 发送占位字节产生时钟，并读取当前状态寄存器 1
        Timeout--;                                         // 每轮查询减少超时计数，防止总线故障导致程序无限卡死
    } while (((StatusRegister & 0x01) == 0x01) &&          // 当 BUSY 位为 1 时说明擦除或编程仍未完成，需要继续轮询
             (Timeout > 0));                               // 当超时计数耗尽时强制退出，给上层保留故障处理机会

    MySPI_Stop();                                          // 拉高片选，结束状态寄存器连续读取命令帧
}                                                          // 结束忙状态等待函数

void W25Q64_PageProgram(uint32_t Address,                  // 定义页编程函数并接收 24 位目标起始地址
                         uint8_t *DataArray,               // 接收待写入数据数组首地址
                         uint16_t Count)                   // 接收本次写入字节数，调用者应保证不超过 256 字节且不跨页
{                                                          // 进入页编程函数体
    uint16_t i;                                            // 定义循环变量，用于依次发送待写入的每个数据字节

    W25Q64_WriteEnable();                                  // 在页编程前发送写使能指令，否则 W25Q64 会忽略编程命令

    MySPI_Start();                                         // 拉低片选，开始页编程指令帧
    MySPI_SwapByte(W25Q64_PAGE_PROGRAM);                   // 发送 0x02 页编程指令
    MySPI_SwapByte((uint8_t)(Address >> 16));              // 发送 24 位地址的最高字节 A23～A16
    MySPI_SwapByte((uint8_t)(Address >> 8));               // 发送 24 位地址的中间字节 A15～A8
    MySPI_SwapByte((uint8_t)Address);                      // 发送 24 位地址的最低字节 A7～A0

    for (i = 0; i < Count; i++)                            // 遍历待写入缓冲区，将 Count 个字节顺序装入页缓存
    {                                                      // 进入页数据发送循环体
        MySPI_SwapByte(DataArray[i]);                      // 发送当前数据字节，返回值在写入阶段不需要使用
    }                                                      // 完成全部待写入字节的发送

    MySPI_Stop();                                          // 拉高片选，提交页编程操作并让 W25Q64 开始内部写入
    W25Q64_WaitBusy();                                     // 轮询 BUSY 位，等待内部页编程真正完成后再返回
}                                                          // 结束页编程函数

void W25Q64_SectorErase(uint32_t Address)                  // 定义 4KB 扇区擦除函数并接收目标地址
{                                                          // 进入扇区擦除函数体
    W25Q64_WriteEnable();                                  // 擦除属于写操作，必须先将 WEL 写使能锁存位置 1

    MySPI_Start();                                         // 拉低片选，开始扇区擦除指令帧
    MySPI_SwapByte(W25Q64_SECTOR_ERASE_4KB);               // 发送 0x20 指令，请求擦除地址所在的 4KB 扇区
    MySPI_SwapByte((uint8_t)(Address >> 16));              // 发送目标地址的最高字节 A23～A16
    MySPI_SwapByte((uint8_t)(Address >> 8));               // 发送目标地址的中间字节 A15～A8
    MySPI_SwapByte((uint8_t)Address);                      // 发送目标地址的最低字节 A7～A0
    MySPI_Stop();                                          // 拉高片选，提交擦除命令并启动 Flash 内部擦除流程

    W25Q64_WaitBusy();                                     // 轮询状态寄存器 BUSY 位，等待扇区擦除完成
}                                                          // 结束扇区擦除函数

void W25Q64_ReadData(uint32_t Address,                     // 定义普通连续读取函数并接收起始地址
                      uint8_t *DataArray,                  // 接收用于保存读取结果的目标数组首地址
                      uint32_t Count)                      // 接收需要连续读取的数据总字节数
{                                                          // 进入数据读取函数体
    uint32_t i;                                            // 定义循环变量，用于按顺序接收多个数据字节

    MySPI_Start();                                         // 拉低片选，开始普通读取数据指令帧
    MySPI_SwapByte(W25Q64_READ_DATA);                      // 发送 0x03 普通读取指令，该指令在低速下不需要额外空时钟
    MySPI_SwapByte((uint8_t)(Address >> 16));              // 发送读取起始地址的最高字节 A23～A16
    MySPI_SwapByte((uint8_t)(Address >> 8));               // 发送读取起始地址的中间字节 A15～A8
    MySPI_SwapByte((uint8_t)Address);                      // 发送读取起始地址的最低字节 A7～A0

    for (i = 0; i < Count; i++)                            // 按地址自动递增规则连续读取 Count 个字节
    {                                                      // 进入连续读取循环体
        DataArray[i] = MySPI_SwapByte(W25Q64_DUMMY_BYTE);  // 发送占位数据产生时钟，并保存 W25Q64 同步返回的有效字节
    }                                                      // 完成全部目标数据的接收

    MySPI_Stop();                                          // 拉高片选，结束本次连续读取指令帧
}                                                          // 结束普通数据读取函数
```

### 6. main.c

```c
#include "stm32f10x.h"                                     // 引入 STM32F10x 标准外设库基础定义
#include <string.h>                                        // 引入 memcmp 函数，用于比较写入数组和读回数组
#include "OLED.h"                                          // 引入 OLED 显示接口，用于展示芯片 ID 和读写测试结果
#include "W25Q64.h"                                        // 引入 W25Q64 初始化、擦除、编程和读取接口

uint8_t MID;                                               // 定义 8 位厂商 ID 变量，用于保存 JEDEC ID 的第一个字节
uint16_t DID;                                              // 定义 16 位器件 ID 变量，用于保存存储器类型和容量代码
uint8_t DataArrayWrite[] = {0x01, 0x02, 0x03, 0x04};       // 定义四字节测试数据，作为页编程操作的数据源
uint8_t DataArrayRead[4];                                  // 定义四字节接收数组，用于保存从 Flash 同一地址读回的数据

int main(void)                                             // 定义程序入口函数，完成 W25Q64 硬件 SPI 读写验证
{                                                          // 进入主函数体
    OLED_Init();                                           // 初始化 OLED 调试显示模块
    W25Q64_Init();                                         // 初始化 SPI1、片选 GPIO 和 W25Q64 驱动通信层

    W25Q64_ReadID(&MID, &DID);                             // 读取 W25Q64 的 JEDEC 厂商 ID、存储器类型和容量代码
    OLED_ShowString(1, 1, "MID:");                         // 在 OLED 第 1 行显示厂商 ID 标签
    OLED_ShowHexNum(1, 5, MID, 2);                         // 以两位十六进制形式显示 8 位厂商 ID
    OLED_ShowString(1, 8, "DID:");                         // 在同一行后半部分显示器件 ID 标签
    OLED_ShowHexNum(1, 12, DID, 4);                        // 以四位十六进制形式显示 16 位器件 ID

    W25Q64_SectorErase(0x000000);                          // 擦除地址 0x000000 所在的首个 4KB 扇区，将目标位恢复为 1
    W25Q64_PageProgram(0x000000, DataArrayWrite, 4);       // 从地址 0x000000 开始写入四个测试字节
    W25Q64_ReadData(0x000000, DataArrayRead, 4);           // 从相同地址读回四个字节并保存到接收数组

    OLED_ShowString(2, 1, "W:");                           // 在 OLED 第 2 行显示写入数据标签
    OLED_ShowHexNum(2, 3, DataArrayWrite[0], 2);           // 显示写入数组第 1 个字节
    OLED_ShowHexNum(2, 5, DataArrayWrite[1], 2);           // 显示写入数组第 2 个字节
    OLED_ShowHexNum(2, 7, DataArrayWrite[2], 2);           // 显示写入数组第 3 个字节
    OLED_ShowHexNum(2, 9, DataArrayWrite[3], 2);           // 显示写入数组第 4 个字节

    OLED_ShowString(3, 1, "R:");                           // 在 OLED 第 3 行显示读回数据标签
    OLED_ShowHexNum(3, 3, DataArrayRead[0], 2);            // 显示读回数组第 1 个字节
    OLED_ShowHexNum(3, 5, DataArrayRead[1], 2);            // 显示读回数组第 2 个字节
    OLED_ShowHexNum(3, 7, DataArrayRead[2], 2);            // 显示读回数组第 3 个字节
    OLED_ShowHexNum(3, 9, DataArrayRead[3], 2);            // 显示读回数组第 4 个字节

    if (memcmp(DataArrayWrite, DataArrayRead, 4) == 0)     // 比较写入与读回的四字节内容，判断 Flash 读写是否一致
    {                                                      // 进入数据一致分支
        OLED_ShowString(4, 1, "VERIFY:PASS");              // 显示校验通过，说明本次擦除、编程和读取流程正常
    }                                                      // 结束数据一致分支
    else                                                   // 当任意字节不一致时进入失败分支
    {                                                      // 进入数据不一致分支
        OLED_ShowString(4, 1, "VERIFY:FAIL");              // 显示校验失败，提示检查时序、擦除、接线和地址参数
    }                                                      // 结束数据不一致分支

    while (1)                                              // 进入主循环，使显示结果持续保留并为后续功能扩展提供框架
    {                                                      // 进入无限循环体
    }                                                      // 当前示例无需周期任务，因此保持空循环
}                                                          // 结束主函数
```

> 说明：上面的 `W25Q64_PageProgram()` 是基础教学版本。调用者必须保证 `Count <= 256`，并且 `Address` 到最后一个数据字节不能跨越 256 字节页边界。正式项目应在上层封装自动分页写入函数。

## 代码要点

| 行/段 | 说明 |
|---|---|
| `RCC_APB2PeriphClockCmd(RCC_APB2Periph_SPI1, ENABLE)` | SPI1 挂载在 APB2 总线上，使用前必须开启 SPI1 外设时钟。 |
| `GPIO_Mode_AF_PP` | PA5/SCK 和 PA7/MOSI 由 SPI1 外设主动驱动，应配置为复用推挽输出。 |
| `GPIO_Mode_IPU` | PA6/MISO 作为主机输入，接收 W25Q64 返回的数据；上拉可避免从机未选中时输入悬空。 |
| `SPI_Direction_2Lines_FullDuplex` | 使用独立 MOSI、MISO 的双线全双工结构，发送一个字节时也会同步接收一个字节。 |
| `SPI_CPOL_Low` + `SPI_CPHA_1Edge` | 构成 SPI 模式 0：SCK 空闲为低电平，在第一个有效边沿采样。 |
| `SPI_NSS_Soft` | SPI1 的内部 NSS 由软件管理；外部 W25Q64 `/CS` 仍由 PA4 手动控制。 |
| `SPI_NSSInternalSoftwareConfig()` | 将内部 SSI 置为高电平，维持主机身份并避免因内部 NSS 为低而触发 MODF。 |
| `SPI_BaudRatePrescaler_128` | 降低初始 SPI 时钟，适合杜邦线和面包板环境；通信稳定后可逐步提高速度。 |
| `SPI_I2S_FLAG_TXE` | 表示发送数据寄存器为空，可以写入下一个待发送字节，但不等于整帧已完全结束。 |
| `SPI_I2S_FLAG_RXNE` | 表示接收数据寄存器中已有完整字节，读取数据寄存器后该标志正常清除。 |
| `MySPI_SwapByte()` | 把 SPI 的全双工特性封装成“发一个字节、收一个字节”的统一接口。 |
| `W25Q64_WRITE_ENABLE` | 擦除和编程前必须执行写使能，否则 W25Q64 会忽略写操作。 |
| `W25Q64_READ_STATUS_REGISTER_1` | 状态寄存器 1 的 bit0 为 BUSY，擦除或编程期间必须等待其清零。 |
| `W25Q64_PAGE_PROGRAM` | 单次最多向一个 256 字节页内编程，跨页会在当前页内回卷覆盖。 |
| `W25Q64_SECTOR_ERASE_4KB` | 将目标 4KB 扇区擦除为 `0xFF`，是重新写入任意数据前常用的最小擦除操作。 |
| `W25Q64_JEDEC_ID` | 通过 `0x9F` 读取厂商、存储器类型和容量代码，是最常用的通信自检方法。 |
| `memcmp()` | 对写入数组和读回数组做逐字节比较，比只观察单个字节更可靠。 |

## 关键知识点

### 1. STM32 硬件 SPI 的数据通路

#### 原理

STM32 的 SPI 外设内部包含控制逻辑、波特率分频器、发送缓冲、接收缓冲和移位寄存器。

CPU 向数据寄存器写入一个字节后，SPI 外设会自动完成：

```text
装载发送移位寄存器
→ 根据 CPOL/CPHA 自动翻转 SCK
→ 按位从 MOSI 移出数据
→ 同时从 MISO 移入数据
→ 将接收结果放入数据寄存器
→ 置位 RXNE
```

因此，硬件 SPI 不需要程序逐次控制 SCK，也不需要在每一位之间插入软件延时。

#### 特点

- 时钟由专用外设自动产生，波形更加稳定。
- CPU 只需按字节读写数据寄存器。
- 通信速度通常明显高于 GPIO 模拟 SPI。
- 可以配合中断或 DMA 进一步减少 CPU 占用。
- CPOL、CPHA、位序和分频均由寄存器统一配置。

#### 面试易问

**Q：硬件 SPI 为什么比软件 SPI 快？**

A：硬件 SPI 使用片上移位器和时钟生成电路自动完成逐位传输，CPU 不需要反复执行 GPIO 写入、读取和延时指令，因此有效吞吐率更高，时序也更稳定。

**Q：使用硬件 SPI 后，CPU 是否完全不参与通信？**

A：轮询方式下 CPU 仍需等待标志位、写入发送数据和读取接收数据；配合中断或 DMA 后，CPU 占用可以进一步降低。

#### 易错点

- 只开启 GPIO 时钟，没有开启 SPI1 外设时钟。
- SCK、MOSI 未配置为复用推挽输出。
- SPI 参数与从机时序模式不匹配。
- 写入数据寄存器后未读取接收寄存器，导致后续出现溢出问题。
- 为追求速度直接使用过高时钟，忽略面包板、杜邦线和模块质量。

---

### 2. SPI 模式 0：CPOL 与 CPHA

#### 原理

SPI 的时钟时序由 CPOL 和 CPHA 两个位共同决定。

本实验使用：

```text
CPOL = 0：SCK 空闲时保持低电平
CPHA = 0：接收端在第一个有效边沿采样数据
```

在 STM32 标准外设库中，对应：

```c
SPI_InitStructure.SPI_CPOL = SPI_CPOL_Low;             // 设置 SCK 空闲为低电平，对应模式 0 的 CPOL=0
SPI_InitStructure.SPI_CPHA = SPI_CPHA_1Edge;           // 设置在第一个有效边沿采样，对应模式 0 的 CPHA=0
```

W25Q64 标准 SPI 支持模式 0 和模式 3，本实验选择模式 0。

#### 特点

- SCK 在未通信时保持低电平。
- 数据通常在下降沿附近改变，在上升沿采样。
- 主机和从机必须对采样边沿保持一致理解。
- CPOL、CPHA 配错时，常出现位移、固定错误或 ID 异常。

#### 面试易问

**Q：SPI 一共有几种模式？**

A：由 CPOL 和 CPHA 的四种组合形成模式 0、1、2、3。主机配置必须与从机支持的模式一致。

**Q：CPHA=0 和 CPHA=1 的核心区别是什么？**

A：核心区别是接收端在第一个有效边沿还是第二个有效边沿采样，同时也会影响首位数据需要在何时准备好。

#### 易错点

- 把库函数中的 `SPI_CPHA_1Edge` 误解为 CPHA=1；它实际表示第一个边沿采样，对应 CPHA=0。
- 只看 CPOL，不检查 CPHA。
- 修改模式后没有重新观察逻辑分析仪波形。
- 主机和从机模式不一致时仍继续排查上层协议。

---

### 3. 全双工交换与 `MySPI_SwapByte()`

#### 原理

SPI 每产生一个时钟脉冲，主机和从机都会同时移出一位并移入一位。

因此，发送 8 位数据的同时，也必然接收到 8 位数据：

```text
发送命令阶段：关心 MOSI，通常忽略 MISO 返回值
读取数据阶段：发送 Dummy Byte 产生时钟，关心 MISO 返回值
```

`MySPI_SwapByte()` 把这种特性封装成统一接口：

```text
等待 TXE → 写入发送字节 → 等待 RXNE → 读取返回字节
```

#### 特点

- 同一个函数既能用于发送，也能用于接收。
- 读取阶段必须发送无意义的占位字节来产生 SCK。
- 每次发送后都读取接收数据，可维持收发状态同步。
- 上层 W25Q64 驱动无需关心寄存器细节。

#### 面试易问

**Q：为什么 SPI 读取数据时还要发送一个字节？**

A：SPI 时钟由主机产生。从机只有在主机继续输出时钟时才能移出数据，因此主机必须发送 Dummy Byte 来换取相同位数的接收时钟。

**Q：为什么不能只等待 TXE 就认为交换完成？**

A：TXE 只说明发送数据寄存器已经空，可以装入新数据；完整字节是否接收完成应检查 RXNE，整条总线是否完全空闲则还要结合 BSY。

#### 易错点

- 读取时不发送 Dummy Byte，导致没有时钟。
- 只写数据寄存器，不读接收数据寄存器。
- 把发送阶段的无效返回值当成有效数据。
- 忽略 RXNE，过早读取数据寄存器。

---

### 4. TXE、RXNE 与 BSY 标志

#### 原理

SPI 常用状态标志含义不同：

| 标志 | 含义 | 典型用途 |
|---|---|---|
| TXE | 发送数据寄存器为空 | 判断是否能写入下一个字节 |
| RXNE | 接收数据寄存器非空 | 判断是否能读取完整接收字节 |
| BSY | SPI 正在忙 | 关闭 SPI、切换模式或结束关键操作前确认总线空闲 |

对于逐字节交换，等待 `RXNE` 通常意味着该字节的时钟已经完成。若要关闭 SPI、进入低功耗或改变外设配置，应进一步等待 `BSY=0`。

#### 特点

- TXE 可以在最后一位仍在移位时提前置位。
- RXNE 对应接收缓冲中已有数据。
- BSY 反映 SPI 通信状态机是否仍处于活动状态。
- 不同场景需要组合使用不同标志。

#### 面试易问

**Q：TXE 置位是否表示数据已经完全发送到引脚？**

A：不一定。TXE 只表示发送数据寄存器可以接收新数据，最后一个字节可能仍在移位器中发送。需要确认整个通信结束时，应继续检查 BSY。

**Q：读取 DR 为什么重要？**

A：读取 DR 能取走接收字节并配合硬件清除 RXNE。持续发送却不读取接收数据，可能产生接收溢出 OVR。

#### 易错点

- 用 TXE 替代 RXNE 判断本次字节交换完成。
- 最后一个字节后立即关闭 SPI，未等待 BSY 清零。
- 忽略接收数据导致 OVR。
- 在调试中直接修改寄存器，却未按参考手册要求清除错误标志。

---

### 5. 复用推挽、MISO 输入与软件 NSS

#### 原理

SPI1 的 SCK 和 MOSI 由 SPI 外设输出，所以 PA5、PA7 要配置成复用推挽输出。MISO 是主机输入，所以 PA6 配置为输入模式。

`SPI_NSS_Soft` 表示 SPI 外设内部不再通过硬件 NSS 引脚判断主机选择状态，而由软件设置内部 SSI 位。

外部 W25Q64 的 `/CS` 仍需由普通 GPIO 控制：

```text
内部 NSS：维持 STM32 SPI 主机状态
外部 /CS：决定当前哪个 SPI 从设备响应
```

#### 特点

- SCK/MOSI 使用复用推挽，可由外设高速主动驱动。
- MISO 在从机未选中时通常为高阻态。
- 软件 NSS 适合一个主机手动管理多个从设备。
- 每个从设备可拥有独立 GPIO 片选。

#### 面试易问

**Q：已经配置 `SPI_NSS_Soft`，为什么还要控制 PA4？**

A：软件 NSS 主要服务于 STM32 SPI 外设内部的主从模式管理；PA4 控制的是 W25Q64 实际 `/CS` 引脚，两者职责不同。

**Q：多个 SPI 从机如何共享总线？**

A：SCK、MOSI、MISO 可以共享，每个从机使用独立片选。任一时刻只允许选中一个会驱动 MISO 的从机。

#### 易错点

- 把软件 NSS 当作外部 W25Q64 片选。
- 多个从机同时拉低片选，造成 MISO 冲突。
- 片选未在完整命令期间保持低电平。
- SCK/MOSI 错配为普通推挽输出，导致 SPI 外设无法接管引脚。

---

### 6. W25Q64 指令帧与片选边界

#### 原理

W25Q64 以 `/CS` 的下降沿开始识别一条指令，以 `/CS` 的上升沿结束该指令。

典型读取流程：

```text
/CS 拉低
→ 发送 Read Data 指令
→ 发送 24 位地址
→ 连续交换并接收数据
→ /CS 拉高
```

典型编程流程：

```text
发送 Write Enable 独立指令帧
→ 发送 Page Program + 地址 + 数据
→ /CS 拉高提交编程
→ 轮询 BUSY
```

片选不是简单的“使能电平”，也是 W25Q64 划分命令边界的重要协议信号。

#### 特点

- 一条完整命令期间 `/CS` 必须持续保持低电平。
- 某些写入和擦除操作在 `/CS` 上升沿后才开始内部执行。
- 每个独立指令通常需要重新拉低片选。
- 地址按高字节到低字节顺序发送。

#### 面试易问

**Q：为什么写使能和页编程要分成两个片选帧？**

A：写使能是一条独立命令，用于设置 WEL。片选拉高后该命令结束；随后再开始页编程命令，W25Q64 才会接受写操作。

**Q：片选一直保持低电平是否可以连续发送所有命令？**

A：不可以。W25Q64 依赖片选上升沿结束并提交多数命令，不同指令必须按照数据手册要求划分帧。

#### 易错点

- 在发送地址或数据中途错误拉高片选。
- 写使能后没有结束当前帧，直接继续发送页编程指令。
- 地址字节顺序发送错误。
- 多个指令之间没有形成清晰片选边界。

---

### 7. 写使能、BUSY 与 NOR Flash 的“先擦后写”

#### 原理

W25Q64 是 NOR Flash。编程操作只能把存储位从 1 改为 0，不能直接把 0 改回 1。若要重新写入任意内容，通常需要先擦除，使目标区域恢复为 `0xFF`。

擦除和编程前还必须执行写使能：

```text
Write Enable → WEL=1
Erase / Program → BUSY=1
内部操作完成 → BUSY=0，WEL 通常自动清零
```

#### 特点

- 最小常用擦除单位为 4KB 扇区。
- 页编程单次最多处理 256 字节。
- 擦除耗时通常显著长于读取。
- 状态寄存器 BUSY 位必须被正确轮询。
- Flash 具有有限擦写寿命，频繁更新应考虑磨损均衡。

#### 面试易问

**Q：为什么写入前通常要先擦除？**

A：NOR Flash 编程只能将位从 1 写成 0。要把已经为 0 的位恢复为 1，必须执行擦除操作。

**Q：为什么每次擦除或编程前都要发送 Write Enable？**

A：WEL 是防止误写的重要保护机制。擦除或编程完成后，WEL 通常自动清零，因此下一次写操作必须重新使能。

#### 易错点

- 未发送写使能就执行擦除或编程。
- 固定延时后直接继续操作，没有读取 BUSY。
- 反复擦写同一扇区，忽略 Flash 擦写寿命。
- 认为页编程可以把任意旧数据直接覆盖成新数据。

---

### 8. 页编程边界与连续读取

#### 原理

W25Q64 的页大小为 256 字节。页编程命令从给定地址开始把数据装入页缓存。

若发送数据跨越当前页末尾，地址会在该页内部回卷，而不是自动进入下一页，可能覆盖本页前部数据。

例如：

```text
起始地址：0x0000F0
发送长度：32 字节
当前页剩余：16 字节
结果：后 16 字节可能回卷到 0x000000 附近
```

连续读取没有相同的页边界限制，地址可以自动递增跨越页和扇区。

#### 特点

- 页编程最大长度为 256 字节。
- 实际可写长度还受起始地址页内偏移限制。
- 连续读取可以跨页。
- 通用写函数应自动计算剩余页空间并拆分多次编程。

#### 面试易问

**Q：如何实现任意长度的 W25Q64 写入？**

A：先计算起始地址在当前页中的偏移和剩余空间，将数据拆成多个不跨页的 Page Program 操作，并在每次编程前写使能、编程后等待 BUSY 清零。

**Q：页编程跨页时芯片会自动写到下一页吗？**

A：通常不会，而是发生页内地址回卷，因此必须由软件主动分页。

#### 易错点

- 只检查 `Count <= 256`，却没有检查起始地址偏移。
- 大数组一次性调用页编程函数。
- 跨页后发现前部数据被覆盖，却误判为 SPI 丢字节。
- 把读取无页限制的特性错误套用到编程操作。

---

### 9. 软件 SPI 与硬件 SPI 的区别

#### 原理

软件 SPI 由 CPU 通过 GPIO 指令模拟时序；硬件 SPI 由专用外设自动完成移位和时钟生成。

| 对比项 | 软件 SPI | 硬件 SPI |
|---|---|---|
| 时钟生成 | CPU 翻转 GPIO | SPI 外设自动生成 |
| 引脚选择 | 灵活，可使用多数普通 GPIO | 通常受固定引脚或重映射限制 |
| 速度 | 较低，受代码执行影响 | 较高且波形稳定 |
| CPU 占用 | 高 | 轮询较低，可进一步用中断/DMA |
| 调试直观性 | 每一步可控，适合教学 | 配置项更多，需理解寄存器状态 |
| 扩展能力 | 一般 | 易与中断、DMA、高速传输结合 |

#### 特点

- 软件 SPI 适合引脚受限、速率不高和初学时序验证。
- 硬件 SPI 适合高速传输和正式工程。
- W25Q64 上层驱动可以保持不变，只替换底层字节交换实现。
- 良好的分层设计能降低通信方式切换成本。

#### 面试易问

**Q：什么时候更适合使用软件 SPI？**

A：当硬件 SPI 引脚被占用、需要特殊时序、通信速率不高或需要快速验证协议时，可以使用软件 SPI。

**Q：从软件 SPI 切换到硬件 SPI，为什么 W25Q64 驱动层可以基本不改？**

A：因为设备驱动层只依赖“片选、开始、停止、交换字节”等抽象接口。只要底层接口语义一致，上层指令逻辑无需关心时钟是由 GPIO 还是 SPI 外设产生。

#### 易错点

- 硬件 SPI 与软件 SPI 同时驱动同一组引脚。
- 切换底层后改变了 SPI 模式或位序，却未同步验证。
- 为了追求复用，底层接口设计过度复杂。
- 认为硬件 SPI 一定可靠，从而忽略接线、信号质量和片选时序。

---

## 本节核心记忆

```text
SPI1 默认引脚：
PA5 = SCK
PA6 = MISO
PA7 = MOSI
PA4 = W25Q64 /CS（普通 GPIO 手动控制）
```

```text
硬件 SPI 字节交换：
等待 TXE → 写 DR → 硬件产生 8 个时钟 → 等待 RXNE → 读 DR
```

```text
SPI 模式 0：
CPOL = 0
CPHA = 0
STM32 标准库配置为 SPI_CPOL_Low + SPI_CPHA_1Edge
```

```text
W25Q64 写操作：
Write Enable → Erase / Page Program → Wait BUSY
```

```text
NOR Flash：
编程只能 1 → 0
擦除负责 0 → 1
页大小 256B
扇区大小 4KB
```

```text
软件 NSS 管 STM32 内部主机状态
PA4 片选管外部 W25Q64 是否响应
```

## 开发过程总结

### 问题 1：读取 JEDEC ID 为 `0xFFFFFF` 或固定全 1

现象：

- OLED 显示 `MID=FF`、`DID=FFFF`
- SCK 和 MOSI 可能有波形
- MISO 始终保持高电平

排查过程：

1. 检查 W25Q64 是否使用 3.3V 供电并与 STM32 共地。
2. 检查 PA6 是否确实连接到 W25Q64 的 DO/MISO。
3. 检查 `/CS` 是否在读取期间被拉低。
4. 检查 `/WP` 和 `/HOLD` 是否保持高电平。
5. 检查 W25Q64 模块丝印是否把 DO、DI 方向理解反了。
6. 使用逻辑分析仪确认 `0x9F` 是否按高位先发出。

解决方案：

- 修正 MISO、MOSI 和片选接线。
- 将 PA6 配置为输入模式。
- 确认 PA4 初始化后默认拉高，通信开始时再拉低。
- 降低 SPI 时钟后重新测试。

### 问题 2：SPI1 没有输出 SCK

现象：

- PA5 始终为低电平
- 程序卡在等待标志位
- W25Q64 没有任何响应

排查过程：

1. 检查是否开启 `RCC_APB2Periph_SPI1` 时钟。
2. 检查 PA5 是否配置为 `GPIO_Mode_AF_PP`。
3. 检查是否调用 `SPI_Cmd(SPI1, ENABLE)`。
4. 检查程序是否实际执行到了 `MySPI_SwapByte()`。
5. 检查软件 NSS 的 SSI 是否置高，避免主机模式错误。

解决方案：

- 补全 SPI1 时钟、GPIO 复用和 SPI 使能步骤。
- 调用 `SPI_NSSInternalSoftwareConfig(SPI1, SPI_NSSInternalSoft_Set)`。
- 在发送函数入口设置断点确认代码路径。

### 问题 3：ID 能读取，但擦除和写入无效

现象：

- JEDEC ID 正常
- 读回数据始终是原值或 `0xFF`
- 擦除、编程指令看似已经发送

排查过程：

1. 检查每次擦除和编程前是否单独发送 `Write Enable`。
2. 检查写使能帧是否通过片选上升沿正确结束。
3. 检查 `/WP` 是否被拉低。
4. 检查目标区域是否受状态寄存器保护位保护。
5. 检查页编程结束后是否等待 BUSY 清零。

解决方案：

- 在每次擦除和页编程前重新执行写使能。
- 检查状态寄存器中的 WEL 和 BUSY。
- 将 `/WP` 保持高电平。
- 为忙等待增加超时和错误返回值。

### 问题 4：写入数据与读回数据部分不一致

现象：

- 前几个字节正确，后半部分错误
- 大数组跨越某个地址后发生覆盖
- 每隔 256 字节出现规律性异常

排查过程：

1. 检查本次页编程是否超过 256 字节。
2. 计算起始地址在页内的偏移。
3. 检查数据是否跨越当前页末尾。
4. 检查写入前是否正确擦除。
5. 检查数组长度和函数 `Count` 参数是否一致。

解决方案：

- 将任意长度写入拆分为多个不跨页的页编程操作。
- 每次编程长度取“剩余数据”和“当前页剩余空间”的较小值。
- 写入后读回并进行整块校验。

### 问题 5：读取结果整体错一位或数值规律性错误

现象：

- ID 不是全 0 或全 1，但数值始终不正确
- 数据似乎左移或右移一位
- 降低速度后问题仍保持固定规律

排查过程：

1. 检查 CPOL 和 CPHA。
2. 检查是否使用 MSB First。
3. 用逻辑分析仪观察数据在哪个边沿变化和采样。
4. 检查 W25Q64 是否工作在标准 SPI 模式。
5. 检查首个时钟前 MOSI 数据是否已稳定。

解决方案：

- 使用模式 0：`SPI_CPOL_Low` + `SPI_CPHA_1Edge`。
- 设置 `SPI_FirstBit_MSB`。
- 不要在一条指令中途动态改变 SPI 模式。

### 问题 6：提高 SPI 速度后偶发读写失败

现象：

- 低速时读写稳定
- 分频减小时偶发 ID 错误或校验失败
- 错误与杜邦线摆放有关

排查过程：

1. 检查导线长度、接地和模块供电去耦。
2. 观察 SCK 过冲、振铃和边沿质量。
3. 检查面包板接触可靠性。
4. 检查 SPI 实际频率是否超过当前电路条件。
5. 检查 MISO 是否有多个从机同时驱动。

解决方案：

- 缩短导线并增加可靠地线。
- 在 W25Q64 电源附近放置去耦电容。
- 逐级提高 SPI 速度，而不是一次使用最高频率。
- 多从机系统确保任一时刻只选中一个从机。

## 结果展示

> 实验 1：初始化 SPI1 后，通过 `0x9F` 指令读取到稳定的 JEDEC ID，说明 SCK、MOSI、MISO 和片选通信链路正常。✅

> 实验 2：擦除首个 4KB 扇区后，向地址 `0x000000` 写入 `01 02 03 04`。✅

> 实验 3：从相同地址读回数据，OLED 显示 `01 02 03 04`，与写入数组一致。✅

> 实验 4：程序通过 `memcmp()` 完成逐字节校验，OLED 显示 `VERIFY:PASS`。✅

## 本节小结

本节将上一节的软件 SPI 底层替换为 STM32F103 的 SPI1 硬件外设，在保留 W25Q64 上层指令驱动的基础上，实现了更稳定、更高效的 Flash 通信。

最重要的知识点是：

```text
硬件 SPI = GPIO 复用配置 + SPI 参数配置 + 状态标志轮询 + 数据寄存器交换
```

```text
MySPI_SwapByte() 是通信层与设备驱动层之间的关键抽象接口
```

```text
W25Q64 的核心流程 = 片选划分指令帧 + 24 位地址 + 写使能 + BUSY 轮询
```

通过本节应能够独立完成：

1. 配置 STM32F103 SPI1 主机。
2. 正确理解 TXE、RXNE、BSY 和软件 NSS。
3. 使用统一的字节交换接口封装 SPI 设备驱动。
4. 读取 W25Q64 JEDEC ID。
5. 执行扇区擦除、页编程和连续读取。
6. 分析页边界、片选时序和 Flash“先擦后写”限制。
7. 根据项目需求在软件 SPI、硬件 SPI、SPI 中断和 SPI DMA 之间做出选择。

后续可以在本实验基础上继续扩展：

- 自动分页的任意长度写入函数
- 跨扇区数据更新
- CRC 或校验和验证
- SPI + DMA 批量读取
- 字库、图片、参数和日志存储
- 简易 Flash 文件管理与磨损均衡
- 多个 SPI 从设备共享总线

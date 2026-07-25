# 09 — USART串口通信：串口发送与串口发送+接收

> 说明：本笔记对应江协科技《STM32入门教程-2023版》的 P27：**[9-3] 串口发送 & 串口发送+接收**，视频时长约 **1:00:01**。由于无法直接取得完整视频字幕与全部画面，本文依据可访问的分集信息、公开课程笔记和 STM32F103 官方资料整理；核心代码为贴合课程标准外设库风格的参考实现。

## 实验概述

本节基于 STM32F103C8T6 的 USART1 外设和 STM32F10x 标准外设库，完成两个基础串口实验：

1. STM32 通过 USART1 向电脑发送字节、数组、字符串和数字。
2. STM32 接收电脑发送的数据，并通过 OLED 显示或原样回传。
3. 将 `printf()` 的底层字符输出重定向到 USART1。
4. 对比查询法接收和 RXNE 中断法接收。

本节的核心目标是掌握 USART 通信的完整软件链路：

```text
开启时钟
    ↓
配置 TX / RX 引脚
    ↓
配置波特率、字长、停止位和校验位
    ↓
使能 USART
    ↓
写 DR 发送 / 读 DR 接收
    ↓
查询标志位或使用中断处理数据
```

本实验默认使用最常见的异步串口参数：

```text
波特率：9600 bit/s
数据位：8 位
校验位：无
停止位：1 位
硬件流控：无
简称：9600 8N1
```

## 硬件连接

以 STM32F103C8T6 最小系统板、USB 转 TTL 串口模块和可选 OLED 为例。

| 器件/信号 | STM32 引脚 | 外部连接 | 说明 |
|---|---:|---|---|
| USART1_TX | PA9 | USB-TTL 的 RXD | STM32 发送端连接对方接收端 |
| USART1_RX | PA10 | USB-TTL 的 TXD | STM32 接收端连接对方发送端 |
| GND | GND | USB-TTL 的 GND | 两端必须共地，建立统一电平参考 |
| USB-TTL 电平 | — | 选择 3.3V TTL | 不要把 RS-232 电平直接接入 STM32 |
| OLED_SCL | PB8 | OLED SCL | 可选，用于显示接收数据 |
| OLED_SDA | PB9 | OLED SDA | 可选，用于显示接收数据 |
| ST-Link | PA13、PA14、GND、3.3V | SWD | 用于下载和调试程序 |

> 串口接线必须交叉：**TX 接 RX，RX 接 TX**。若只做 STM32 单向发送，可以只连接 PA9 → USB-TTL RXD 和 GND。

> 部分 USB-TTL 模块的“3.3V/5V”跳帽既可能选择供电电压，也可能选择逻辑电平。使用前应查看模块原理图或说明，确保 PA9、PA10 接收到的是 STM32 可接受的 TTL 电平。

## 工程文件结构

建议将串口驱动独立封装为 `Serial.c` 和 `Serial.h`：

```text
09-USART-Serial-Tx-Rx
├── Code
│   ├── main.c
│   ├── Serial.c
│   ├── Serial.h
│   ├── OLED.c
│   ├── OLED.h
│   ├── Delay.c
│   └── Delay.h
├── Hardware
│   └── USART1_USB_TTL_Wiring.png
├── Results
│   ├── Serial_Send_Test.png
│   └── Serial_Receive_Echo.png
└── README.md
```

## 核心代码

下面代码采用 STM32F10x 标准外设库。发送示例、查询接收示例和中断接收示例属于不同测试入口，实际工程中只保留一个 `main.c`。

### 1. Serial.h

```c
#ifndef __SERIAL_H                                                     // 判断串口头文件保护宏是否尚未定义，避免同一头文件被重复展开
#define __SERIAL_H                                                     // 定义串口头文件保护宏，使后续重复包含时跳过本文件内容

#include "stm32f10x.h"                                                 // 引入 STM32F10x 器件定义、标准库数据类型和外设寄存器声明
#include <stdio.h>                                                     // 引入 FILE、printf 和格式化输出相关声明
#include <stdarg.h>                                                    // 引入 va_list、va_start 和 va_end 等可变参数工具

void Serial_Init(void);                                                // 声明 USART1 初始化函数，负责 GPIO、USART 和接收中断配置
void Serial_SendByte(uint8_t Byte);                                    // 声明单字节发送函数，把一个 8 位数据写入 USART1
void Serial_SendArray(const uint8_t *Array, uint16_t Length);           // 声明数组发送函数，按指定长度依次发送原始字节
void Serial_SendString(const char *String);                             // 声明字符串发送函数，发送到字符串结束符 '\0' 为止
uint32_t Serial_Pow(uint32_t X, uint32_t Y);                            // 声明整数幂函数，供十进制数字逐位分解使用
void Serial_SendNumber(uint32_t Number, uint8_t Length);                // 声明定长十进制数字发送函数，把数字转换成 ASCII 字符
void Serial_Printf(const char *Format, ...);                            // 声明串口格式化输出函数，使用可变参数生成字符串后发送
uint8_t Serial_GetRxFlag(void);                                        // 声明接收完成标志读取函数，读取后自动清除软件标志
uint8_t Serial_GetRxData(void);                                        // 声明最近一次接收数据读取函数，返回中断缓存的字节

#endif                                                                 // 结束头文件保护条件编译区域
```

### 2. Serial.c：USART1 初始化

```c
#include "Serial.h"                                                     // 引入串口模块接口、STM32 类型及格式化输出相关声明

static volatile uint8_t Serial_RxData = 0;                              // 保存最近一次通过 USART1 接收到的字节，volatile 防止主程序和中断共享时被错误优化
static volatile uint8_t Serial_RxFlag = 0;                              // 保存“收到新字节”软件标志，1 表示主程序尚未处理该字节

void Serial_Init(void)                                                 // 定义 USART1 初始化函数，统一完成发送、接收和 RXNE 中断配置
{                                                                      // 进入串口初始化函数体
    GPIO_InitTypeDef GPIO_InitStructure;                               // 定义 GPIO 初始化结构体，用于配置 PA9 和 PA10 的工作模式
    USART_InitTypeDef USART_InitStructure;                             // 定义 USART 初始化结构体，用于配置通信帧格式和工作模式
    NVIC_InitTypeDef NVIC_InitStructure;                               // 定义 NVIC 初始化结构体，用于配置 USART1 中断通道和优先级

    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA, ENABLE);               // 开启 GPIOA 外设时钟，使 PA9 和 PA10 的配置能够生效
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_USART1, ENABLE);              // 开启挂载在 APB2 总线上的 USART1 外设时钟

    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_AF_PP;                    // 将 TX 引脚配置为复用推挽输出，由 USART1 外设主动驱动高低电平
    GPIO_InitStructure.GPIO_Pin = GPIO_Pin_9;                           // 选择 USART1 默认映射的发送引脚 PA9
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;                  // 设置 PA9 输出速度等级为 50MHz，满足串口边沿驱动需求
    GPIO_Init(GPIOA, &GPIO_InitStructure);                              // 把上述配置写入 GPIOA 控制寄存器，完成 PA9 初始化

    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_IPU;                      // 将 RX 引脚配置为上拉输入，使线路未连接时维持串口空闲高电平
    GPIO_InitStructure.GPIO_Pin = GPIO_Pin_10;                          // 选择 USART1 默认映射的接收引脚 PA10
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;                  // 为结构体速度字段赋合法值；输入模式下该字段通常不决定采样速度
    GPIO_Init(GPIOA, &GPIO_InitStructure);                              // 把上述配置写入 GPIOA 控制寄存器，完成 PA10 初始化

    USART_InitStructure.USART_BaudRate = 9600;                          // 设置 USART1 波特率为 9600bit/s，必须与电脑串口工具保持一致
    USART_InitStructure.USART_HardwareFlowControl = USART_HardwareFlowControl_None; // 关闭 RTS/CTS 硬件流控，本实验只使用 TX、RX 两根数据线
    USART_InitStructure.USART_Mode = USART_Mode_Tx | USART_Mode_Rx;     // 同时使能发送器和接收器，实现全双工收发
    USART_InitStructure.USART_Parity = USART_Parity_No;                 // 关闭奇偶校验，使一帧数据不包含额外校验位
    USART_InitStructure.USART_StopBits = USART_StopBits_1;              // 设置 1 位停止位，对应常用的 8N1 帧格式
    USART_InitStructure.USART_WordLength = USART_WordLength_8b;         // 设置字长为 8 位，每帧承载一个标准字节
    USART_Init(USART1, &USART_InitStructure);                           // 根据结构体参数配置 USART1 的控制寄存器和波特率寄存器

    USART_ITConfig(USART1, USART_IT_RXNE, ENABLE);                      // 允许 RXNE 事件产生中断请求，每收到一个字节即可进入中断服务函数

    NVIC_PriorityGroupConfig(NVIC_PriorityGroup_2);                     // 设置 NVIC 优先级分组为组 2，提供抢占优先级和响应优先级字段
    NVIC_InitStructure.NVIC_IRQChannel = USART1_IRQn;                   // 选择 USART1 全局中断通道
    NVIC_InitStructure.NVIC_IRQChannelCmd = ENABLE;                     // 使能 USART1 中断通道，使内核能够响应该外设请求
    NVIC_InitStructure.NVIC_IRQChannelPreemptionPriority = 1;           // 设置 USART1 抢占优先级为 1，数值越小通常优先级越高
    NVIC_InitStructure.NVIC_IRQChannelSubPriority = 1;                  // 设置 USART1 响应优先级为 1，用于同抢占级中断的先后仲裁
    NVIC_Init(&NVIC_InitStructure);                                     // 把 USART1 中断优先级配置写入 NVIC 寄存器

    USART_Cmd(USART1, ENABLE);                                          // 最后使能 USART1，使发送器、接收器和波特率发生器开始工作
}                                                                      // 结束 USART1 初始化函数
```

### 3. Serial.c：字节、数组、字符串、数字和格式化发送

```c
void Serial_SendByte(uint8_t Byte)                                     // 定义单字节发送函数，作为所有上层发送接口的基础
{                                                                      // 进入单字节发送函数体
    USART_SendData(USART1, (uint16_t)Byte);                             // 将待发送字节写入 USART1 数据寄存器 DR 的低 8 位
    while (USART_GetFlagStatus(USART1, USART_FLAG_TXE) == RESET)        // 循环等待发送数据寄存器变空，确保下一个字节可以安全写入
    {                                                                  // 进入 TXE 轮询等待循环
    }                                                                  // TXE 置位后退出循环；读取状态并不会要求软件手动清除 TXE
}                                                                      // 结束单字节发送函数

void Serial_SendArray(const uint8_t *Array, uint16_t Length)            // 定义原始字节数组发送函数，不依赖字符串结束符
{                                                                      // 进入数组发送函数体
    uint16_t Index;                                                     // 定义数组索引变量，可遍历最多 65535 个字节
    for (Index = 0; Index < Length; Index++)                            // 从数组第 0 项开始，按指定长度逐项发送
    {                                                                  // 进入数组遍历循环
        Serial_SendByte(Array[Index]);                                 // 调用基础字节函数发送当前位置的原始 8 位数据
    }                                                                  // 完成本次数组元素发送并进入下一次循环
}                                                                      // 结束数组发送函数

void Serial_SendString(const char *String)                              // 定义 C 字符串发送函数，适合输出文本和调试信息
{                                                                      // 进入字符串发送函数体
    uint16_t Index = 0;                                                 // 定义字符索引并从字符串首字符开始
    while (String[Index] != '\0')                                      // 在尚未遇到字符串末尾的空字符时持续发送
    {                                                                  // 进入字符串遍历循环
        Serial_SendByte((uint8_t)String[Index]);                        // 把当前字符的编码值作为一个字节发送到串口
        Index++;                                                        // 索引加 1，移动到字符串中的下一个字符
    }                                                                  // 遇到 '\0' 后退出循环，结束字符串发送
}                                                                      // 结束字符串发送函数

uint32_t Serial_Pow(uint32_t X, uint32_t Y)                             // 定义非负整数幂函数，用于计算 10 的各次幂
{                                                                      // 进入整数幂函数体
    uint32_t Result = 1;                                                // 将累乘结果初始化为 1，对应任意数的 0 次幂
    while (Y > 0)                                                       // 当指数仍大于 0 时继续执行乘法
    {                                                                  // 进入幂运算循环
        Result *= X;                                                    // 将当前结果乘以底数，逐步得到 X 的 Y 次幂
        Y--;                                                            // 指数减 1，记录已经完成一次乘法
    }                                                                  // 指数减到 0 后退出循环
    return Result;                                                      // 返回最终计算得到的整数幂结果
}                                                                      // 结束整数幂函数

void Serial_SendNumber(uint32_t Number, uint8_t Length)                 // 定义定长十进制发送函数，按高位到低位输出数字字符
{                                                                      // 进入数字发送函数体
    uint8_t Index;                                                      // 定义数字位索引，用于遍历指定的显示长度
    for (Index = 0; Index < Length; Index++)                            // 从最高位开始循环处理 Length 个十进制位
    {                                                                  // 进入数字逐位发送循环
        uint8_t Digit = (uint8_t)(Number / Serial_Pow(10, Length - Index - 1) % 10); // 通过除法和取模提取当前十进制位
        Serial_SendByte((uint8_t)(Digit + '0'));                        // 加上字符 '0' 的 ASCII 偏移，把数值 0～9 转成可显示字符
    }                                                                  // 当前位发送完成后继续处理下一位
}                                                                      // 结束定长数字发送函数

int fputc(int Character, FILE *Stream)                                  // 重写 C 库底层字符输出函数，使 printf 的字符流转向 USART1
{                                                                      // 进入 fputc 重定向函数体
    (void)Stream;                                                       // 显式忽略未使用的 FILE 指针，避免编译器产生未使用参数警告
    Serial_SendByte((uint8_t)Character);                                // 将 printf 交付的单个字符通过 USART1 发送
    return Character;                                                   // 按 fputc 约定返回已经输出的字符值
}                                                                      // 结束 fputc 重定向函数

void Serial_Printf(const char *Format, ...)                             // 定义不依赖全局 stdout 的串口格式化输出封装
{                                                                      // 进入串口格式化输出函数体
    char Buffer[100];                                                   // 定义临时字符缓冲区，用于保存格式化后的完整字符串
    va_list Arguments;                                                  // 定义可变参数列表对象，用于访问 Format 后面的参数
    va_start(Arguments, Format);                                        // 从 Format 之后的位置开始读取可变参数
    vsnprintf(Buffer, sizeof(Buffer), Format, Arguments);               // 按格式生成字符串，并限制最大写入长度以防止缓冲区越界
    va_end(Arguments);                                                  // 结束可变参数访问并释放相关状态
    Serial_SendString(Buffer);                                          // 将格式化后的字符串逐字节发送到 USART1
}                                                                      // 结束串口格式化输出函数
```

> `Serial_SendByte()` 等待的是 `TXE`，表示发送数据寄存器已经空，可以写入下一个字节；它不等价于整帧物理波形已经发送完成。若要关闭 USART、切换引脚或控制 RS-485 DE 引脚，应额外等待 `TC`。

### 4. Serial.c：中断接收接口

```c
uint8_t Serial_GetRxFlag(void)                                         // 定义接收标志查询函数，供主循环判断是否出现新字节
{                                                                      // 进入接收标志查询函数体
    if (Serial_RxFlag == 1)                                            // 判断中断服务函数是否已经置位“新数据到达”标志
    {                                                                  // 进入存在新数据的处理分支
        Serial_RxFlag = 0;                                             // 清除软件接收标志，使下一次新字节能够再次被检测
        return 1;                                                      // 返回 1，通知调用者本次可以读取接收缓存
    }                                                                  // 结束存在新数据的处理分支
    return 0;                                                          // 没有新数据时返回 0，主程序可继续执行其他任务
}                                                                      // 结束接收标志查询函数

uint8_t Serial_GetRxData(void)                                         // 定义接收数据读取函数，返回最近一次中断缓存的字节
{                                                                      // 进入接收数据读取函数体
    return Serial_RxData;                                              // 把中断服务函数写入的软件缓存值返回给主程序
}                                                                      // 结束接收数据读取函数

void USART1_IRQHandler(void)                                           // 定义名称固定的 USART1 中断服务函数，启动文件会从向量表跳转到此处
{                                                                      // 进入 USART1 中断服务函数体
    if (USART_GetITStatus(USART1, USART_IT_RXNE) == SET)                // 判断本次中断是否由“接收数据寄存器非空”事件触发
    {                                                                  // 进入 RXNE 中断处理分支
        Serial_RxData = (uint8_t)USART_ReceiveData(USART1);             // 读取 DR 低 8 位并保存数据；读取操作同时完成 RXNE 的正常清除流程
        Serial_RxFlag = 1;                                             // 置位软件标志，通知主循环有一个新字节等待处理
    }                                                                  // 结束 RXNE 中断处理分支
}                                                                      // 结束 USART1 中断服务函数
```

> 该单字节缓存结构适合低速教学实验。如果电脑连续快速发送多个字节，而主循环处理不及时，新字节可能覆盖旧字节。正式项目通常使用环形缓冲区、FIFO、空闲中断或 DMA 接收。

### 5. main.c：串口发送测试

```c
#include "stm32f10x.h"                                                 // 引入 STM32F10x 器件定义和标准外设库基础类型
#include "Serial.h"                                                    // 引入串口初始化、发送和格式化输出函数声明

int main(void)                                                         // 定义程序入口函数，系统启动后从此处执行用户代码
{                                                                      // 进入主函数体
    const uint8_t RawData[] = {0x41, 0x42, 0x43, 0x44};                 // 定义原始字节数组，对应 ASCII 字符 A、B、C、D
    Serial_Init();                                                     // 初始化 USART1 的 PA9、PA10、通信参数和接收中断

    Serial_SendByte(0x41);                                             // 发送原始字节 0x41，文本模式下通常显示为字符 A
    Serial_SendString("\r\n");                                         // 发送回车和换行控制字符，使串口终端光标移动到下一行
    Serial_SendArray(RawData, sizeof(RawData));                         // 按数组实际字节数发送四个连续的原始数据
    Serial_SendString("\r\n");                                         // 再次发送换行，分隔不同测试输出
    Serial_SendString("Hello STM32!\r\n");                              // 发送以 '\0' 结尾的文本字符串并在末尾换行
    Serial_SendNumber(12345, 5);                                       // 将整数 12345 拆成五个 ASCII 数字字符发送
    Serial_SendString("\r\n");                                         // 发送换行，避免后续输出与数字粘连
    printf("printf: Num=%d, Hex=0x%02X\r\n", 666, 0x5A);                // 通过 fputc 重定向把格式化文本输出到 USART1
    Serial_Printf("Serial_Printf: Voltage=%.2fV\r\n", 3.30);            // 使用串口封装函数格式化浮点数并发送；需确认所用 C 库开启浮点格式支持

    while (1)                                                          // 进入无限循环，保持单片机持续运行
    {                                                                  // 进入主循环体
    }                                                                  // 本实验发送完成后不再执行其他周期任务
}                                                                      // 结束主函数
```

### 6. main.c：查询法接收

查询法不使用 `Serial_GetRxFlag()`，而是在主循环中直接检查硬件 `RXNE` 标志。它结构直观，但主循环必须频繁执行到查询语句。

```c
#include "stm32f10x.h"                                                 // 引入 STM32F10x 器件定义、USART 标志位和数据读取函数
#include "OLED.h"                                                      // 引入 OLED 初始化及十六进制显示函数声明
#include "Serial.h"                                                    // 引入 USART1 初始化和发送函数声明

int main(void)                                                         // 定义查询法接收示例的程序入口
{                                                                      // 进入主函数体
    uint8_t RxData = 0;                                                // 定义接收变量，用于保存主循环从 USART1 读取的字节
    OLED_Init();                                                       // 初始化 OLED，便于观察接收到的十六进制数据
    OLED_ShowString(1, 1, "RxData:");                                  // 在 OLED 第一行显示固定标签
    Serial_Init();                                                     // 初始化 USART1；查询法若完全不用中断，可在驱动中关闭 RXNE 中断配置

    while (1)                                                          // 持续查询串口接收状态
    {                                                                  // 进入主循环体
        if (USART_GetFlagStatus(USART1, USART_FLAG_RXNE) == SET)        // 检查接收数据寄存器是否已经存入一个尚未读取的字节
        {                                                              // 进入收到新字节的处理分支
            RxData = (uint8_t)USART_ReceiveData(USART1);                // 读取 DR 中的接收字节，同时按硬件规则清除 RXNE 状态
            OLED_ShowHexNum(1, 8, RxData, 2);                           // 将接收字节以两位十六进制形式显示在 OLED 上
            Serial_SendByte(RxData);                                   // 把收到的字节原样发回电脑，实现最简单的串口回显
        }                                                              // 结束新字节处理分支
    }                                                                  // 返回循环顶部继续检查下一字节
}                                                                      // 结束查询法主函数
```

> 若使用本节给出的完整 `Serial_Init()`，RXNE 中断已经开启，因此查询法示例与中断法会同时争用接收数据。实际测试查询法时，应临时注释 `USART_ITConfig()` 和 NVIC 配置，或者提供独立的“无中断初始化函数”。

### 7. main.c：中断法接收并回显

```c
#include "stm32f10x.h"                                                 // 引入 STM32F10x 器件定义和基础数据类型
#include "OLED.h"                                                      // 引入 OLED 初始化、字符串和十六进制显示函数
#include "Serial.h"                                                    // 引入串口初始化、中断接收接口和发送函数

int main(void)                                                         // 定义中断法收发示例的程序入口
{                                                                      // 进入主函数体
    uint8_t RxData = 0;                                                // 定义主程序接收变量，用于保存从串口模块取出的最新字节
    OLED_Init();                                                       // 初始化 OLED 显示屏
    OLED_ShowString(1, 1, "RxData:");                                  // 显示接收数据标签，为后续十六进制数预留位置
    Serial_Init();                                                     // 初始化 USART1 并开启 RXNE 中断及 NVIC 通道

    Serial_SendString("USART1 Ready\r\n");                              // 上电后向电脑发送就绪提示，验证发送链路正常

    while (1)                                                          // 主循环持续执行应用层任务，不需要反复查询硬件 RXNE
    {                                                                  // 进入主循环体
        if (Serial_GetRxFlag() == 1)                                   // 检查中断模块是否已经缓存了一个新接收字节
        {                                                              // 进入新数据处理分支
            RxData = Serial_GetRxData();                               // 从串口模块取得最近一次收到的字节
            OLED_ShowHexNum(1, 8, RxData, 2);                           // 在 OLED 上以两位十六进制显示该字节
            Serial_SendByte(RxData);                                   // 将该字节原样回传给电脑，形成中断接收回显
        }                                                              // 结束新数据处理分支
    }                                                                  // 返回主循环顶部，继续执行并等待下一次软件标志置位
}                                                                      // 结束中断法主函数
```

## 代码要点

| 行/段 | 说明 |
|---|---|
| `RCC_APB2PeriphClockCmd()` | GPIOA 和 USART1 都挂载在 APB2 总线上，使用前必须分别开启时钟。 |
| `GPIO_Mode_AF_PP` | PA9 是 USART1_TX，由外设控制输出，因此配置为复用推挽输出。 |
| `GPIO_Mode_IPU` | PA10 是接收输入；串口空闲电平为高，上拉输入可避免悬空误触发。 |
| `USART_InitTypeDef` | 集中配置波特率、字长、停止位、校验位、收发模式和硬件流控。 |
| `USART_Mode_Tx \| USART_Mode_Rx` | 同时开启发送器与接收器，实现全双工通信。 |
| `USART_SendData()` | 把数据写入 DR；硬件随后将其送入发送移位寄存器并按位输出。 |
| `USART_FLAG_TXE` | 发送数据寄存器为空，表示可以写入下一个数据，不代表最后一位已经离开 TX 引脚。 |
| `USART_FLAG_TC` | 整个数据帧（含停止位）已经发送完成，关闭外设或切换方向前应等待它。 |
| `USART_FLAG_RXNE` | 接收数据寄存器非空，表示至少有一个字节等待软件读取。 |
| `USART_ReceiveData()` | 读取 DR 中的接收数据；正常读取会完成 RXNE 清除流程。 |
| `USART_ITConfig()` | 把 RXNE 事件连接到 USART1 中断请求。 |
| `USART1_IRQHandler()` | USART1 的固定中断函数名，必须与启动文件向量表中的符号一致。 |
| `volatile` | 告诉编译器变量可能在中断等异步环境中变化，不能只使用寄存器中的旧副本。 |
| `fputc()` | `printf()` 输出字符时调用的底层接口之一，重写后可把标准输出转到 USART1。 |
| `Serial_Printf()` | 先把格式化内容写入字符串缓冲区，再通过串口发送，适合模块化工程。 |
| `\r\n` | `\r` 回到行首，`\n` 换到下一行；组合使用可兼容多数串口终端。 |
| `TX ↔ RX` | 串口两端必须交叉连接，并且共地。 |

## 关键知识点

### 1. USART1 初始化流程

#### 原理

USART 外设不能直接在复位默认状态下完成通信。软件需要依次打开时钟、配置引脚复用、设置通信参数、使能收发器，并在需要时配置中断。

本实验初始化顺序为：

```text
GPIOA 时钟
    ↓
USART1 时钟
    ↓
PA9 复用推挽输出
    ↓
PA10 上拉输入
    ↓
9600 / 8N1 / 无流控 / 收发使能
    ↓
RXNE 中断与 NVIC
    ↓
USART_Cmd()
```

USART1 位于 APB2，总线时钟参与波特率发生器分频。标准库根据 `USART_BaudRate` 和当前外设时钟计算 BRR 寄存器值。

#### 特点

- USART1 默认使用 PA9 发送、PA10 接收。
- USART1 挂载在 APB2，USART2、USART3 通常挂载在 APB1。
- 通信两端必须设置相同波特率、字长、校验位和停止位。
- GPIO 初始化与 USART 外设初始化缺一不可。

#### 面试易问

**Q：为什么 PA9 要配置为复用推挽，而不是普通推挽输出？**

A：普通推挽模式由 GPIO 输出寄存器控制；复用推挽模式把引脚输出控制权交给 USART1 外设，使硬件能够自动产生起始位、数据位和停止位波形。

**Q：为什么通常最后才调用 `USART_Cmd()`？**

A：先完成参数、GPIO 和中断配置，再统一使能外设，可避免外设在配置尚未完整时产生无效输出或意外中断。

#### 易错点

- 只开启 USART1 时钟，忘记开启 GPIOA 时钟。
- 把 USART1 错写成 APB1 外设。
- 先使能 USART，再配置中断和 GPIO，导致启动过程不稳定。
- 串口工具参数与 MCU 配置不一致，出现乱码或无法接收。

---

### 2. 串口交叉接线与电平标准

#### 原理

TX 是本设备的数据输出，RX 是本设备的数据输入。因此两个设备互连时，一端 TX 必须进入另一端 RX。

```text
STM32 PA9  / TX  ─────→  USB-TTL RXD
STM32 PA10 / RX  ←─────  USB-TTL TXD
STM32 GND          ─────  USB-TTL GND
```

TTL 串口描述的是逻辑电平，而传统 RS-232 使用正负电压，两者不能直接相连。

#### 特点

- 串口最少可用 TX、RX、GND 三根线实现全双工通信。
- 单向发送只需要 TX 和 GND。
- STM32F103 系统一般使用 3.3V 逻辑。
- 共地为两端提供相同的电压参考。

#### 面试易问

**Q：串口为什么要共地？**

A：接收端需要根据本地 GND 判断输入电压是高还是低。如果两端没有共同参考，信号电平可能漂移，导致误码或完全无法通信。

**Q：USART 与 USB-TTL 模块之间为什么要交叉连接？**

A：发送端应连接接收端。若 TX 接 TX，两端都在输出而没有设备读取；若 RX 接 RX，两端都在等待输入。

#### 易错点

- TX 接 TX、RX 接 RX。
- 只接数据线而没有连接 GND。
- 把 USB-TTL 的 5V 电源输出接到 STM32 的 3.3V 电源节点。
- 将真正的 RS-232 电平直接接到 PA9/PA10。

---

### 3. 字节、数组、字符串与数字发送

#### 原理

USART 硬件每次处理的是一个数据帧。在 8N1 模式下，软件通常把一个 8 位数值写入 DR，硬件自动添加起始位和停止位并串行输出。

上层数据最终都要拆成字节：

```text
单字节：直接发送一次
数组：按长度循环发送
字符串：发送到 '\0'
十进制数字：逐位转换成 '0'～'9'
格式化文本：先生成字符串，再逐字节发送
```

HEX 显示和文本显示只是电脑串口工具对同一字节的不同解释。例如字节 `0x41` 在 HEX 模式显示为 `41`，在文本模式显示为 `A`。

#### 特点

- 字节数组可以包含 `0x00`，必须依靠显式长度。
- C 字符串以 `'\0'` 结尾，不适合直接承载任意二进制数据。
- 数字要显示成人类可读文本，需要转换成 ASCII 字符。
- `printf()` 适合调试，但代码体积和执行时间通常高于直接发送函数。

#### 面试易问

**Q：发送数值 100 和发送字符串 `"100"` 有什么区别？**

A：数值 100 作为单字节发送时是 `0x64`；字符串 `"100"` 会发送三个字节 `0x31、0x30、0x30`，分别对应字符 `'1'、'0'、'0'`。

**Q：为什么发送二进制数组不能用字符串函数？**

A：二进制数据中可能包含 `0x00`，字符串函数会把它当作结尾提前停止，因此二进制数组必须同时提供长度。

#### 易错点

- 把“HEX 模式显示”误认为串口线上传输了十六进制文本。
- 发送数组时长度写错，造成少发或越界读取。
- 忘记字符串末尾的 `'\0'` 只用于内存，不应作为正文发送。
- 用 `Serial_SendNumber()` 发送的长度小于实际位数，导致高位被截断。

---

### 4. TXE 与 TC 的区别

#### 原理

USART 发送路径通常包含数据寄存器和发送移位寄存器。

```text
CPU 写 DR
   ↓
发送数据寄存器
   ↓
发送移位寄存器
   ↓
TX 引脚逐位输出
```

- `TXE = 1`：发送数据寄存器已经空，可以写下一个数据。
- `TC = 1`：数据寄存器和移位寄存器都空，最后一个停止位也已发送完成。

连续发送时等待 TXE 可以形成流水线，提高效率；结束通信或切换硬件方向时应等待 TC。

#### 特点

- TXE 更适合逐字节连续发送。
- TC 更适合判断整段物理发送真正结束。
- 写入 DR 后，TXE 会暂时清零。
- TC 在新一帧开始发送时清零，在最后一帧完成后置位。

#### 面试易问

**Q：发送一个字节后等待 TXE，能否立即关闭 USART？**

A：不能保证。TXE 只表示 DR 空，发送移位寄存器可能仍在输出。关闭 USART 或切换 RS-485 方向前应等待 TC。

**Q：为什么课程发送函数常等待 TXE？**

A：上层通常要连续写多个字节，等待 TXE 即可安全装载下一个字节，不必让每个字节都等待完整停止位后再继续。

#### 易错点

- 把 TXE 当作整帧发送完成。
- 每个字节都等待 TC，虽然通常能工作，但降低连续发送效率。
- RS-485 控制 DE 时只等待 TXE，导致最后一个字节尾部被截断。
- 不等待任何标志就连续写 DR，导致数据覆盖或丢失。

---

### 5. `printf()` 重定向与可变参数

#### 原理

`printf()` 会把格式化结果转换为一连串字符，并通过底层字符输出函数写到标准输出。嵌入式系统没有默认终端，因此可以重写 `fputc()`，把每个字符发送到 USART1。

另一种方式是：

```text
Format + 可变参数
        ↓
vsnprintf()
        ↓
字符数组 Buffer
        ↓
Serial_SendString()
```

这种封装便于将格式化字符串发送到指定串口，不完全依赖全局标准输出。

#### 特点

- 支持 `%d`、`%u`、`%X`、`%s` 等格式化输出。
- 便于打印传感器值、状态和调试信息。
- 浮点格式化可能显著增加代码体积。
- 缓冲区必须有长度限制，优先使用 `snprintf()` / `vsnprintf()`。

#### 面试易问

**Q：为什么重写 `fputc()` 后 `printf()` 可以从串口输出？**

A：`printf()` 最终需要逐字符写出结果。工具链的 C 库会调用底层字符输出接口；将该接口改为 `Serial_SendByte()` 后，标准输出流就被导向 USART。

**Q：`sprintf()` 和 `printf()` 的区别是什么？**

A：`printf()` 把格式化结果发送到标准输出；`sprintf()` 把结果写入内存字符串。嵌入式中可先用 `snprintf()` 生成字符串，再选择任意串口发送。

#### 易错点

- 没有在 Keil 中配置相应 C 库支持，导致 `printf()` 链接失败或没有输出。
- 使用无长度限制的 `sprintf()` / `vsprintf()`，造成缓冲区溢出。
- 开启浮点格式化后 Flash 占用明显增加。
- 在高频中断里调用 `printf()`，导致中断执行时间过长。

---

### 6. 查询法与中断法接收

#### 原理

查询法由主循环主动检查 `RXNE`：

```text
主循环 → 检查 RXNE → 读取 DR → 处理数据
```

中断法由硬件在 `RXNE` 置位时请求 CPU：

```text
收到字节 → RXNE 置位 → NVIC 响应 → ISR 读取 DR → 置软件标志
```

查询法依赖主循环及时轮询；中断法允许主循环处理其他任务，字节到达时再进入服务函数。

#### 特点

| 对比项 | 查询法 | 中断法 |
|---|---|---|
| 实现难度 | 低 | 较高 |
| CPU 占用 | 需要频繁查询 | 无数据时不占用接收处理时间 |
| 响应及时性 | 受主循环周期影响 | 通常更及时 |
| 适用场景 | 简单、低速、阻塞式程序 | 多任务、非阻塞式程序 |
| 扩展性 | 一般 | 易扩展到缓冲区和协议解析 |

#### 面试易问

**Q：中断接收一定不会丢数据吗？**

A：不一定。中断只改善响应及时性。如果中断执行不及时、软件只保留单字节缓存、处理速度低于输入速度，仍可能发生溢出或覆盖。

**Q：什么时候适合使用查询法？**

A：程序非常简单、主循环可以持续轮询、通信速率低，或者当前代码本身就是阻塞式流程时，查询法更直观。

#### 易错点

- 同时在主循环和中断中读取 DR，造成数据被不同代码路径争抢。
- 查询周期过长，前一个字节未读取时新字节到达，产生 ORE 溢出。
- 中断函数名称拼写错误，导致向量表无法调用。
- 忘记同时打开 USART 的 RXNE 中断源和 NVIC 通道。

---

### 7. RXNE、ORE 与接收数据读取

#### 原理

当接收移位寄存器完成一帧数据，并把数据转入接收数据寄存器后，`RXNE` 置位。软件读取 DR 后，当前字节被取走，RXNE 按硬件流程清除。

如果 RXNE 中的数据尚未读取，下一个新字节又需要转入 DR，就可能产生 ORE（Overrun Error，溢出错误）。

#### 特点

- RXNE 表示接收寄存器中存在未读取数据。
- 读取 DR 是接收处理的核心动作。
- ORE 表示软件没有及时取走前一字节。
- 高吞吐接收通常需要 FIFO 思想、DMA 或更高效的中断处理。

#### 面试易问

**Q：为什么 ISR 中要尽快读取 DR？**

A：读取 DR 可以取走当前字节并腾出接收寄存器。如果长时间不读，新字节到达时可能触发 ORE，造成数据丢失。

**Q：RXNE 是否需要调用 `USART_ClearITPendingBit()` 清除？**

A：正常接收流程应通过读取状态和数据寄存器完成清除。对于 STM32F1 的 RXNE，读取接收数据是关键动作，不应把“手动清标志”当作替代读取数据的方法。

#### 易错点

- ISR 只置标志但不读取 DR，导致中断持续触发。
- 在中断里执行大量显示、延时或格式化输出，来不及接收后续字节。
- 忽略 ORE，出现“偶尔少一个字节”却只排查接线。
- 误以为波特率低就绝对不会溢出，忽略主循环中的长时间阻塞。

---

### 8. 中断与主程序共享变量

#### 原理

中断服务函数可以在任意指令边界打断主程序。`Serial_RxData` 和 `Serial_RxFlag` 同时被 ISR 写入、主程序读取，因此属于异步共享变量。

`volatile` 告诉编译器：每次访问都应从实际内存读取或写回，不能假设变量在当前代码段之外不会变化。

#### 特点

- `volatile` 解决编译优化可见性问题。
- `volatile` 不自动保证多步骤操作的原子性。
- 单字节读写在 Cortex-M3 上通常是原子的，但“判断后清零”仍是多个操作。
- 高可靠接收应使用环形缓冲区并明确生产者/消费者规则。

#### 面试易问

**Q：`volatile` 能否解决所有中断并发问题？**

A：不能。它只约束编译器优化，不提供锁、临界区、顺序一致性或多变量事务保护。

**Q：为什么接收变量建议加 `volatile`？**

A：变量可能在主程序没有显式写入的位置被中断修改。若不加 `volatile`，编译器可能复用旧值，使主程序看不到更新。

#### 易错点

- 共享变量不加 `volatile`，优化后程序行为异常。
- 认为加了 `volatile` 就不会丢数据。
- 只有一个接收字节缓存，却用来接收连续数据包。
- 主循环清标志的同时新中断到达，造成事件合并或遗漏。

---

### 9. 波特率、误差与乱码

#### 原理

异步串口双方没有共享时钟线，发送端和接收端分别根据各自时钟产生位时间。波特率不一致或时钟误差过大时，接收端采样点会逐渐偏离数据位中心，最终导致错误。

乱码还可能来自显示模式和字符编码：

- HEX 模式与文本模式解释不同。
- 中文字符串可能涉及 UTF-8、GB2312/GBK 等编码差异。
- 串口工具的编码方式必须与源码和发送字节一致。

#### 特点

- 双方波特率必须一致。
- 8N1 中的 N 表示无校验。
- 晶振、系统时钟配置会影响实际波特率。
- 英文 ASCII 最适合初次验证链路。

#### 面试易问

**Q：串口没有时钟线，接收端如何知道在哪里采样？**

A：接收端检测起始位下降沿后，根据预设波特率生成内部采样时钟，并在每个数据位的中间附近采样。

**Q：为什么建议先发送 `"Hello"`，再测试中文？**

A：ASCII 编码简单且工具兼容性高，可先排除波特率和接线问题；中文还叠加了源码编码与终端编码因素。

#### 易错点

- MCU 配置 9600，电脑工具选择 115200。
- 一端配置偶校验，另一端无校验。
- 系统时钟配置错误，导致实际波特率偏差。
- 看到中文乱码就误判为串口硬件故障。

---

## 本节核心记忆

```text
USART1 默认引脚：
PA9  = TX
PA10 = RX
```

```text
串口接线：
TX 接对方 RX
RX 接对方 TX
GND 必须共地
```

```text
常用参数：
9600 / 8 数据位 / 无校验 / 1 停止位
也就是 9600 8N1
```

```text
发送：
写 DR → 等 TXE → 写下一个字节
全部物理发送完成要看 TC
```

```text
接收：
RXNE 置位 → 读取 DR → 处理数据
简单程序可查询，复杂程序优先中断或 DMA
```

```text
printf 重定向：
printf → fputc → Serial_SendByte → USART1
```

```text
中断共享变量：
需要 volatile
但 volatile 不等于线程安全，也不等于不会丢数据
```

## 开发过程总结

### 问题 1：串口助手完全收不到 STM32 数据

现象：

- 程序可以正常下载运行。
- 串口助手打开成功，但窗口没有任何输出。
- OLED 或其他外设工作正常。

排查过程：

1. 检查 PA9 是否接到 USB-TTL 的 RXD。
2. 检查 STM32 与 USB-TTL 是否共地。
3. 检查电脑选择的 COM 口是否正确。
4. 检查 GPIOA 和 USART1 时钟是否开启。
5. 检查 PA9 是否配置为复用推挽输出。
6. 检查程序是否执行到 `Serial_Init()` 和发送函数。

解决方案：

- 按 TX → RX 交叉方式重新接线。
- 在初始化后固定发送 `"Hello\r\n"`，先排除应用逻辑问题。
- 用示波器或逻辑分析仪观察 PA9 是否存在串口波形。
- 确认 USB-TTL 驱动安装正常，设备管理器中没有异常标记。

---

### 问题 2：收到的数据全部是乱码

现象：

- 串口助手能持续出现字符。
- 内容与程序发送的文本不一致。
- 改变串口工具参数后乱码形式变化。

排查过程：

1. 对比 MCU 和电脑的波特率。
2. 对比数据位、校验位和停止位。
3. 检查系统时钟是否按 72MHz 正确配置。
4. 先发送单字符 `'A'` 或字节 `0x55`。
5. 区分 HEX 显示模式和文本显示模式。

解决方案：

- 两端统一为 9600、8N1、无硬件流控。
- 先用 ASCII 英文验证，确认链路后再发送中文。
- 若只有中文乱码，统一 Keil 源码编码和串口工具文本编码。

---

### 问题 3：STM32 能发送，但收不到电脑数据

现象：

- 电脑能看到 STM32 的发送内容。
- 串口助手发送数据后，OLED 没变化。
- USART1 中断函数没有进入。

排查过程：

1. 检查 USB-TTL TXD 是否接到 PA10。
2. 检查 PA10 是否配置为上拉输入或浮空输入。
3. 检查 `USART_Mode_Rx` 是否开启。
4. 检查 RXNE 中断源与 USART1 NVIC 通道是否同时开启。
5. 检查中断函数名是否为 `USART1_IRQHandler`。
6. 在查询法中直接观察 `USART_FLAG_RXNE`。

解决方案：

- 补全 PA10 和 USB-TTL TXD 接线。
- 将 USART 模式设置为 `USART_Mode_Tx | USART_Mode_Rx`。
- 先用查询法验证硬件接收链路，再切换到中断法。
- 确认工程使用的启动文件包含 USART1 中断向量。

---

### 问题 4：只能收到第一个字节，后续字节丢失

现象：

- 单击发送一个字符时正常。
- 连续发送字符串时只显示部分内容。
- 高波特率或主循环包含延时后问题更明显。

排查过程：

1. 检查 ISR 是否第一时间读取 DR。
2. 检查 ISR 中是否调用 OLED、Delay 或 `printf()`。
3. 检查是否只使用单字节全局变量缓存。
4. 检查是否出现 ORE 溢出错误。
5. 检查主循环是否及时处理并清除软件标志。

解决方案：

- 中断中只完成“读 DR + 入缓冲区”，不要做耗时操作。
- 使用环形缓冲区保存连续字节。
- 数据量更大时使用 DMA 或空闲中断方案。
- 减少长时间阻塞式延时。

---

### 问题 5：`printf()` 编译报错或没有串口输出

现象：

- 直接调用 `Serial_SendString()` 正常。
- 加入 `printf()` 后链接报错、程序体积异常或没有输出。
- 浮点数格式化无法显示。

排查过程：

1. 检查是否包含 `<stdio.h>`。
2. 检查是否实现了工具链要求的底层输出函数。
3. 检查 Keil 是否启用了 MicroLIB，或工程是否提供完整系统调用重定向。
4. 检查 `fputc()` 是否确实调用 `Serial_SendByte()`。
5. 检查浮点格式化支持选项和 Flash 容量。

解决方案：

- 先验证 `fputc()` 能发送单字符。
- 不需要全局标准输出时，使用 `snprintf()` + `Serial_SendString()`。
- 资源紧张时减少 `printf()` 使用，改用轻量级数字转换函数。
- 格式化缓冲区使用有界函数，防止越界。

---

### 问题 6：发送最后一个字节后立即切换引脚，数据尾部错误

现象：

- 连续文本大部分正常。
- 最后一个字符偶尔缺失或停止位不完整。
- 常发生在关闭 USART、进入低功耗或控制 RS-485 方向时。

排查过程：

1. 检查代码只等待了 `TXE` 还是等待了 `TC`。
2. 观察最后一个字节的 TX 波形是否完整。
3. 检查是否在发送函数返回后立即关闭时钟或切换 GPIO。

解决方案：

- 连续装载数据时等待 TXE。
- 最后一个字节写入后，再等待 `USART_FLAG_TC == SET`。
- 确认 TC 置位后再关闭 USART、切换引脚或拉低 RS-485 DE。

## 结果展示

> 实验 1：连接 USB-TTL 并打开串口助手后，STM32 能依次发送字节、数组、字符串、十进制数字和格式化文本。✅

> 实验 2：串口助手发送单个字节后，OLED 能显示对应的两位十六进制数。✅

> 实验 3：使用查询法时，主循环检测到 RXNE 后读取数据并原样回传。✅

> 实验 4：使用中断法时，USART1 每收到一个字节进入中断，主循环通过软件标志取得数据并回显。✅

> 实验 5：`printf()` 和 `Serial_Printf()` 能将格式化调试信息输出到电脑串口终端。✅

## 本节小结

本节完成了 STM32F103 USART1 从底层初始化到应用层收发的完整实践。

最重要的知识点是：

```text
串口硬件链路 = TX/RX 交叉连接 + 共地 + 正确逻辑电平
```

```text
串口软件初始化 = RCC + GPIO 复用 + USART 参数 + 使能
```

```text
发送的本质 = 写入数据寄存器，并根据 TXE/TC 判断进度
```

```text
接收的本质 = RXNE 置位后及时读取数据寄存器
```

```text
简单程序可以轮询 RXNE
需要并行处理其他任务时使用中断
连续数据接收应进一步使用环形缓冲区、协议状态机或 DMA
```

本节建立的 `Serial.c / Serial.h` 模块也是后续学习 USART 数据包、串口命令解析、传感器通信、上位机调试和串口下载的基础。

## 参考资料

- 江协科技《STM32入门教程-2023版》P27：[9-3] 串口发送 & 串口发送+接收。
- STMicroelectronics《RM0008 STM32F10xxx Reference Manual》USART 章节。
- STMicroelectronics《DS5319 STM32F103x8/xB Datasheet》引脚与通信外设章节。
- STM32F10x Standard Peripheral Library V3.5.0 USART 驱动与示例。

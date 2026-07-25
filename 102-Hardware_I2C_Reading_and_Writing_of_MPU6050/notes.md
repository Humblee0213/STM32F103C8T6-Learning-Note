# 23 — I2C通信：硬件I2C读写MPU6050

> 资料说明：本节对应江协科技《STM32入门教程-2023版》的 `[10-5] 硬件I2C读写MPU6050`。由于未直接获取到视频完整字幕和逐帧画面，以下内容依据可访问的分集信息、课程前后文、公开学习笔记，以及 STM32F103 和 MPU6050 官方资料整理。核心代码为贴合课程标准外设库风格的参考实现，并非视频源码的逐字转录。

## 实验概述

本实验基于 STM32F103C8T6 标准外设库，使用芯片内部的 **I2C2 硬件外设**与 MPU6050 六轴姿态传感器通信，实现：

1. 初始化 I2C2，并由硬件自动产生 SCL 时钟与 SDA 数据时序
2. 向 MPU6050 指定寄存器写入配置数据
3. 从 MPU6050 指定寄存器读取芯片 ID
4. 读取三轴加速度和三轴角速度原始数据
5. 将传感器数据实时显示到 OLED 屏幕
6. 理解硬件 I2C 的事件状态机、应答控制和重复起始条件

本节的核心目标是把上一节的软件 I2C 通信层替换为硬件 I2C 外设，同时保持 MPU6050 驱动层和主程序调用方式基本不变。

> GPIO 模拟时序 → 替换为 I2C2 外设产生时序 → 等待对应硬件事件 → 完成寄存器读写

## 硬件连接

本实验使用 STM32F103C8T6 的 I2C2 默认引脚 PB10 和 PB11。

| 器件 | 对应引脚 | 说明 |
|---|---:|---|
| MPU6050 VCC | 3.3V | 推荐使用 3.3V 供电，避免不同模块电平设计造成风险 |
| MPU6050 GND | GND | 与 STM32 共地 |
| MPU6050 SCL | PB10 / I2C2_SCL | 硬件 I2C2 时钟线 |
| MPU6050 SDA | PB11 / I2C2_SDA | 硬件 I2C2 数据线 |
| MPU6050 AD0 | GND 或悬空 | 常见模块内部下拉，7 位地址为 `0x68` |
| MPU6050 XCL | 不连接 | MPU6050 辅助 I2C 主机接口，本实验不用 |
| MPU6050 XDA | 不连接 | MPU6050 辅助 I2C 主机接口，本实验不用 |
| MPU6050 INT | 不连接 | 本实验采用主机主动读取，不使用数据就绪中断 |
| OLED SCL | 以原 OLED 工程为准 | 建议继续使用原有独立软件 I2C 引脚 |
| OLED SDA | 以原 OLED 工程为准 | 避免与 PB10、PB11 的 I2C2 配置冲突 |
| ST-Link V2 | PA13、PA14、GND、3.3V | SWD 下载与调试 |

> 注意：I2C 的 SCL 和 SDA 都必须通过上拉电阻获得高电平。常见 GY-521 模块已经带有上拉电阻，但仍应检查模块原理图。多个模块并联时，上拉电阻并联后的等效阻值不能过小。

## 工程文件结构

建议工程目录按下面方式组织：

```text
23-I2C-Hardware-MPU6050
├── Code
│   ├── main.c
│   ├── HardwareI2C.c
│   ├── HardwareI2C.h
│   ├── MPU6050.c
│   ├── MPU6050.h
│   ├── MPU6050_Reg.h
│   ├── OLED.c
│   ├── OLED.h
│   ├── Delay.c
│   └── Delay.h
├── Hardware
│   └── I2C2_MPU6050_Connection.png
├── Results
│   └── MPU6050_Hardware_I2C_Display.gif
└── README.md
```

## 核心代码

### 1. HardwareI2C.h

```c
#ifndef __HARDWARE_I2C_H                                      // 判断硬件 I2C 头文件是否尚未被包含，防止重复定义
#define __HARDWARE_I2C_H                                      // 定义头文件保护宏，确保本文件内容只编译一次

#include "stm32f10x.h"                                        // 引入 STM32F10x 标准外设库类型和 I2C 外设函数声明

void HardwareI2C_Init(void);                                  // 声明 I2C2 初始化函数，用于配置时钟、GPIO 和 I2C 工作参数
uint8_t HardwareI2C_WaitEvent(I2C_TypeDef *I2Cx, uint32_t Event); // 声明带超时的事件等待函数，防止通信异常时程序永久卡死
uint8_t HardwareI2C_WriteReg(uint8_t DeviceAddress, uint8_t RegAddress, uint8_t Data); // 声明指定设备寄存器写函数
uint8_t HardwareI2C_ReadReg(uint8_t DeviceAddress, uint8_t RegAddress, uint8_t *Data); // 声明指定设备寄存器单字节读函数

#endif                                                        // 结束头文件保护条件编译
```

### 2. HardwareI2C.c

```c
#include "stm32f10x.h"                                        // 引入 STM32F10x 标准外设库，获得 RCC、GPIO 和 I2C 操作接口
#include "HardwareI2C.h"                                      // 引入本模块函数声明，保证定义与声明保持一致

#define HARDWARE_I2C_TIMEOUT 100000U                           // 定义轮询超时计数上限，避免从机无响应时进入永久死循环

static uint8_t HardwareI2C_WaitBusIdle(void)                   // 定义仅供本文件使用的总线空闲等待函数
{                                                              // 开始总线空闲等待函数
    uint32_t Timeout = HARDWARE_I2C_TIMEOUT;                    // 初始化超时计数器，为 BUSY 标志等待设置最大次数
    while (I2C_GetFlagStatus(I2C2, I2C_FLAG_BUSY) == SET)       // 当 I2C2 的 BUSY 标志仍置位时持续等待总线释放
    {                                                          // 开始 BUSY 标志轮询循环
        if (Timeout == 0U)                                     // 判断等待次数是否已经耗尽
        {                                                      // 开始超时处理分支
            return 0U;                                         // 返回失败，提示上层总线可能被占用或卡死
        }                                                      // 结束超时处理分支
        Timeout--;                                             // 每轮等待将超时计数减一，形成有限等待机制
    }                                                          // 结束 BUSY 标志轮询循环
    return 1U;                                                 // 返回成功，表示总线当前处于空闲状态
}                                                              // 结束总线空闲等待函数

uint8_t HardwareI2C_WaitEvent(I2C_TypeDef *I2Cx, uint32_t Event) // 定义硬件 I2C 事件等待函数
{                                                              // 开始事件等待函数
    uint32_t Timeout = HARDWARE_I2C_TIMEOUT;                    // 初始化事件等待超时计数器
    while (I2C_CheckEvent(I2Cx, Event) != SUCCESS)              // 循环检查 SR1、SR2 是否满足指定组合事件
    {                                                          // 开始事件轮询循环
        if (Timeout == 0U)                                     // 判断目标事件是否在限定时间内仍未出现
        {                                                      // 开始事件超时处理分支
            I2C_GenerateSTOP(I2Cx, ENABLE);                     // 尝试申请停止条件，尽量释放当前占用的 I2C 总线
            I2C_AcknowledgeConfig(I2Cx, ENABLE);                // 恢复默认应答状态，避免影响下一次接收
            return 0U;                                         // 返回失败，让上层决定是否重试或报错
        }                                                      // 结束事件超时处理分支
        Timeout--;                                             // 每次轮询递减超时计数，避免无限等待
    }                                                          // 结束事件轮询循环
    return 1U;                                                 // 返回成功，表示指定 I2C 事件已经发生
}                                                              // 结束事件等待函数

void HardwareI2C_Init(void)                                    // 定义 I2C2 硬件外设初始化函数
{                                                              // 开始 I2C2 初始化函数
    RCC_APB1PeriphClockCmd(RCC_APB1Periph_I2C2, ENABLE);        // 开启 APB1 总线上的 I2C2 外设时钟
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOB, ENABLE);       // 开启 APB2 总线上的 GPIOB 外设时钟

    GPIO_InitTypeDef GPIO_InitStructure;                        // 定义 GPIO 初始化结构体，用于配置 PB10 和 PB11
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_AF_OD;             // 配置为复用开漏输出，使引脚由 I2C2 外设控制且符合总线线与结构
    GPIO_InitStructure.GPIO_Pin = GPIO_Pin_10 | GPIO_Pin_11;    // 同时选择 PB10 作为 SCL、PB11 作为 SDA
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;           // 设置 GPIO 最大翻转速度，该参数不等于实际 I2C 时钟频率
    GPIO_Init(GPIOB, &GPIO_InitStructure);                      // 将上述 GPIO 参数写入 GPIOB 配置寄存器

    I2C_InitTypeDef I2C_InitStructure;                          // 定义 I2C 初始化结构体，用于配置 I2C2 工作参数
    I2C_InitStructure.I2C_Mode = I2C_Mode_I2C;                  // 选择标准 I2C 模式，而不是 SMBus 模式
    I2C_InitStructure.I2C_ClockSpeed = 400000;                  // 将总线时钟设置为 400 kHz，匹配 MPU6050 支持的快速模式上限
    I2C_InitStructure.I2C_DutyCycle = I2C_DutyCycle_2;          // 快速模式下选择低高电平时间比为 2:1
    I2C_InitStructure.I2C_Ack = I2C_Ack_Enable;                 // 默认使能自动应答，便于后续接收连续数据
    I2C_InitStructure.I2C_AcknowledgedAddress = I2C_AcknowledgedAddress_7bit; // 配置 STM32 从机地址格式为 7 位，本实验主机模式下仍需填写
    I2C_InitStructure.I2C_OwnAddress1 = 0x00;                   // 设置 STM32 自身地址，本实验只作为主机因此取不冲突的默认值
    I2C_Init(I2C2, &I2C_InitStructure);                         // 根据结构体参数配置 I2C2 的时序和控制寄存器

    I2C_Cmd(I2C2, ENABLE);                                     // 使能 I2C2 外设，使硬件开始接管 PB10 和 PB11
}                                                              // 结束 I2C2 初始化函数

uint8_t HardwareI2C_WriteReg(uint8_t DeviceAddress, uint8_t RegAddress, uint8_t Data) // 定义指定寄存器写函数
{                                                              // 开始指定寄存器写函数
    if (HardwareI2C_WaitBusIdle() == 0U)                        // 在发起通信前确认总线没有被其他事务占用
    {                                                          // 开始总线忙错误处理分支
        return 0U;                                             // 总线无法释放时立即返回失败
    }                                                          // 结束总线忙错误处理分支

    I2C_GenerateSTART(I2C2, ENABLE);                            // 由 I2C2 硬件产生起始条件并切换到主机模式
    if (HardwareI2C_WaitEvent(I2C2, I2C_EVENT_MASTER_MODE_SELECT) == 0U) // 等待 EV5，确认起始条件已经发送
    {                                                          // 开始 EV5 失败处理分支
        return 0U;                                             // 起始条件未成功产生时返回失败
    }                                                          // 结束 EV5 失败处理分支

    I2C_Send7bitAddress(I2C2, DeviceAddress, I2C_Direction_Transmitter); // 发送从机地址，并由库函数把最低读写位设置为写
    if (HardwareI2C_WaitEvent(I2C2, I2C_EVENT_MASTER_TRANSMITTER_MODE_SELECTED) == 0U) // 等待 EV6，确认从机已应答写地址
    {                                                          // 开始发送地址失败处理分支
        return 0U;                                             // 地址阶段无应答时返回失败
    }                                                          // 结束发送地址失败处理分支

    I2C_SendData(I2C2, RegAddress);                             // 把目标寄存器地址写入数据寄存器并交给硬件发送
    if (HardwareI2C_WaitEvent(I2C2, I2C_EVENT_MASTER_BYTE_TRANSMITTING) == 0U) // 等待 EV8，确认寄存器地址已进入发送流程
    {                                                          // 开始寄存器地址发送失败处理分支
        return 0U;                                             // 寄存器地址未正常发送时返回失败
    }                                                          // 结束寄存器地址发送失败处理分支

    I2C_SendData(I2C2, Data);                                  // 将需要写入目标寄存器的数据交给 I2C2 发送
    if (HardwareI2C_WaitEvent(I2C2, I2C_EVENT_MASTER_BYTE_TRANSMITTED) == 0U) // 等待 EV8_2，确认数据寄存器和移位寄存器均已发送完
    {                                                          // 开始寄存器数据发送失败处理分支
        return 0U;                                             // 数据没有完整发出时返回失败
    }                                                          // 结束寄存器数据发送失败处理分支

    I2C_GenerateSTOP(I2C2, ENABLE);                             // 产生停止条件，结束本次指定寄存器写事务
    return 1U;                                                 // 返回成功，表示写寄存器流程完整执行
}                                                              // 结束指定寄存器写函数

uint8_t HardwareI2C_ReadReg(uint8_t DeviceAddress, uint8_t RegAddress, uint8_t *Data) // 定义指定寄存器单字节读函数
{                                                              // 开始指定寄存器读函数
    if (Data == 0)                                             // 检查输出指针是否为空，避免向无效地址写入数据
    {                                                          // 开始空指针错误处理分支
        return 0U;                                             // 输出指针无效时立即返回失败
    }                                                          // 结束空指针错误处理分支

    if (HardwareI2C_WaitBusIdle() == 0U)                        // 等待 I2C 总线进入空闲状态
    {                                                          // 开始总线忙错误处理分支
        return 0U;                                             // 总线持续忙时返回失败
    }                                                          // 结束总线忙错误处理分支

    I2C_GenerateSTART(I2C2, ENABLE);                            // 产生第一次起始条件，准备先向从机写入寄存器地址
    if (HardwareI2C_WaitEvent(I2C2, I2C_EVENT_MASTER_MODE_SELECT) == 0U) // 等待 EV5，确认主机模式建立
    {                                                          // 开始第一次 EV5 失败处理分支
        return 0U;                                             // 起始条件失败时返回
    }                                                          // 结束第一次 EV5 失败处理分支

    I2C_Send7bitAddress(I2C2, DeviceAddress, I2C_Direction_Transmitter); // 以发送方向寻址 MPU6050，准备指定内部寄存器
    if (HardwareI2C_WaitEvent(I2C2, I2C_EVENT_MASTER_TRANSMITTER_MODE_SELECTED) == 0U) // 等待 EV6，确认写方向寻址成功
    {                                                          // 开始写方向寻址失败处理分支
        return 0U;                                             // 从机未应答写地址时返回
    }                                                          // 结束写方向寻址失败处理分支

    I2C_SendData(I2C2, RegAddress);                             // 发送目标寄存器地址，设置 MPU6050 内部寄存器指针
    if (HardwareI2C_WaitEvent(I2C2, I2C_EVENT_MASTER_BYTE_TRANSMITTED) == 0U) // 等待 EV8_2，保证寄存器地址完整发出
    {                                                          // 开始寄存器地址发送失败处理分支
        return 0U;                                             // 寄存器地址发送失败时返回
    }                                                          // 结束寄存器地址发送失败处理分支

    I2C_GenerateSTART(I2C2, ENABLE);                            // 不释放总线，直接产生重复起始条件以切换通信方向
    if (HardwareI2C_WaitEvent(I2C2, I2C_EVENT_MASTER_MODE_SELECT) == 0U) // 再次等待 EV5，确认重复起始条件完成
    {                                                          // 开始重复起始失败处理分支
        return 0U;                                             // 重复起始未成功时返回
    }                                                          // 结束重复起始失败处理分支

    I2C_Send7bitAddress(I2C2, DeviceAddress, I2C_Direction_Receiver); // 以接收方向重新寻址同一从机，准备读取寄存器数据
    if (HardwareI2C_WaitEvent(I2C2, I2C_EVENT_MASTER_RECEIVER_MODE_SELECTED) == 0U) // 等待 EV6，确认读方向寻址成功
    {                                                          // 开始读方向寻址失败处理分支
        return 0U;                                             // 从机未应答读地址时返回
    }                                                          // 结束读方向寻址失败处理分支

    I2C_AcknowledgeConfig(I2C2, DISABLE);                       // 在接收最后一个字节前关闭 ACK，使主机在该字节后发送非应答
    I2C_GenerateSTOP(I2C2, ENABLE);                             // 提前申请停止条件，让硬件在最后一个字节完成后释放总线

    if (HardwareI2C_WaitEvent(I2C2, I2C_EVENT_MASTER_BYTE_RECEIVED) == 0U) // 等待 EV7，确认接收数据寄存器已有有效字节
    {                                                          // 开始数据接收失败处理分支
        I2C_AcknowledgeConfig(I2C2, ENABLE);                    // 失败退出前恢复默认 ACK，避免影响下一次通信
        return 0U;                                             // 未收到数据时返回失败
    }                                                          // 结束数据接收失败处理分支

    *Data = I2C_ReceiveData(I2C2);                              // 读取数据寄存器，并通过指针把结果返回给调用者
    I2C_AcknowledgeConfig(I2C2, ENABLE);                        // 恢复 ACK 使能，为后续可能的多字节接收恢复默认状态
    return 1U;                                                 // 返回成功，表示目标寄存器数据已经读出
}                                                              // 结束指定寄存器读函数
```

> 说明：上面的单字节接收流程采用课程中常见的标准外设库事件写法。STM32F1 的硬件 I2C 对单字节、双字节和多字节接收的 ACK、ADDR、STOP 操作顺序较敏感；在高可靠性项目中，应结合 RM0008、芯片勘误表和 ST 官方接收流程进一步优化，并在关键操作期间考虑中断影响。

### 3. MPU6050_Reg.h

```c
#ifndef __MPU6050_REG_H                                       // 判断 MPU6050 寄存器定义头文件是否尚未包含
#define __MPU6050_REG_H                                       // 定义头文件保护宏，防止寄存器宏重复定义

#define MPU6050_SMPLRT_DIV      0x19                           // 定义采样率分频寄存器地址，用于设置传感器输出采样频率
#define MPU6050_CONFIG          0x1A                           // 定义配置寄存器地址，用于设置数字低通滤波器和外部同步
#define MPU6050_GYRO_CONFIG     0x1B                           // 定义陀螺仪配置寄存器地址，用于设置自检和满量程
#define MPU6050_ACCEL_CONFIG    0x1C                           // 定义加速度计配置寄存器地址，用于设置自检、量程和高通滤波
#define MPU6050_ACCEL_XOUT_H    0x3B                           // 定义 X 轴加速度高 8 位数据寄存器地址
#define MPU6050_ACCEL_XOUT_L    0x3C                           // 定义 X 轴加速度低 8 位数据寄存器地址
#define MPU6050_ACCEL_YOUT_H    0x3D                           // 定义 Y 轴加速度高 8 位数据寄存器地址
#define MPU6050_ACCEL_YOUT_L    0x3E                           // 定义 Y 轴加速度低 8 位数据寄存器地址
#define MPU6050_ACCEL_ZOUT_H    0x3F                           // 定义 Z 轴加速度高 8 位数据寄存器地址
#define MPU6050_ACCEL_ZOUT_L    0x40                           // 定义 Z 轴加速度低 8 位数据寄存器地址
#define MPU6050_GYRO_XOUT_H     0x43                           // 定义 X 轴角速度高 8 位数据寄存器地址
#define MPU6050_GYRO_XOUT_L     0x44                           // 定义 X 轴角速度低 8 位数据寄存器地址
#define MPU6050_GYRO_YOUT_H     0x45                           // 定义 Y 轴角速度高 8 位数据寄存器地址
#define MPU6050_GYRO_YOUT_L     0x46                           // 定义 Y 轴角速度低 8 位数据寄存器地址
#define MPU6050_GYRO_ZOUT_H     0x47                           // 定义 Z 轴角速度高 8 位数据寄存器地址
#define MPU6050_GYRO_ZOUT_L     0x48                           // 定义 Z 轴角速度低 8 位数据寄存器地址
#define MPU6050_PWR_MGMT_1      0x6B                           // 定义电源管理寄存器 1 地址，用于休眠控制和时钟源选择
#define MPU6050_PWR_MGMT_2      0x6C                           // 定义电源管理寄存器 2 地址，用于各轴待机和低功耗唤醒
#define MPU6050_WHO_AM_I        0x75                           // 定义芯片身份寄存器地址，默认读取结果通常为 0x68

#endif                                                        // 结束 MPU6050 寄存器定义头文件保护
```

### 4. MPU6050.h

```c
#ifndef __MPU6050_H                                           // 判断 MPU6050 驱动头文件是否尚未包含
#define __MPU6050_H                                           // 定义头文件保护宏，防止重复声明

#include "stm32f10x.h"                                        // 引入标准整数类型和 STM32F10x 基础定义

typedef struct                                                 // 定义用于打包六轴原始数据的结构体类型
{                                                              // 开始 MPU6050 数据结构体
    int16_t AccX;                                              // 保存 X 轴加速度原始有符号 16 位数据
    int16_t AccY;                                              // 保存 Y 轴加速度原始有符号 16 位数据
    int16_t AccZ;                                              // 保存 Z 轴加速度原始有符号 16 位数据
    int16_t GyroX;                                             // 保存 X 轴角速度原始有符号 16 位数据
    int16_t GyroY;                                             // 保存 Y 轴角速度原始有符号 16 位数据
    int16_t GyroZ;                                             // 保存 Z 轴角速度原始有符号 16 位数据
} MPU6050_DataTypeDef;                                        // 将结构体类型命名为 MPU6050_DataTypeDef

uint8_t MPU6050_Init(void);                                   // 声明 MPU6050 初始化函数，返回通信和配置是否成功
uint8_t MPU6050_GetID(uint8_t *ID);                            // 声明芯片 ID 读取函数，通过指针返回 WHO_AM_I 数据
uint8_t MPU6050_GetData(MPU6050_DataTypeDef *Data);            // 声明六轴数据读取函数，通过结构体返回全部原始数据

#endif                                                        // 结束 MPU6050 驱动头文件保护
```

### 5. MPU6050.c

```c
#include "stm32f10x.h"                                        // 引入 STM32F10x 标准外设库和基础数据类型
#include "HardwareI2C.h"                                      // 引入硬件 I2C 寄存器读写接口
#include "MPU6050_Reg.h"                                      // 引入 MPU6050 常用寄存器地址宏
#include "MPU6050.h"                                          // 引入 MPU6050 驱动函数和数据结构体声明

#define MPU6050_ADDRESS 0xD0                                  // 定义标准库函数使用的左移后地址，7 位地址 0x68 左移一位得到 0xD0

static uint8_t MPU6050_WriteReg(uint8_t RegAddress, uint8_t Data) // 定义 MPU6050 指定寄存器写函数，仅供本驱动文件调用
{                                                              // 开始指定寄存器写函数
    return HardwareI2C_WriteReg(MPU6050_ADDRESS, RegAddress, Data); // 调用硬件 I2C 通信层完成地址、寄存器和数据发送
}                                                              // 结束指定寄存器写函数

static uint8_t MPU6050_ReadReg(uint8_t RegAddress, uint8_t *Data) // 定义 MPU6050 指定寄存器读函数，仅供本驱动文件调用
{                                                              // 开始指定寄存器读函数
    return HardwareI2C_ReadReg(MPU6050_ADDRESS, RegAddress, Data); // 调用硬件 I2C 通信层读取目标寄存器的一个字节
}                                                              // 结束指定寄存器读函数

static uint8_t MPU6050_ReadWord(uint8_t HighRegAddress, int16_t *Value) // 定义连续两个寄存器拼接为有符号 16 位数据的辅助函数
{                                                              // 开始 16 位数据读取辅助函数
    uint8_t HighByte;                                          // 定义变量保存高地址寄存器返回的高 8 位数据
    uint8_t LowByte;                                           // 定义变量保存相邻低地址寄存器返回的低 8 位数据

    if (Value == 0)                                            // 检查输出数据指针是否为空
    {                                                          // 开始输出指针错误处理分支
        return 0U;                                             // 指针无效时返回失败，避免非法内存访问
    }                                                          // 结束输出指针错误处理分支

    if (MPU6050_ReadReg(HighRegAddress, &HighByte) == 0U)       // 读取指定轴数据的高 8 位并检查通信状态
    {                                                          // 开始高字节读取失败处理分支
        return 0U;                                             // 高字节读取失败时终止本次数据拼接
    }                                                          // 结束高字节读取失败处理分支

    if (MPU6050_ReadReg((uint8_t)(HighRegAddress + 1U), &LowByte) == 0U) // 读取紧随其后的低 8 位寄存器
    {                                                          // 开始低字节读取失败处理分支
        return 0U;                                             // 低字节读取失败时终止本次数据拼接
    }                                                          // 结束低字节读取失败处理分支

    *Value = (int16_t)(((uint16_t)HighByte << 8) | LowByte);    // 将高字节左移后与低字节组合，并按补码解释为有符号数
    return 1U;                                                 // 返回成功，表示完整 16 位数据已经得到
}                                                              // 结束 16 位数据读取辅助函数

uint8_t MPU6050_Init(void)                                    // 定义 MPU6050 初始化函数
{                                                              // 开始 MPU6050 初始化函数
    HardwareI2C_Init();                                        // 初始化 I2C2，使 PB10 和 PB11 进入硬件 I2C 复用功能

    if (MPU6050_WriteReg(MPU6050_PWR_MGMT_1, 0x01) == 0U)      // 解除休眠并选择 X 轴陀螺仪作为更稳定的时钟源
    {                                                          // 开始电源管理寄存器 1 写入失败处理分支
        return 0U;                                             // 配置失败时返回错误
    }                                                          // 结束电源管理寄存器 1 写入失败处理分支

    if (MPU6050_WriteReg(MPU6050_PWR_MGMT_2, 0x00) == 0U)      // 使能全部加速度计和陀螺仪轴，不进入待机模式
    {                                                          // 开始电源管理寄存器 2 写入失败处理分支
        return 0U;                                             // 配置失败时返回错误
    }                                                          // 结束电源管理寄存器 2 写入失败处理分支

    if (MPU6050_WriteReg(MPU6050_SMPLRT_DIV, 0x09) == 0U)      // 设置采样率分频值为 9，使输出采样率按内部基准进行 10 分频
    {                                                          // 开始采样率分频寄存器写入失败处理分支
        return 0U;                                             // 配置失败时返回错误
    }                                                          // 结束采样率分频寄存器写入失败处理分支

    if (MPU6050_WriteReg(MPU6050_CONFIG, 0x06) == 0U)          // 选择较强的数字低通滤波配置，降低高频噪声
    {                                                          // 开始配置寄存器写入失败处理分支
        return 0U;                                             // 配置失败时返回错误
    }                                                          // 结束配置寄存器写入失败处理分支

    if (MPU6050_WriteReg(MPU6050_GYRO_CONFIG, 0x18) == 0U)     // 将陀螺仪满量程设置为正负 2000 度每秒
    {                                                          // 开始陀螺仪配置寄存器写入失败处理分支
        return 0U;                                             // 配置失败时返回错误
    }                                                          // 结束陀螺仪配置寄存器写入失败处理分支

    if (MPU6050_WriteReg(MPU6050_ACCEL_CONFIG, 0x18) == 0U)    // 将加速度计满量程设置为正负 16g
    {                                                          // 开始加速度计配置寄存器写入失败处理分支
        return 0U;                                             // 配置失败时返回错误
    }                                                          // 结束加速度计配置寄存器写入失败处理分支

    return 1U;                                                 // 返回成功，表示 MPU6050 已完成全部基础配置
}                                                              // 结束 MPU6050 初始化函数

uint8_t MPU6050_GetID(uint8_t *ID)                             // 定义芯片 ID 获取函数
{                                                              // 开始芯片 ID 获取函数
    return MPU6050_ReadReg(MPU6050_WHO_AM_I, ID);              // 读取 WHO_AM_I 寄存器并通过指针返回芯片身份值
}                                                              // 结束芯片 ID 获取函数

uint8_t MPU6050_GetData(MPU6050_DataTypeDef *Data)             // 定义六轴原始数据读取函数
{                                                              // 开始六轴数据读取函数
    if (Data == 0)                                             // 检查调用者提供的数据结构体指针是否有效
    {                                                          // 开始结构体指针错误处理分支
        return 0U;                                             // 指针为空时返回失败
    }                                                          // 结束结构体指针错误处理分支

    if (MPU6050_ReadWord(MPU6050_ACCEL_XOUT_H, &Data->AccX) == 0U) // 读取并拼接 X 轴加速度高低字节
    {                                                          // 开始 X 轴加速度读取失败处理分支
        return 0U;                                             // 本轴读取失败时终止整组数据读取
    }                                                          // 结束 X 轴加速度读取失败处理分支

    if (MPU6050_ReadWord(MPU6050_ACCEL_YOUT_H, &Data->AccY) == 0U) // 读取并拼接 Y 轴加速度高低字节
    {                                                          // 开始 Y 轴加速度读取失败处理分支
        return 0U;                                             // 本轴读取失败时终止整组数据读取
    }                                                          // 结束 Y 轴加速度读取失败处理分支

    if (MPU6050_ReadWord(MPU6050_ACCEL_ZOUT_H, &Data->AccZ) == 0U) // 读取并拼接 Z 轴加速度高低字节
    {                                                          // 开始 Z 轴加速度读取失败处理分支
        return 0U;                                             // 本轴读取失败时终止整组数据读取
    }                                                          // 结束 Z 轴加速度读取失败处理分支

    if (MPU6050_ReadWord(MPU6050_GYRO_XOUT_H, &Data->GyroX) == 0U) // 读取并拼接 X 轴角速度高低字节
    {                                                          // 开始 X 轴角速度读取失败处理分支
        return 0U;                                             // 本轴读取失败时终止整组数据读取
    }                                                          // 结束 X 轴角速度读取失败处理分支

    if (MPU6050_ReadWord(MPU6050_GYRO_YOUT_H, &Data->GyroY) == 0U) // 读取并拼接 Y 轴角速度高低字节
    {                                                          // 开始 Y 轴角速度读取失败处理分支
        return 0U;                                             // 本轴读取失败时终止整组数据读取
    }                                                          // 结束 Y 轴角速度读取失败处理分支

    if (MPU6050_ReadWord(MPU6050_GYRO_ZOUT_H, &Data->GyroZ) == 0U) // 读取并拼接 Z 轴角速度高低字节
    {                                                          // 开始 Z 轴角速度读取失败处理分支
        return 0U;                                             // 本轴读取失败时终止整组数据读取
    }                                                          // 结束 Z 轴角速度读取失败处理分支

    return 1U;                                                 // 返回成功，表示六个轴的数据均已读取完成
}                                                              // 结束六轴数据读取函数
```

### 6. main.c

```c
#include "stm32f10x.h"                                        // 引入 STM32F10x 标准外设库基础定义
#include "OLED.h"                                              // 引入 OLED 显示函数，用于显示芯片 ID 和六轴数据
#include "Delay.h"                                             // 引入毫秒延时函数，用于控制数据显示刷新速度
#include "MPU6050.h"                                          // 引入 MPU6050 初始化和数据读取接口

int main(void)                                                 // 定义程序入口函数
{                                                              // 开始主函数
    uint8_t MPU6050_ID = 0;                                   // 定义变量保存 WHO_AM_I 寄存器返回的芯片 ID
    MPU6050_DataTypeDef MPU6050_Data;                          // 定义结构体变量保存三轴加速度和三轴角速度原始数据

    OLED_Init();                                               // 初始化 OLED 显示屏，为数据显示准备界面
    OLED_ShowString(1, 1, "ID:");                              // 在 OLED 第 1 行显示芯片 ID 标签
    OLED_ShowString(1, 8, "HW I2C");                          // 在 OLED 第 1 行右侧标记当前使用硬件 I2C

    if (MPU6050_Init() == 0U)                                 // 初始化 MPU6050 并判断硬件 I2C 配置或寄存器写入是否失败
    {                                                          // 开始初始化失败处理分支
        OLED_ShowString(2, 1, "INIT ERROR");                   // 在 OLED 上显示初始化失败提示
        while (1)                                              // 进入错误保持循环，便于调试时观察故障状态
        {                                                      // 开始初始化错误保持循环
        }                                                      // 结束初始化错误保持循环
    }                                                          // 结束初始化失败处理分支

    if (MPU6050_GetID(&MPU6050_ID) == 0U)                     // 读取芯片 ID 并检查 I2C 通信是否成功
    {                                                          // 开始芯片 ID 读取失败处理分支
        OLED_ShowString(2, 1, "ID ERROR");                     // 在 OLED 上显示芯片 ID 读取失败提示
        while (1)                                              // 进入错误保持循环，防止继续使用无效数据
        {                                                      // 开始 ID 错误保持循环
        }                                                      // 结束 ID 错误保持循环
    }                                                          // 结束芯片 ID 读取失败处理分支

    OLED_ShowHexNum(1, 4, MPU6050_ID, 2);                      // 以两位十六进制格式显示 WHO_AM_I 寄存器值
    OLED_ShowString(2, 1, "AX");                               // 在第 2 行显示 X 轴加速度标签
    OLED_ShowString(3, 1, "AY");                               // 在第 3 行显示 Y 轴加速度标签
    OLED_ShowString(4, 1, "AZ");                               // 在第 4 行显示 Z 轴加速度标签
    OLED_ShowString(2, 8, "GX");                               // 在第 2 行右侧显示 X 轴角速度标签
    OLED_ShowString(3, 8, "GY");                               // 在第 3 行右侧显示 Y 轴角速度标签
    OLED_ShowString(4, 8, "GZ");                               // 在第 4 行右侧显示 Z 轴角速度标签

    while (1)                                                  // 进入主循环，持续读取并刷新 MPU6050 数据
    {                                                          // 开始主循环
        if (MPU6050_GetData(&MPU6050_Data) != 0U)              // 读取六轴原始数据并判断通信是否成功
        {                                                      // 开始数据读取成功处理分支
            OLED_ShowSignedNum(2, 3, MPU6050_Data.AccX, 5);    // 在第 2 行显示 X 轴加速度原始有符号数
            OLED_ShowSignedNum(3, 3, MPU6050_Data.AccY, 5);    // 在第 3 行显示 Y 轴加速度原始有符号数
            OLED_ShowSignedNum(4, 3, MPU6050_Data.AccZ, 5);    // 在第 4 行显示 Z 轴加速度原始有符号数
            OLED_ShowSignedNum(2, 10, MPU6050_Data.GyroX, 5);  // 在第 2 行右侧显示 X 轴角速度原始有符号数
            OLED_ShowSignedNum(3, 10, MPU6050_Data.GyroY, 5);  // 在第 3 行右侧显示 Y 轴角速度原始有符号数
            OLED_ShowSignedNum(4, 10, MPU6050_Data.GyroZ, 5);  // 在第 4 行右侧显示 Z 轴角速度原始有符号数
        }                                                      // 结束数据读取成功处理分支
        else                                                   // 当任意一次寄存器读取失败时进入错误提示分支
        {                                                      // 开始数据读取失败处理分支
            OLED_ShowString(1, 8, "I2C ERR");                  // 在 OLED 顶部显示通信错误提示，便于定位总线故障
        }                                                      // 结束数据读取失败处理分支

        Delay_ms(50);                                          // 延时 50ms 限制刷新频率，避免显示闪烁和无意义的高速轮询
    }                                                          // 结束主循环
}                                                              // 结束主函数
```

## 代码要点

| 行/段 | 说明 |
|---|---|
| `GPIO_Mode_AF_OD` | 将 PB10、PB11 配置为复用开漏模式，由 I2C2 外设控制电平，同时依赖上拉电阻产生高电平。 |
| `RCC_APB1Periph_I2C2` | I2C2 挂载在 APB1 总线上，使用前必须开启其外设时钟。 |
| `I2C_ClockSpeed = 400000` | 设置 I2C 总线速率为 400kHz；接线较长或干扰较大时可先降至 100kHz 排错。 |
| `I2C_GenerateSTART()` | 申请起始或重复起始条件，并使 STM32 进入 I2C 主机模式。 |
| `I2C_Send7bitAddress()` | 发送从机地址并自动设置最低读写位；标准库参数通常使用左移后的地址 `0xD0`。 |
| `I2C_EVENT_MASTER_MODE_SELECT` | EV5，表示起始条件已发送，主机模式已经建立。 |
| `I2C_EVENT_MASTER_TRANSMITTER_MODE_SELECTED` | EV6 发送方向，表示从机已经应答写地址。 |
| `I2C_EVENT_MASTER_BYTE_TRANSMITTING` | EV8，表示数据寄存器可继续写入下一字节，但前一字节未必全部发送结束。 |
| `I2C_EVENT_MASTER_BYTE_TRANSMITTED` | EV8_2，表示数据寄存器和移位寄存器均空，当前字节已完整发送。 |
| `I2C_EVENT_MASTER_RECEIVER_MODE_SELECTED` | EV6 接收方向，表示从机已经应答读地址。 |
| `I2C_EVENT_MASTER_BYTE_RECEIVED` | EV7，表示数据寄存器中已经收到一个有效字节。 |
| `I2C_AcknowledgeConfig(DISABLE)` | 单字节接收结束前关闭 ACK，使主机对最后一个字节返回 NACK，通知从机停止发送。 |
| `HardwareI2C_WaitEvent()` | 给事件等待增加超时机制，避免地址错误、断线或从机掉电时程序永久卡死。 |
| `MPU6050_ADDRESS 0xD0` | MPU6050 的 7 位地址为 `0x68`；左移一位后为 `0xD0`，最低位由读写方向函数处理。 |
| `WHO_AM_I` | 通过读取 `0x75` 寄存器验证通信链路和从机地址是否正确，常见结果为 `0x68`。 |
| 高低字节拼接 | MPU6050 每个轴输出 16 位补码数据，必须先读高 8 位，再读低 8 位并组合为 `int16_t`。 |

## 关键知识点

### 1. STM32 硬件 I2C 外设

#### 原理

硬件 I2C 由 STM32 内部专用外设完成起始、停止、时钟生成、地址发送、应答检测和数据移位。

CPU 不再像软件 I2C 那样逐次翻转 GPIO，而是：

```text
写控制寄存器或数据寄存器
        ↓
I2C 外设自动产生总线波形
        ↓
状态寄存器产生对应事件
        ↓
程序检查事件并执行下一步
```

#### 特点

- SCL 时钟和 SDA 数据时序由硬件自动产生。
- 波形周期更规整，通信速度更稳定。
- CPU 不需要逐位操作 GPIO。
- 必须使用芯片规定的 I2C 复用引脚。
- 配置和接收流程比软件 I2C 更复杂。
- 可进一步配合中断或 DMA 减少轮询开销。

#### 面试易问

**Q：硬件 I2C 和软件 I2C 的主要区别是什么？**

A：硬件 I2C 使用芯片内部专用外设自动产生时序，速度稳定、CPU 占用低，但引脚固定且状态机复杂；软件 I2C 使用普通 GPIO 模拟协议，移植和换脚方便，但占用 CPU，时序精度受程序执行影响。

**Q：使用硬件 I2C 后，CPU 是否完全不参与通信？**

A：不是。轮询方式下，CPU 仍需要配置外设、写入数据、读取数据并等待事件；只有结合中断或 DMA 后，才能进一步降低 CPU 的持续等待。

#### 易错点

- 误把 PB10、PB11 配置成普通开漏输出，而不是复用开漏输出。
- 忘记开启 I2C2 的 APB1 时钟。
- 使用了不属于目标 I2C 外设的 GPIO 引脚。
- 认为硬件 I2C 会自动完成完整寄存器协议，而忽略软件仍需组织地址和数据顺序。

---

### 2. 复用开漏输出与上拉电阻

#### 原理

I2C 的 SDA 和 SCL 是开漏结构。设备只能主动把总线拉低，不能主动输出高电平。

当所有设备都释放总线时，上拉电阻将线路拉到高电平；任意设备拉低时，总线立即变为低电平，从而实现多设备共享和线与逻辑。

#### 特点

- 高电平由上拉电阻产生。
- 低电平由主机或从机的 MOSFET 主动拉低。
- 多个设备可以安全连接到同一组 SDA、SCL。
- 上升沿速度由上拉电阻和总线电容共同决定。
- 复用模式表示引脚输出控制权交给 I2C 外设。

#### 面试易问

**Q：为什么 I2C 必须使用开漏输出？**

A：因为多个设备可能同时连接总线。开漏结构只允许设备主动拉低，避免一个设备输出高电平、另一个输出低电平造成推挽短路，同时还能实现仲裁和时钟同步。

**Q：上拉电阻越小越好吗？**

A：不是。阻值过大时上升沿过慢，可能无法满足高速通信；阻值过小时低电平电流和功耗增大，也可能超过器件灌电流能力，需要根据总线电容和速率选择。

#### 易错点

- 忘记接上拉电阻，导致线路悬空或始终无法形成有效高电平。
- 多个模块自带上拉电阻并联，使总等效阻值过小。
- 使用长杜邦线并直接运行 400kHz，导致波形畸变和偶发错误。
- 把 GPIO 输出速度误认为 I2C 的实际总线频率。

---

### 3. I2C 事件状态机

#### 原理

STM32F1 的标准外设库通过组合检查 SR1 和 SR2 状态位，把硬件状态封装成事件。

本实验的主要事件链如下：

```text
EV5：起始条件发送完成
 ↓
EV6：从机地址发送并收到 ACK
 ↓
EV8：发送数据寄存器可继续写入
 ↓
EV8_2：当前数据完全发送结束
 ↓
EV7：接收数据寄存器中已有有效字节
```

程序只有在当前事件成立后，才能安全执行下一步。

#### 特点

- 事件对应硬件状态，不是普通的软件延时。
- 不同通信方向使用不同的 EV6 事件。
- EV8 和 EV8_2 的完成程度不同。
- 轮询事件写法直观，适合入门。
- 不加超时会在从机无响应时永久阻塞。

#### 面试易问

**Q：EV8 和 EV8_2 有什么区别？**

A：EV8 表示数据寄存器为空，可以继续写入下一字节，但移位寄存器可能仍在发送；EV8_2 表示数据寄存器和移位寄存器都为空，当前字节已经完整发送，适合在发送最后一个字节后等待。

**Q：为什么地址发送后必须等待 EV6？**

A：EV6 表示地址阶段完成且从机已经应答。没有 EV6 通常说明地址错误、从机未供电、线路异常或上拉问题，继续发送数据没有意义。

#### 易错点

- 把 EV8 当成最后一个字节完全发送完成。
- 发送方向和接收方向使用了错误的 EV6 事件。
- 完全依赖死循环等待事件，没有设置超时退出。
- 忽略错误标志，只检查正常事件，导致故障原因难以定位。

---

### 4. 7 位地址与左移后地址

#### 原理

MPU6050 的 I2C 从机地址为 7 位：

```text
AD0 = 0：0b1101000 = 0x68
AD0 = 1：0b1101001 = 0x69
```

总线上真正发送的地址字节由 7 位地址左移一位，再在最低位加入读写方向：

```text
0x68 << 1 = 0xD0
写：0xD0
读：0xD1
```

STM32F10x 标准外设库的 `I2C_Send7bitAddress()` 常使用左移后的地址参数，再根据方向自动设置最低位。

#### 特点

- 数据手册通常写 7 位地址 `0x68`。
- 总线地址字节写方向为 `0xD0`。
- 总线地址字节读方向为 `0xD1`。
- AD0 引脚决定地址最低位。
- 不同库对地址参数格式的要求可能不同。

#### 面试易问

**Q：为什么 MPU6050 地址有时写 `0x68`，有时写 `0xD0`？**

A：`0x68` 是不包含读写位的 7 位地址；`0xD0` 是左移一位后的写地址字节。具体传给函数哪个值，取决于所使用库函数的地址参数约定。

**Q：HAL 库和标准外设库的地址写法一定相同吗？**

A：不一定。必须查看函数文档和实现，确认函数需要 7 位地址还是左移后的地址，不能只凭函数名猜测。

#### 易错点

- 把 `0x68` 直接传入要求左移地址的函数，导致寻址失败。
- 已经写成 `0xD0` 后又左移一次。
- 手动把读写位写入地址，又让库函数重复修改最低位。
- AD0 实际为高电平，却仍固定使用 `0x68` 地址。

---

### 5. 指定寄存器读与重复起始条件

#### 原理

读取 MPU6050 指定寄存器时，主机必须先告诉从机要读取哪个寄存器，再切换到接收方向：

```text
START
→ 从机地址 + 写
→ 寄存器地址
→ Repeated START
→ 从机地址 + 读
→ 接收数据
→ NACK
→ STOP
```

中间的重复起始条件不会释放总线，因此可以保证寄存器指针设置和数据读取属于同一完整事务。

#### 特点

- 第一次寻址使用写方向。
- 寄存器地址本质上也是主机发送的数据。
- 第二次寻址使用读方向。
- 重复起始条件不会产生总线空闲间隙。
- 适合“先指定内部地址，再读取数据”的寄存器型器件。

#### 面试易问

**Q：为什么不能一开始就直接用读方向寻址？**

A：因为从机需要先知道主机要读取哪个内部寄存器。先以写方向发送寄存器地址，再用重复起始切换为读方向，才能读取指定位置。

**Q：重复起始和先 STOP 再 START 有什么区别？**

A：重复起始在不释放总线的情况下开始新一段寻址，可保持事务原子性；先 STOP 会释放总线，其他主机理论上可能在两段操作之间占用总线。

#### 易错点

- 发送寄存器地址后直接等待接收数据，忘记重新寻址为读方向。
- 第二次起始后仍然使用发送方向事件。
- 误把重复起始理解成重新初始化 I2C 外设。
- 在寄存器地址发送完成前过早产生重复起始。

---

### 6. ACK、NACK 与单字节接收

#### 原理

接收数据时，主机通过 ACK 告诉从机是否还要继续发送：

- ACK：已经收到当前字节，请继续发送下一个字节。
- NACK：当前字节是最后一个字节，请停止发送。

因此，单字节读取时，主机必须在最后一个字节结束前准备好 NACK 和 STOP。

#### 特点

- ACK 是接收方对发送方的反馈。
- 单字节读取只接收一个数据字节。
- 最后一个字节必须返回 NACK。
- STOP 的申请时机与硬件状态机密切相关。
- STM32F1 的 1、2、N 字节接收流程并不完全相同。

#### 面试易问

**Q：为什么接收最后一个字节时要发送 NACK？**

A：NACK 用来通知从机主机不再需要更多数据，从机随后释放 SDA，主机才能正常产生停止条件结束事务。

**Q：如果最后一个字节仍发送 ACK 会怎样？**

A：从机会认为主机还需要下一个字节，可能继续驱动数据线，使主机的停止流程和下一次通信出现异常。

#### 易错点

- 读取最后一个字节后才关闭 ACK，时机过晚。
- 单字节、双字节和多字节接收使用同一套简单流程。
- 通信失败退出后没有恢复 ACK 使能。
- 接收过程中被高优先级中断打断，破坏关键操作时序。

---

### 7. 软件 I2C 与硬件 I2C 的分层替换

#### 原理

上一节工程可以抽象为三层：

```text
应用层：main.c
    ↓
设备驱动层：MPU6050.c
    ↓
通信层：MyI2C.c
```

本节只把最底层替换为：

```text
通信层：HardwareI2C.c
```

只要上层仍提供“写寄存器”和“读寄存器”接口，MPU6050 初始化和数据处理逻辑就无需大幅修改。

#### 特点

- 降低模块之间的耦合。
- 便于软件 I2C 和硬件 I2C 相互切换。
- 设备驱动不需要关注底层波形如何产生。
- 主程序不需要直接调用复杂的 I2C 事件函数。
- 体现嵌入式驱动分层设计思想。

#### 面试易问

**Q：为什么要把 I2C 和 MPU6050 驱动写成两个模块？**

A：I2C 模块关注总线通信，MPU6050 模块关注寄存器和设备功能。分层后，换 MCU、换 I2C 实现或换传感器时，只需修改对应层，代码更容易复用和维护。

**Q：设备驱动层应该暴露底层 EV5、EV6 事件吗？**

A：通常不应该。硬件事件属于通信层细节，设备驱动层只应调用更抽象的读写寄存器接口。

#### 易错点

- 在 `main.c` 中直接堆叠所有 I2C 事件操作，导致代码难以维护。
- MPU6050 驱动依赖具体 GPIO 电平函数，无法切换到硬件 I2C。
- 通信层和设备层使用不同的地址格式。
- 替换通信层后仍保留旧软件 I2C 对同一引脚的初始化代码。

---

### 8. MPU6050 原始数据与量程

#### 原理

MPU6050 的每个轴输出一个 16 位二进制补码。高字节和低字节必须按以下方式组合：

```text
Value = (HighByte << 8) | LowByte
```

本实验配置：

```text
加速度量程：±16g
陀螺仪量程：±2000°/s
```

若需要换算成物理量，应根据所选量程对应的灵敏度系数计算。

#### 特点

- 每个轴占两个连续寄存器。
- 数据是有符号补码。
- 静止状态下加速度计仍会测到重力分量。
- 陀螺仪静止时可能存在零偏。
- 量程越大，单位物理量对应的数字分辨率越低。

#### 面试易问

**Q：为什么拼接后要存入 `int16_t`？**

A：传感器输出采用 16 位补码表示正负方向。使用 `int16_t` 能让最高位自动作为符号位解释，得到正确的正负原始值。

**Q：静止时加速度计三个轴为什么不全是 0？**

A：加速度计会测到重力加速度。传感器姿态不同，重力会投影到不同轴上，因此至少一个或多个轴会出现明显数值。

#### 易错点

- 高低字节顺序写反。
- 用 `uint16_t` 显示负方向数据，结果变成很大的正数。
- 更改量程寄存器后仍使用旧灵敏度系数。
- 把原始数据直接当作角度或位移使用。

---

### 9. 超时机制与总线恢复

#### 原理

如果从机没有应答，正常事件不会出现。没有超时机制时：

```c
while (I2C_CheckEvent(...) != SUCCESS);
```

会让程序永远卡住。

加入超时后，程序可以退出当前事务、产生 STOP、显示错误或重新初始化外设。

#### 特点

- 能区分“暂时等待”和“通信已经异常”。
- 避免传感器掉线拖死整个系统。
- 便于输出错误日志和定位故障阶段。
- 仍需要进一步处理 BUSY 卡死和错误标志。
- 高可靠性系统应设计重试次数和总线恢复策略。

#### 面试易问

**Q：I2C 总线 BUSY 一直为 1，常见原因是什么？**

A：SDA 被从机拉低、上次通信异常中断、主机复位而从机仍处于发送状态、GPIO 配置错误，或外设状态机没有正确复位。

**Q：如何恢复被从机拉住的 I2C 总线？**

A：常见方法是暂时把 SCL 配置为 GPIO，手动输出若干时钟让从机完成未结束的数据，再产生停止条件，最后重新初始化 I2C 外设；具体实现应结合芯片和从机行为设计。

#### 易错点

- 超时后只跳出循环，却没有释放总线或恢复 ACK。
- 发生错误后立即无限重试，使系统持续占用 CPU。
- 把所有错误都当成地址错误，不检查 BUSY、AF、BERR 等标志。
- 只在调试版加超时，正式项目反而使用永久死循环。

---

## 本节核心记忆

```text
硬件 I2C = 复用开漏 GPIO + I2C 外设参数 + 事件状态机
```

```text
PB10 = I2C2_SCL
PB11 = I2C2_SDA
```

```text
指定寄存器读：
START → 地址写 → 寄存器地址 → 重复 START → 地址读 → 数据 → NACK → STOP
```

```text
EV5：主机模式
EV6：地址应答
EV8：正在发送
EV8_2：发送完成
EV7：收到数据
```

```text
MPU6050：
7 位地址 0x68
左移后写地址 0xD0
左移后读地址 0xD1
```

```text
最后一个接收字节必须 NACK
所有事件等待都应设计超时
```

## 开发过程总结

### 问题 1：程序卡在等待 EV6

现象：

- 程序进入 `HardwareI2C_WaitEvent()` 后不再运行
- OLED 停留在初始化界面
- 逻辑分析仪能看到 START，但地址后没有 ACK

排查过程：

1. 检查 MPU6050 是否正确供电并与 STM32 共地
2. 检查 SCL、SDA 是否接反
3. 检查 PB10、PB11 是否配置为 `GPIO_Mode_AF_OD`
4. 检查地址参数使用的是 `0xD0` 还是 `0x68`
5. 测量 SCL、SDA 空闲时是否均为高电平
6. 检查 AD0 电平是否改变了从机地址

解决方案：

- 按标准库函数要求传入左移后的地址
- 确认模块上拉电阻有效
- 先把总线速率降低到 100kHz
- 使用带超时的事件等待函数输出故障，而不是永久阻塞

### 问题 2：I2C_FLAG_BUSY 上电后一直置位

现象：

- 尚未发送 START，总线就显示忙
- 重新下载程序后偶尔恢复，按复位键后又失败
- SDA 或 SCL 中有一根始终为低电平

排查过程：

1. 断开 MPU6050，观察总线是否恢复高电平
2. 检查是否有其他模块与 PB10、PB11 冲突
3. 检查上一次通信是否在中途复位
4. 检查 GPIO 是否被其他初始化代码重新配置
5. 检查从机是否处于未完成的发送状态

解决方案：

- 彻底断电重启主机和从机
- 手动输出 SCL 恢复时钟，再生成 STOP
- 软件复位并重新初始化 I2C2
- 避免在事务中途复位 MCU

### 问题 3：WHO_AM_I 不是 0x68

现象：

- 通信函数返回成功，但显示值不是 `0x68`
- 显示值固定为 `0x00` 或 `0xFF`
- 移动传感器时六轴数据也没有变化

排查过程：

1. 确认读取的寄存器地址是 `0x75`
2. 确认寄存器地址阶段使用写方向
3. 确认第二次寻址使用读方向
4. 检查是否遗漏重复起始条件
5. 检查读取数据前是否正确等待 EV7

解决方案：

- 严格按指定寄存器读时序执行
- 使用逻辑分析仪检查地址、ACK 和数据字节
- 先只测试 ID，不要同时加入六轴数据和 OLED 刷新逻辑

### 问题 4：数据偶尔跳变或通信不稳定

现象：

- 大部分时间显示正常，偶尔出现 `I2C ERR`
- 400kHz 下失败，100kHz 下正常
- 杜邦线移动后故障概率明显变化

排查过程：

1. 检查线长和接触质量
2. 检查上拉电阻等效阻值
3. 检查供电是否稳定
4. 用示波器观察上升沿是否过慢
5. 检查 OLED 等模块是否给电源带来干扰

解决方案：

- 缩短 SCL、SDA 连接线
- 降低总线速率到 100kHz
- 在模块电源附近增加去耦电容
- 优化上拉电阻并避免过多模块并联

### 问题 5：读取到的负数显示异常

现象：

- 传感器向反方向运动时显示接近 65535 的大数
- 加速度或角速度无法显示负值
- 高低字节拼接结果明显不合理

排查过程：

1. 检查结果变量是否为 `int16_t`
2. 检查高字节是否先左移 8 位
3. 检查低字节是否通过按位或拼接
4. 检查 OLED 是否使用有符号数显示函数

解决方案：

- 将拼接结果强制转换为 `int16_t`
- 使用 `OLED_ShowSignedNum()` 显示
- 保持高字节在前、低字节在后

### 问题 6：OLED 正常，但 MPU6050 硬件 I2C 不工作

现象：

- OLED 可以显示固定字符
- MPU6050 初始化始终报错
- 修改 OLED 代码后 MPU6050 状态发生变化

排查过程：

1. 检查 OLED 是否也错误使用 PB10、PB11
2. 检查软件 I2C 模块是否重新配置了同一组引脚
3. 检查多个驱动是否重复初始化 I2C2
4. 检查 OLED 和 MPU6050 是否共享总线但地址或驱动流程不兼容

解决方案：

- 保持 OLED 使用原独立引脚，MPU6050 使用 PB10、PB11
- 若共享同一硬件总线，统一使用一个 I2C 通信层
- 删除对 PB10、PB11 的重复 GPIO 初始化

## 结果展示

> 实验 1：程序启动后，OLED 显示 MPU6050 的 `WHO_AM_I` 值，正常情况下为 `0x68`。✅

> 实验 2：移动或旋转 MPU6050 时，OLED 上 AX、AY、AZ、GX、GY、GZ 六个原始数据持续变化。✅

> 实验 3：SCL 和 SDA 波形由 I2C2 外设自动产生，时钟周期比 GPIO 模拟方式更加规整。✅

> 实验 4：断开 MPU6050 或修改错误地址后，超时机制能够退出等待并显示通信错误，而不是让程序永久卡死。✅

## 本节小结

本节使用 STM32F103 的 I2C2 外设完成了 MPU6050 的寄存器配置和六轴数据读取，掌握了硬件 I2C 的初始化、事件等待、地址发送、重复起始、ACK/NACK 和停止条件控制。

最重要的知识点是：

```text
硬件 I2C 不需要软件逐位产生波形，
但必须严格按照外设事件状态机推进通信流程。
```

```text
写寄存器：
EV5 → EV6 → EV8 → EV8_2 → STOP
```

```text
读寄存器：
EV5 → EV6 → EV8_2 → 重复 EV5 → 接收 EV6 → NACK/STOP → EV7
```

软件 I2C 更容易理解协议和更换引脚；硬件 I2C 波形更规整、速度更稳定，并能进一步配合中断和 DMA。实际项目中应根据引脚资源、实时性、移植需求和可靠性要求选择合适方案。

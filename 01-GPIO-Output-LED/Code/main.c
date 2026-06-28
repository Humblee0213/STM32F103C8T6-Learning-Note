#include "stm32f10x.h"

int main(void)
{
	// 使能 GPIOC 时钟（APB2 总线）
	RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOC, ENABLE);

	// 配置 PC13 为推挽输出，50MHz
	GPIO_InitTypeDef GPIO_Initstructure;
	GPIO_Initstructure.GPIO_Mode = GPIO_Mode_Out_PP;
	GPIO_Initstructure.GPIO_Pin = GPIO_Pin_13;
	GPIO_Initstructure.GPIO_Speed = GPIO_Speed_50MHz;
	GPIO_Init(GPIOC, &GPIO_Initstructure);

	// 输出低电平 -> 点亮板载 LED（Active Low）
	GPIO_ResetBits(GPIOC, GPIO_Pin_13);

	while (1)
	{
		// 保持常亮
	}
}

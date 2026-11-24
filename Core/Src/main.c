/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.c
  * @brief          : Main program body
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2026 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */
/* USER CODE END Header */
/* Includes ------------------------------------------------------------------*/
#include "main.h"
#include "dma.h"
#include "usart.h"
#include "gpio.h"
#include "OLED.h"
/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include <string.h>
#include <stdio.h>
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */

/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/

/* USER CODE BEGIN PV */

volatile uint32_t last = 0;
int led_pos = 0;
volatile int run = 1;
volatile int dir = 1;
volatile uint16_t step = 500;
volatile char message[60];

typedef struct      /* 自定义结构体 */
{
  uint8_t head;
  uint8_t RUN;
  uint8_t DIR;
  uint8_t STEP_L;
  uint8_t STEP_H;
  uint8_t CHECK;
} Packet;

Packet pkt;
uint8_t byte;
uint8_t cnt = 0;

volatile uint8_t ack = 0;
volatile uint8_t dirty = 1;   // 1 = 屏幕需要刷新
volatile uint8_t busy = 0;

char state1[10] = "run  ";
char state0[10] = "stop ";
char rank1[10] = "forward ";
char rank0[10] = "break   ";

/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
/* USER CODE BEGIN PFP */
static void led(int n);
static void keys();
static void cmd();
static void show();
/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */

static void led(const int n)   /* 灯的定量 */
{
  HAL_GPIO_WritePin(LED0_GPIO_Port, LED0_Pin, n == 0 ? GPIO_PIN_SET : GPIO_PIN_RESET);
  HAL_GPIO_WritePin(LED1_GPIO_Port, LED1_Pin, n == 1 ? GPIO_PIN_SET : GPIO_PIN_RESET);
  HAL_GPIO_WritePin(LED2_GPIO_Port, LED2_Pin, n == 2 ? GPIO_PIN_SET : GPIO_PIN_RESET);
}

/**
  * @brief  扫描两个按键（消抖，按一次只动作一次）
  * @note   按键接地上拉：松开 = 1，按下 = 0
  */
static void keys()
{
  static uint8_t old1 = 1, stable1 = 1;   /* 按键 1：上次电平、确认过的电平 */
  static uint8_t old2 = 1, stable2 = 1;   /* 按键 2：同上 */
  static uint32_t t1 = 0, t2 = 0;         /* 电平变化的时刻 */

  uint32_t now = HAL_GetTick();
  uint8_t b1 = HAL_GPIO_ReadPin(BTN1_GPIO_Port, BTN1_Pin);
  uint8_t b2 = HAL_GPIO_ReadPin(BTN2_GPIO_Port, BTN2_Pin);

  /* ---- 按键 1：切换方向 ---- */
  if (b1 != old1)                 /* 电平刚变化，重新开始计时 */
  {
    old1 = b1;
    t1 = now;
  }
  if (now - t1 >= 20 && b1 != stable1)   /* 稳定 20ms 才认 */
  {
    stable1 = b1;
    if (b1 == 0)                  /* 只在按下（低电平）那一刻动作 */
    {
      dir = -dir;
      last = now;
      dirty = 1;
      ack = 1;
    }
  }

  /* ---- 按键 2：切换暂停 / 继续（写法同上） ---- */
  if (b2 != old2)
  {
    old2 = b2;
    t2 = now;
  }
  if (now - t2 >= 20 && b2 != stable2)
  {
    stable2 = b2;
    if (b2 == 0)
    {
      run = !run;
      last = now;
      dirty = 1;
      ack = 1;
    }
  }
}

static void cmd()
{
  uint8_t sum;
  uint16_t ms;

  sum = (uint8_t)((pkt.head + pkt.RUN + pkt.DIR + pkt.STEP_L + pkt.STEP_H) % 256);
  if (sum != pkt.CHECK)                                 /* 效验不通过则不执行 */
  {return;}

  ms = ((uint16_t)pkt.STEP_H << 8) | pkt.STEP_L;        /* 高低8字节拼为16位延时时间 */

  if (run == 0 && pkt.RUN == 1)                         /* 暂停启动，防灯跳闪 */
  {last = HAL_GetTick();}

  run = pkt.RUN;
  dir = (pkt.DIR == 1) ? -1 : 1;
  step = ms;
  ack = 1;
  dirty = 1;
}

static void show()
{
  uint16_t s = step;    /* 先存一份，防止画到一半被中断改掉 */

  /* 第 1 行：运行状态。RUN 后面补 2 个空格，和 PAUSE 一样宽 */
  OLED_ShowString(1, 1, "STATE: ");
  OLED_ShowString(1, 8, run ? "Run " : "Stop");

  /* 第 2 行：方向 */
  OLED_ShowString(2, 1, "DIR:   ");
  OLED_ShowString(2, 8, dir == 1 ? "Forward" : "Break  ");

  /* 第 3 行：间隔。先按 4 位画，小于 1000 时把最高位的 0 抹成空格 */
  OLED_ShowString(3, 1, "STEP:  ");
  OLED_ShowNum(3, 8, s, 4);


  if (run == 1 && dir == 1)
    sprintf(message,"%s  %s  %dms  %d ",state1,rank1,s,led_pos);
  else if (run == 0 && dir == 1)
    sprintf(message,"%s  %s  %dms  %d ",state0,rank1,s,led_pos);
  else if (run == 1 && dir == 0)
    sprintf(message,"%s  %s  %dms  %d ",state1,rank0,s,led_pos);
  else if (run == 0 && dir == 0)
    sprintf(message,"%s  %s  %dms  %d ",state0,rank0,s,led_pos);


  if (s < 1000)
  {
    OLED_ShowChar(3, 8, ' ');
  }
  OLED_ShowString(3, 12, "ms");

  /* 第 4 行：当前灯，直接显示 0/1/2（对应 LED0/LED1/LED2） */
  OLED_ShowString(4, 1, "LED:   ");
  OLED_ShowNum(4, 8, led_pos, 1);
}


/* USER CODE END 0 */

/**
  * @brief  The application entry point.
  * @retval int
  */
int main(void)
{

  /* USER CODE BEGIN 1 */

  /* USER CODE END 1 */

  /* MCU Configuration--------------------------------------------------------*/

  /* Reset of all peripherals, Initializes the Flash interface and the Systick. */
  HAL_Init();

  /* USER CODE BEGIN Init */

  /* USER CODE END Init */

  /* Configure the system clock */
  SystemClock_Config();

  /* USER CODE BEGIN SysInit */

  /* USER CODE END SysInit */

  /* Initialize all configured peripherals */
  MX_GPIO_Init();
  MX_DMA_Init();
  MX_USART2_UART_Init();
  /* USER CODE BEGIN 2 */

  led(0);
  HAL_UART_Receive_IT(&huart2, &byte, 1); /* 上电初始化，每次接收 1 个字节 */
  OLED_Init();
  show();

  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  while (1)
  {
    keys();

    if (dirty == 1)
    {
      dirty = 0;
      show();
    }

    if (run == 1 && HAL_GetTick() - last >= step)    /* 描述时间点，实现非阻塞 */
    {
      last = HAL_GetTick();                          /* 重置初始时间 */
      led_pos += dir;                                /* 累加dir对饮的数实现0、1、2的循环 */

      if (led_pos > 2)
      {led_pos = 0;}
      if (led_pos < 0)
      {led_pos = 2;}

      led(led_pos);
      OLED_ShowNum(4, 8, led_pos, 1);   /* 新增：只改 LED 编号这一位 */
    }


    if (ack == 1 && busy == 0 )
    {
      ack = 0;
      busy = 1;
      HAL_UART_Transmit_DMA(&huart2, message, sizeof(message));
    }

    /* USER CODE END WHILE */

    /* USER CODE BEGIN 3 */
  }
  /* USER CODE END 3 */
}

/**
  * @brief System Clock Configuration
  * @retval None
  */
void SystemClock_Config(void)
{
  RCC_OscInitTypeDef RCC_OscInitStruct = {0};
  RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

  /** Initializes the RCC Oscillators according to the specified parameters
  * in the RCC_OscInitTypeDef structure.
  */
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSE;
  RCC_OscInitStruct.HSEState = RCC_HSE_ON;
  RCC_OscInitStruct.HSEPredivValue = RCC_HSE_PREDIV_DIV1;
  RCC_OscInitStruct.HSIState = RCC_HSI_ON;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
  RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSE;
  RCC_OscInitStruct.PLL.PLLMUL = RCC_PLL_MUL9;
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
  {
    Error_Handler();
  }

  /** Initializes the CPU, AHB and APB buses clocks
  */
  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK
                              |RCC_CLOCKTYPE_PCLK1|RCC_CLOCKTYPE_PCLK2;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV2;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_2) != HAL_OK)
  {
    Error_Handler();
  }
}

/* USER CODE BEGIN 4 */

void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart)  /* 接收的回调函数 */
{
  if (cnt == 0 && byte != 0xAA)                          /* 防帧头出错的保护措施 */
  {}
  else
  {
    ((uint8_t *)&pkt)[cnt] = byte;   /* 按顺序写进结构体的第cnt个字节 */
    cnt++;

    if (cnt >= sizeof(pkt))       /* 6 个字节集齐一个完整包 */
    {
      cnt = 0;
      cmd();                      /* 校验并执行 */
    }
  }
  HAL_UART_Receive_IT(&huart2, &byte, 1);   /* 重新打开接收，才能收下一个字节 */
}

void HAL_UART_TxCpltCallback(UART_HandleTypeDef *huart)
{
  busy = 0;
}


/* USER CODE END 4 */

/**
  * @brief  This function is executed in case of error occurrence.
  * @retval None
  */
void Error_Handler(void)
{
  /* USER CODE BEGIN Error_Handler_Debug */
  __disable_irq();
  while (1)
  {
  }
  /* USER CODE END Error_Handler_Debug */
}
#ifdef USE_FULL_ASSERT
/**
  * @brief  Reports the name of the source file and the source line number
  *         where the assert_param error has occurred.
  * @param  file: pointer to the source file name
  * @param  line: assert_param error line source number
  * @retval None
  */
void assert_failed(uint8_t *file, uint32_t line)
{
  /* USER CODE BEGIN 6 */

  /* USER CODE END 6 */
}
#endif /* USE_FULL_ASSERT */

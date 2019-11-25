/* Copyright (c) Microsoft Corporation. All rights reserved.
   Licensed under the MIT License. */

#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>

#include "mt3620-baremetal.h"

#include "FreeRTOS.h"
#include "task.h"
#include "semphr.h"
#include "queue.h"

#include "printf.h"

#include "nnom.h"
#include "image.h"
#include "weights.h"

#include "mt3620-intercore.h"
#include "Log_Debug.h"

#define APP_STACK_SIZE_BYTES		(8192 / 4)

/// <summary>Base address of IO CM4 MCU Core clock.</summary>
static const uintptr_t IO_CM4_RGU = 0x2101000C;
static const uintptr_t IO_CM4_GPT_BASE = 0x21030000;
static TaskHandle_t NNTaskHandle;

#define INTERBUFOVERHEAD	20
#define MINST_DATA_SIZE		784	// 28 * 28
static uint8_t recvBuffer[MINST_DATA_SIZE + INTERBUFOVERHEAD];

static _Noreturn void DefaultExceptionHandler(void);
static _Noreturn void RTCoreMain(void);

extern uint32_t StackTop; // &StackTop == end of TCM0
extern void SVC_Handler(void);
extern void PendSV_Handler(void);
extern void SysTick_Handler(void);

// ARM DDI0403E.d SB1.5.2-3
// From SB1.5.3, "The Vector table must be naturally aligned to a power of two whose alignment
// value is greater than or equal to (Number of Exceptions supported x 4), with a minimum alignment
// of 128 bytes.". The array is aligned in linker.ld, using the dedicated section ".vector_table".

// The exception vector table contains a stack pointer, 15 exception handlers, and an entry for
// each interrupt.
#define INTERRUPT_COUNT 100 // from datasheet
#define EXCEPTION_COUNT (16 + INTERRUPT_COUNT)
#define INT_TO_EXC(i_) (16 + (i_))
const uintptr_t ExceptionVectorTable[EXCEPTION_COUNT] __attribute__((section(".vector_table")))
__attribute__((used)) = {
    [0] = (uintptr_t)&StackTop,					// Main Stack Pointer (MSP)
    [1] = (uintptr_t)RTCoreMain,				// Reset
    [2] = (uintptr_t)DefaultExceptionHandler,	// NMI
    [3] = (uintptr_t)DefaultExceptionHandler,	// HardFault
    [4] = (uintptr_t)DefaultExceptionHandler,	// MPU Fault
    [5] = (uintptr_t)DefaultExceptionHandler,	// Bus Fault
    [6] = (uintptr_t)DefaultExceptionHandler,	// Usage Fault
    [11] = (uintptr_t)SVC_Handler,				// SVCall
    [12] = (uintptr_t)DefaultExceptionHandler,	// Debug monitor
    [14] = (uintptr_t)PendSV_Handler,			// PendSV
    [15] = (uintptr_t)SysTick_Handler,			// SysTick
    [INT_TO_EXC(0)... INT_TO_EXC(INTERRUPT_COUNT - 1)] = (uintptr_t)DefaultExceptionHandler
};

static _Noreturn void DefaultExceptionHandler(void)
{
    for (;;) {
        // empty.
    }
}

void GPT3UsFreeRunTimerInit()
{
	// GPT3_INIT = initial counter value
	WriteReg32(IO_CM4_GPT_BASE, 0x54, 0x0);

	// GPT3_CTRL
	uint32_t ctrlOn = 0x0;
	ctrlOn |= (0x19) << 16; // OSC_CNT_1US (default value)
	ctrlOn |= 0x1;          // GPT3_EN = 1 -> GPT3 enabled
	WriteReg32(IO_CM4_GPT_BASE, 0x50, ctrlOn);
}

uint32_t GetCurrentUs()
{
	return ReadReg32(IO_CM4_GPT_BASE, 0x58);
}

void print_img(uint8_t* buf)
{
	char c;

	for (int y = 0; y < 28; y++) {
		for (int x = 0; x < 28; x++) {
			if (buf[y * 28 + x] == 0) {
				c = '_';
			} else {
				c = '@';
			}
			Log_Debug("%c%c", c, c);
		}
		Log_Debug("\r\n");
	}
}

static void NNTask(void* pParameters)
{
	nnom_model_t *model;
	uint32_t time;
	uint32_t predic_label;
	float prob;
	uint32_t index;

	model = nnom_model_create();

	BufferHeader* outbound, * inbound;
	uint32_t sharedBufSize = 0;

	if (GetIntercoreBuffers(&outbound, &inbound, &sharedBufSize) == -1) {
		Log_Debug("ERROR: GetIntercoreBuffers failed\r\n");
		while (1);
	}

	uint32_t recvSize = MINST_DATA_SIZE + INTERBUFOVERHEAD;

	while (1) {
		
		// waiting for incoming data
		if (DequeueData(outbound, inbound, sharedBufSize, &recvBuffer[0], &recvSize) == -1) {
			continue;
		}

		time = nnom_ms_get();
		memcpy(nnom_input_data, (int8_t*)&recvBuffer[INTERBUFOVERHEAD], MINST_DATA_SIZE);
		(void)nnom_predict(model, &predic_label, &prob);
		time = nnom_ms_get() - time;

		//print original image to console
		print_img((int8_t*)&recvBuffer[INTERBUFOVERHEAD]);

		Log_Debug("%d, probability: %d%%\r\n", predic_label, (int)(prob * 100));
		Log_Debug("Time: %d ms\n", time);
		//model_stat(model);

		// Send the result back to HL core
		recvBuffer[INTERBUFOVERHEAD] = (uint8_t)predic_label;
		EnqueueData(inbound, outbound, sharedBufSize, &recvBuffer[0], INTERBUFOVERHEAD + 1);
	}
}

static void TaskInit(void* pParameters) 
{
	xTaskCreate(NNTask, "NN Task", APP_STACK_SIZE_BYTES, NULL, 2, &NNTaskHandle);
	vTaskSuspend(NULL);
}

static _Noreturn void RTCoreMain(void)
{
    // SCB->VTOR = ExceptionVectorTable
    WriteReg32(SCB_BASE, 0x08, (uint32_t)ExceptionVectorTable);

	DebugUARTInit();
	Log_Debug("CM4F core start!\r\n");

	GPT3UsFreeRunTimerInit();

	// Boost M4 core to 197.6MHz (@26MHz), refer to chapter 3.3 in MT3620 Datasheet
	uint32_t val = ReadReg32(IO_CM4_RGU, 0);
	val &= 0xFFFF00FF;
	val |= 0x00000200;
	WriteReg32(IO_CM4_RGU, 0, val);

	xTaskCreate(TaskInit, "Init Task", APP_STACK_SIZE_BYTES, NULL, 7, NULL);
	vTaskStartScheduler();

	while (1);
}

// applicaiton hooks

void vApplicationStackOverflowHook(TaskHandle_t xTask, char* pcTaskName)
{
	while (1);
}

void vApplicationMallocFailedHook(void)
{
	while (1);
}

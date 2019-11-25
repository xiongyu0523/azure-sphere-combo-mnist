#include <stdbool.h>
#include <errno.h>
#include <string.h>
#include <time.h>
#include <pthread.h>

#include <applibs/log.h>
#include <applibs/gpio.h>
#include <sys/time.h>
#include <sys/socket.h>
#include <applibs/application.h>

#include "epoll_timerfd_utilities.h"
#include "delay.h"
#include "ili9341.h"
#include "text.h"
#include "ft6x06.h"

#define SQ_SIDE			168 // 28 * 6
#define SQ_LEFTUP_X		((ILI9341_LCD_PIXEL_WIDTH - SQ_SIDE) / 2)
#define SQ_LEFTUP_Y		((ILI9341_LCD_PIXEL_HEIGHT - SQ_SIDE) / 2)
#define SQ_RIGHTDOWN_X	((ILI9341_LCD_PIXEL_WIDTH - SQ_SIDE) / 2 + SQ_SIDE)
#define SQ_RIGHTDOWN_Y	((ILI9341_LCD_PIXEL_HEIGHT - SQ_SIDE) / 2 + SQ_SIDE)
#define R				10
#define MINST_BUF_SIZE	784	// 28 * 28

#define BUTTON_R		25

static uint8_t frameBuffer[SQ_SIDE * SQ_SIDE];
static uint8_t mnistBuffer[MINST_BUF_SIZE];

static void SocketEventHandler(EventData* eventData);
static const char rtAppComponentId[] = "8903cf17-d461-4d72-8293-0b7c5a56222b";
static int epollFd = 0;
static int rtSocketFd = 0;
static EventData socketEventData = { .eventHandler = &SocketEventHandler };

static void SocketEventHandler(EventData* eventData)
{
	uint8_t number;
	ssize_t bytesReceived = recv(rtSocketFd, &number, sizeof(number), 0);
	if (bytesReceived < 0) {
		Log_Debug("ERROR: Unable to receive message: %d (%s)\r\n", errno, strerror(errno));
		return;
	}

	lcd_set_text_cursor(241, 12);
	lcd_display_char(0x30 + number);
}

static void* epoll_thread(void* ptr)
{
	epollFd = CreateEpollFd();
	if (epollFd < 0) {
		return -1;
	}

	struct timeval to = { .tv_sec = 1, .tv_usec = 0 };
	int ret = setsockopt(rtSocketFd, SOL_SOCKET, SO_RCVTIMEO, &to, sizeof(to));
	if (ret == -1) {
		Log_Debug("ERROR: Unable to set socket timeout: %d (%s)\n", errno, strerror(errno));
		return -1;
	}

	if (RegisterEventHandlerToEpoll(epollFd, rtSocketFd, &socketEventData, EPOLLIN) != 0) {
		return -1;
	}

	while (1) {
		(void)WaitForEventAndCallHandler(epollFd);
	}
}

static void resize(uint8_t* p_frame_buffer, uint8_t* p_mnist_buffer)
{
	int resize_ratio = SQ_SIDE / 28;

	for (int y = 0; y < 28; y++) {
		for (int x = 0; x < 28; x++) {

			int in = (y * SQ_SIDE + x) * resize_ratio;
			int out = (y * 28 + x);

			p_mnist_buffer[out] = p_frame_buffer[in];
		}
	}
}

void print_img(uint8_t* buf)
{
	char point = '@';
	char empty = '_';
	char data;

	for (int y = 0; y < 28; y++)
	{
		for (int x = 0; x < 28; x++)
		{
			if (buf[y * 28 + x] == 0) {
				data = empty;
			}
			else {
				data = point;
			}
			Log_Debug("%c", data);
			Log_Debug("%c", data);
		}
		Log_Debug("\n");
	}
}


int main(void)
{
	pthread_t thread_id;
	uint16_t x, y, _x_, _y_;
	ssize_t bytesSent;

	Log_Debug("Example to demo handwritten classification\r\n");

	rtSocketFd = Application_Socket(rtAppComponentId);
	if (rtSocketFd == -1) {
		Log_Debug("ERROR: Unable to create socket: %d (%s)\n", errno, strerror(errno));
		return -1;
	}

	if (pthread_create(&thread_id, NULL, epoll_thread, NULL)) {
		Log_Debug("ERROR: creating thread fail\r\n");
		return -1;
	}

	ili9341_init();
	// draw box
	ili9341_draw_rect(SQ_LEFTUP_X - 1, SQ_LEFTUP_Y - 1, SQ_SIDE + 2, SQ_SIDE + 2, RED);

	lcd_set_text_size(2);
	lcd_set_text_cursor(70, 12);
	lcd_display_string("The number is \r\n");

	ft6x06_init();

	uint8_t identifyFlag = 0;

	while (1) {
		delay_ms(20);

		if (ft6x06_detect_touch() > 0) {
			ft6x06_get_xy(&x, &y);

			if ((x < BUTTON_R) && (y > (ILI9341_LCD_PIXEL_HEIGHT - BUTTON_R))) {
				ili9341_fill_rect(SQ_LEFTUP_X, SQ_LEFTUP_Y, SQ_SIDE, SQ_SIDE, WHITE);
				memset(&frameBuffer[0], 0, SQ_SIDE * SQ_SIDE);
			}

			if ((x > (ILI9341_LCD_PIXEL_WIDTH - BUTTON_R)) && (ILI9341_LCD_PIXEL_HEIGHT - BUTTON_R)) {
				identifyFlag = 1;
			}

			if ((x - R > SQ_LEFTUP_X) && (x + R < SQ_RIGHTDOWN_X) && (y - R > SQ_LEFTUP_Y) && (y + R < SQ_RIGHTDOWN_Y)) {

				_x_ = x - SQ_LEFTUP_X - R;
				_y_ = y - SQ_LEFTUP_Y - R;

				for (uint8_t row = 0; row < (2 * R + 1); row++) {
					memset(&frameBuffer[SQ_SIDE * (_y_ + row) + _x_], 127, (2 * R + 1));
				}

				ili9341_fillCircle(x, y, R, BLACK);
			}

			if (identifyFlag == 1) {
				identifyFlag = 0;

				resize(&frameBuffer[0], &mnistBuffer[0]);
				print_img(&mnistBuffer[0]);

				bytesSent = send(rtSocketFd, &mnistBuffer[0], MINST_BUF_SIZE, 0);
				if (bytesSent < 0) {
					Log_Debug("ERROR: Unable to send message: %d (%s)\r\n", errno, strerror(errno));
				}
				else if (bytesSent != MINST_BUF_SIZE) {
					Log_Debug("ERROR: Write %d bytes, expect %d bytes\r\n", bytesSent, MINST_BUF_SIZE);
				}
			}
		}
	}
}

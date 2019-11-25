#include <stdbool.h>
#include <errno.h>
#include <string.h>
#include <time.h>
#include <pthread.h>
#include <signal.h>

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

typedef enum {
	SM_IDLE,
	SM_DRAWING,
	SM_DONE,
	STATE_
} WorkStateMachine_t;

#define NO_TOUCH			0
#define INVALID_TOUCH		1
#define VALID_TOUCH			2

#define SQ_SIDE			168 // 28 * 6
#define SQ_LEFTUP_X		((ILI9341_LCD_PIXEL_WIDTH - SQ_SIDE) / 2)
#define SQ_LEFTUP_Y		((ILI9341_LCD_PIXEL_HEIGHT - SQ_SIDE) / 2)
#define SQ_RIGHTDOWN_X	((ILI9341_LCD_PIXEL_WIDTH - SQ_SIDE) / 2 + SQ_SIDE)
#define SQ_RIGHTDOWN_Y	((ILI9341_LCD_PIXEL_HEIGHT - SQ_SIDE) / 2 + SQ_SIDE)
#define R				10
#define MINST_BUF_SIZE	784	// 28 * 28

#define BUTTON_R		25

#define ACTIVE_AREA_SIZE	(SQ_SIDE * SQ_SIDE * 2)

static uint8_t frameBuffer[SQ_SIDE * SQ_SIDE];
const static uint8_t cleanBitmap[ACTIVE_AREA_SIZE] = { [0 ... (ACTIVE_AREA_SIZE - 1)] = 0xFF };
static uint8_t mnistBuffer[MINST_BUF_SIZE];

static void SocketEventHandler(EventData* eventData);
static void TimerEventHandler(EventData* eventData);
static const char rtAppComponentId[] = "8903cf17-d461-4d72-8293-0b7c5a56222b";
static int timerFd = 0;
static int epollFd = 0;
static int rtSocketFd = 0;

static EventData timerEventData = { .eventHandler = &TimerEventHandler };
static EventData socketEventData = { .eventHandler = &SocketEventHandler };

// Termination state
static volatile sig_atomic_t terminationRequired = false;

static void TerminationHandler(int signalNumber)
{
	// Don't use Log_Debug here, as it is not guaranteed to be async-signal-safe.
	terminationRequired = true;
}

static uint8_t checkTouchAndDrawPoint(void)
{
	uint16_t x, y, _x_, _y_;

	if (ft6x06_detect_touch() > 0) {
		ft6x06_get_xy(&x, &y);

		if ((x - R > SQ_LEFTUP_X) && (x + R < SQ_RIGHTDOWN_X) && (y - R > SQ_LEFTUP_Y) && (y + R < SQ_RIGHTDOWN_Y)) {

			_x_ = x - SQ_LEFTUP_X - R;
			_y_ = y - SQ_LEFTUP_Y - R;

			for (uint8_t row = 0; row < (2 * R + 1); row++) {
				memset(&frameBuffer[SQ_SIDE * (_y_ + row) + _x_], 127, (2 * R + 1));
			}

			ili9341_fillCircle(x, y, R, BLACK);

			return VALID_TOUCH;
		} else {
			return INVALID_TOUCH;
		}
	} else {
		return NO_TOUCH;
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

static void TimerEventHandler(EventData* eventData)
{
	uint16_t x, y, _x_, _y_;

	static WorkStateMachine_t workState = SM_IDLE;

#define DONE_TO		20 // 20 x 20 = 400ms
#define CLEAN_TO	50 // 50 x 20 = 1s

	static uint32_t done_count = DONE_TO;
	static uint32_t clean_count = CLEAN_TO;

	if (ConsumeTimerFdEvent(timerFd) != 0) {
		terminationRequired = true;
		return;
	}

	if (workState == SM_IDLE) {
		if (checkTouchAndDrawPoint() == VALID_TOUCH) {
			workState = SM_DRAWING;
			done_count = DONE_TO;
		}
	} else if (workState == SM_DRAWING) {
		uint8_t ret = checkTouchAndDrawPoint();
		if ((ret == NO_TOUCH) || (ret == INVALID_TOUCH)) {
			done_count--;
			if (done_count == 0) {
				workState = SM_DONE;

				resize(&frameBuffer[0], &mnistBuffer[0]);

				ssize_t bytesSent = send(rtSocketFd, &mnistBuffer[0], MINST_BUF_SIZE, 0);
				if (bytesSent < 0) {
					Log_Debug("ERROR: Unable to send message: %d (%s)\r\n", errno, strerror(errno));
				} else if (bytesSent != MINST_BUF_SIZE) {
					Log_Debug("ERROR: Write %d bytes, expect %d bytes\r\n", bytesSent, MINST_BUF_SIZE);
				}
			}
		} else {
			done_count = DONE_TO;
		}
	} else if (workState == SM_DONE) {
		clean_count--;
		if (clean_count == 0) {
			ili9341_draw_bitmap(SQ_LEFTUP_X, SQ_LEFTUP_Y, SQ_SIDE, SQ_SIDE, &cleanBitmap[0]);
			memset(&frameBuffer[0], 0, SQ_SIDE * SQ_SIDE);

			workState = SM_IDLE;
			clean_count = CLEAN_TO;
		}
	}
}

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

static int InitPeripheralsAndHandlers(void)
{
	struct sigaction action;
	memset(&action, 0, sizeof(struct sigaction));
	action.sa_handler = TerminationHandler;
	sigaction(SIGTERM, &action, NULL);

	epollFd = CreateEpollFd();
	if (epollFd < 0) {
		return -1;
	}

	static const struct timespec period = { .tv_sec = 0, .tv_nsec = 20000000 };
	timerFd = CreateTimerFdAndAddToEpoll(epollFd, &period, &timerEventData, EPOLLIN);
	if (timerFd < 0) {
		return -1;
	}
	if (RegisterEventHandlerToEpoll(epollFd, timerFd, &timerEventData, EPOLLIN) != 0) {
		return -1;
	}

	// Open connection to real-time capable application.
	rtSocketFd = Application_Socket(rtAppComponentId);
	if (rtSocketFd == -1) {
		Log_Debug("ERROR: Unable to create socket: %d (%s)\n", errno, strerror(errno));
		return -1;
	}

	// Set timeout, to handle case where real-time capable application does not respond.
	struct timeval to = { .tv_sec = 1, .tv_usec = 0 };
	int ret = setsockopt(rtSocketFd, SOL_SOCKET, SO_RCVTIMEO, &to, sizeof(to));
	if (ret == -1) {
		Log_Debug("ERROR: Unable to set socket timeout: %d (%s)\n", errno, strerror(errno));
		return -1;
	}

	if (RegisterEventHandlerToEpoll(epollFd, rtSocketFd, &socketEventData, EPOLLIN) != 0) {
		return -1;
	}

	ili9341_init();
	ili9341_draw_rect(SQ_LEFTUP_X - 1, SQ_LEFTUP_Y - 1, SQ_SIDE + 2, SQ_SIDE + 2, RED);
	lcd_set_text_size(2);
	lcd_set_text_cursor(70, 12);
	lcd_display_string("The number is \r\n");

	ft6x06_init();

	return 0;
}

static void CloseHandlers(void)
{
	Log_Debug("Closing file descriptors.\n");
	CloseFdAndPrintError(rtSocketFd, "Socket");
	CloseFdAndPrintError(timerFd, "Timer");
	CloseFdAndPrintError(epollFd, "Epoll");
}

int main(void)
{
	Log_Debug("Example to demo handwritten classification\r\n");

	if (InitPeripheralsAndHandlers() != 0) {
		terminationRequired = true;
	}

	while (!terminationRequired) {
		if (WaitForEventAndCallHandler(epollFd) != 0) {
			terminationRequired = true;
		}
	}

	CloseHandlers();
	Log_Debug("Application exiting.\n");
	return 0;
}
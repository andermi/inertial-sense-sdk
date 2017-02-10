/*
MIT LICENSE

Copyright 2014 Inertial Sense, LLC - http://inertialsense.com

Permission is hereby granted, free of charge, to any person obtaining a copy of this software and associated documentation files(the "Software"), to deal in the Software without restriction, including without limitation the rights to use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of the Software, and to permit persons to whom the Software is furnished to do so, subject to the following conditions :

The above copyright notice and this permission notice shall be included in all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
*/

#include "serialPort.h"
#include "serialPortPlatform.h"
#include "ISUtilities.h"

#if !defined(PLATFORM_IS_WINDOWS) && (defined(_WIN32) || defined(_WIN64))

#include <windows.h>

#define PLATFORM_IS_WINDOWS 1

#elif !defined(PLATFORM_IS_LINUX) && (TARGET_IPHONE_SIMULATOR || TARGET_OS_IPHONE || TARGET_OS_MAC || __linux || __unix__ )

#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/time.h>
#include <errno.h>
#include <termios.h>
#include <unistd.h>

// cygwin defines FIONREAD in socket.h instead of ioctl.h
#ifndef FIONREAD
#include <sys/socket.h>
#endif

#define PLATFORM_IS_LINUX 1

#ifndef error_message
#define error_message printf
#endif

#else

#error "Only Windows and Linux are currently supported."

#endif

typedef struct
{
	int blocking;

#if PLATFORM_IS_WINDOWS

	void* platformHandle;
	OVERLAPPED ovRead;
	OVERLAPPED ovWrite;
	int writePending;

#elif PLATFORM_IS_LINUX

	int fd;

#endif

} serialPortHandle;

#if PLATFORM_IS_WINDOWS

typedef struct
{
	OVERLAPPED ov;
	pfnSerialPortAsyncReadCompletion externalCompletion;
	serial_port_t* serialPort;
	unsigned char* buffer;
} readFileExCompletionStruct;

static void CALLBACK readFileExCompletion(DWORD errorCode, DWORD bytesTransferred, LPOVERLAPPED ov)
{
	readFileExCompletionStruct* c = (readFileExCompletionStruct*)ov;
	c->externalCompletion(c->serialPort, c->buffer, bytesTransferred, errorCode);
	free(c);
}

#elif PLATFORM_IS_LINUX

static int get_baud_speed(int baudRate)
{
	switch (baudRate)
	{
		default:      return 0;
		case 300:     return B300;
		case 600:     return B600;
		case 1200:    return B1200;
		case 2400:    return B2400;
		case 4800:    return B4800;
		case 9600:    return B9600;
		case 19200:   return B19200;
		case 38400:   return B38400;
		case 57600:   return B57600;
		case 115200:  return B115200;
		case 230400:  return B230400;
		case 460800:  return B460800;
		case 921600:  return B921600;
		case 1500000: return B1500000;
		case 2000000: return B2000000;
		case 2500000: return B2500000;
		case 3000000: return B3000000;
	}
}

static int set_interface_attribs(int fd, int speed, int parity)
{
	struct termios tty;
	memset(&tty, 0, sizeof tty);
	if (tcgetattr(fd, &tty) != 0)
	{
		error_message("error %d from tcgetattr", errno);
		return -1;
	}

	speed = get_baud_speed(speed);
	cfsetospeed(&tty, speed);
	cfsetispeed(&tty, speed);

	tty.c_cflag = (tty.c_cflag & ~CSIZE) | CS8;     // 8-bit chars
													// disable IGNBRK for mismatched speed tests; otherwise receive break
													// as \000 chars
	tty.c_iflag &= ~IGNBRK;         // disable break processing
	tty.c_lflag = 0;                // no signaling chars, no echo,
									// no canonical processing
	tty.c_oflag = 0;                // no remapping, no delays
	tty.c_cc[VMIN] = 0;             // read doesn't block
	tty.c_cc[VTIME] = 0;            // no timeout //0.5 seconds read timeout

	tty.c_iflag &= ~(IXON | IXOFF | IXANY); // shut off xon/xoff ctrl

	tty.c_cflag |= (CLOCAL | CREAD);// ignore modem controls,
									// enable reading
	tty.c_cflag &= ~(PARENB | PARODD);  // shut off parity
	tty.c_cflag |= parity;
	tty.c_cflag &= ~CSTOPB;
	tty.c_cflag &= ~CRTSCTS;

	if (tcsetattr(fd, TCSANOW, &tty) != 0)
	{
		error_message("error %d from tcsetattr", errno);
		return -1;
	}
	return 0;
}

// this function is no longer needed as the serial read loop takes care of timeouts and required byte read counts
// code causes glitches on Linux anyway but is left here in case someone wants to try and make blocking reads work better
static void set_blocking(int fd, int should_block)
{
	struct termios tty;
	memset(&tty, 0, sizeof tty);
	if (tcgetattr(fd, &tty) != 0)
	{
		error_message("error %d from tggetattr", errno);
		return;
	}

	tty.c_cc[VMIN] = should_block ? 1 : 0;
	tty.c_cc[VTIME] = 5;            // 0.5 seconds read timeout

	if (tcsetattr(fd, TCSANOW, &tty) != 0)
	{
		error_message("error %d setting term attributes", errno);
	}
}

#endif

static int serialPortOpenPlatform(serial_port_t* serialPort, const char* port, int baudRate, int blocking)
{
	if (serialPort->handle != 0)
	{
		// already open
		return 0;
	}

#if PLATFORM_IS_WINDOWS

	void* platformHandle = 0;
	serialPortSetPort(serialPort, port);
	platformHandle = CreateFileA(serialPort->port, GENERIC_READ | GENERIC_WRITE, 0, 0, OPEN_EXISTING, FILE_FLAG_OVERLAPPED, 0);
	if (platformHandle == INVALID_HANDLE_VALUE)
	{
		// don't modify the originally requested port value, just create a new value that Windows needs for COM10 and above
		char tmpPort[MAX_SERIAL_PORT_NAME_LENGTH];
		sprintf_s(tmpPort, sizeof(tmpPort) / sizeof(tmpPort[0]), "\\\\.\\%s", port);
		platformHandle = CreateFileA(tmpPort, GENERIC_READ | GENERIC_WRITE, 0, 0, OPEN_EXISTING, FILE_FLAG_OVERLAPPED, 0);
		if (platformHandle == INVALID_HANDLE_VALUE)
		{
			return 0;
		}
	}

	DCB serialParams;
	serialParams.DCBlength = sizeof(DCB);
	if (GetCommState(platformHandle, &serialParams))
	{
		serialParams.BaudRate = baudRate;
		serialParams.ByteSize = DATABITS_8;
		serialParams.StopBits = ONESTOPBIT;
		serialParams.Parity = NOPARITY;
		serialParams.fBinary = 1;
		serialParams.fInX = 0;
		serialParams.fOutX = 0;
		serialParams.fAbortOnError = 0;
		serialParams.fNull = 0;
		serialParams.fErrorChar = 0;
        serialParams.fDtrControl = DTR_CONTROL_ENABLE;
        serialParams.fRtsControl = RTS_CONTROL_ENABLE;
		if (!SetCommState(platformHandle, &serialParams))
		{
			serialPortClose(serialPort);
			return 0;
		}
	}
	else
	{
		serialPortClose(serialPort);
		return 0;
	}

	COMMTIMEOUTS timeouts = { (blocking ? 100 : MAXDWORD), 0, (blocking ? 1 : 0), 0, 0 };
	if (!SetCommTimeouts(platformHandle, &timeouts))
	{
		serialPortClose(serialPort);
		return 0;
	}
    SetupComm(platformHandle, 8192, 8192);

	serialPortHandle* handle = (serialPortHandle*)calloc(sizeof(serialPortHandle), 1);
	handle->blocking = blocking;

	// create the events for overlapped IO
	handle->ovRead.hEvent = CreateEvent(0, 1, 0, 0);
	handle->ovWrite.hEvent = CreateEvent(0, 1, 0, 0);
	handle->platformHandle = platformHandle;
	serialPort->handle = handle;

#elif PLATFORM_IS_LINUX

	int fd = open(port, O_RDWR | O_NOCTTY | O_NDELAY);
	if (fd < 0 || set_interface_attribs(fd, baudRate, 0) != 0)
	{
		return 0;
	}
	serialPortHandle* handle = (serialPortHandle*)calloc(sizeof(serialPortHandle), 1);
	handle->fd = fd;
	handle->blocking = blocking;
	serialPort->handle = handle;

	struct termios  config;

	 // Check if the file descriptor is pointing to a TTY device or not.
	 if(!isatty(fd))
	 	return 0;

	 // Get the current configuration of the serial interface
	 if(tcgetattr(fd, &config) < 0) 
	 	return 0;

	 // Input flags - Turn off input processing
	 //
	 // convert break to null byte, no CR to NL translation,
	 // no NL to CR translation, don't mark parity errors or breaks
	 // no input parity check, don't strip high bit off,
	 // no XON/XOFF software flow control
	 //
	 // config.c_iflag &= ~(IGNBRK | BRKINT | ICRNL | INLCR | PARMRK | INPCK | ISTRIP | IXON);
	 config.c_iflag = 0;

	 // Output flags - Turn off output processing
	 //
	 // no CR to NL translation, no NL to CR-NL translation,
	 // no NL to CR translation, no column 0 CR suppression,
	 // no Ctrl-D suppression, no fill characters, no case mapping,
	 // no local output processing
	 //
	 // config.c_oflag &= ~(OCRNL | ONLCR | ONLRET | ONOCR | ONOEOT| OFILL | OLCUC | OPOST);
	 // config.c_oflag &= ~(OCRNL | ONLCR | ONLRET | ONOCR | ONOEOT| OFILL | OLCUC | OPOST);
	 config.c_oflag = 0;

	 // No line processing
	 //
	 // echo off, echo newline off, canonical mode off, 
	 // extended input processing off, signal chars off
	 //
	 // config.c_lflag &= ~(ECHO | ECHONL | ICANON | IEXTEN | ISIG);
	 config.c_lflag = 0;

	 // Turn off character processing
	 //
	 // clear current char size mask, no parity checking,
	 // no output processing, force 8 bit input
	 config.c_cflag &= ~(CSIZE | PARENB);
	 config.c_cflag |= CS8;

	 // One input byte is enough to return from read()
	 // Inter-character timer off
	 config.c_cc[VMIN]  = 1;
	 config.c_cc[VTIME] = 0;

	 // Communication speed (simple version, using the predefined
	 // constants)
	 //
	 // if(cfsetispeed(&config, B9600) < 0 || cfsetospeed(&config, B9600) < 0) 
	 // 	return 0;

	 // Finally, apply the configuration
	 if(tcsetattr(fd, TCSAFLUSH, &config) < 0) 
	 	return 0;

#else

	return 0;

#endif

	return 1;	// success
}

static int serialPortClosePlatform(serial_port_t* serialPort)
{
	serialPortHandle* handle = (serialPortHandle*)serialPort->handle;
	if (handle == 0)
	{
		// not open, no close needed
		return 0;
	}

#if PLATFORM_IS_WINDOWS

	if (handle->ovRead.hEvent != 0)
	{
		// wait for any pending read operations, such as configuration, etc.
		CloseHandle(handle->ovRead.hEvent);
		handle->ovRead.hEvent = 0;
	}
	if (handle->ovWrite.hEvent != 0)
	{
		// wait for any pending write operations, such as configuration, etc.
		CloseHandle(handle->ovWrite.hEvent);
		handle->ovWrite.hEvent = 0;
	}
	CancelIo(handle->platformHandle);
	CloseHandle(handle->platformHandle);
	handle->platformHandle = 0;
	handle->writePending = 0;

#elif PLATFORM_IS_LINUX

	close(handle->fd);
	handle->fd = 0;

#endif

	free(handle);
	serialPort->handle = 0;

	return 1;
}

static int serialPortFlushPlatform(serial_port_t* serialPort)
{
	serialPortHandle* handle = (serialPortHandle*)serialPort->handle;

#if PLATFORM_IS_WINDOWS

	if (!PurgeComm(handle->platformHandle, PURGE_RXABORT | PURGE_RXCLEAR | PURGE_TXABORT | PURGE_TXCLEAR))
	{
		return 0;
	}

#elif PLATFORM_IS_LINUX

	tcflush(handle->fd, TCIOFLUSH);

#endif

	return 1;
}

#if PLATFORM_IS_WINDOWS

static int serialPortReadTimeoutPlatformWindows(serialPortHandle* handle, unsigned char* buffer, int readCount, int timeoutMilliseconds)
{
    DWORD dwRead = 0;
	DWORD error;
    int totalRead = 0;
	ULONGLONG startTime = GetTickCount64();
	while (1)
	{
		// perform the read operation
		if (ReadFile(handle->platformHandle, buffer + totalRead, readCount - totalRead, &dwRead, &handle->ovRead))
		{
			// read finished immediately
			totalRead += dwRead;
		}
		else
		{
			error = GetLastError();
			if (error == ERROR_IO_PENDING)
			{
				if (GetOverlappedResult(handle->platformHandle, &handle->ovRead, &dwRead, 1))
				{
					totalRead += dwRead;
				}
				else
				{
					// error, cancel IO
					CancelIo(handle->platformHandle);
				}
			}
			else
			{
				// error, cancel IO
				CancelIo(handle->platformHandle);
			}
		}
		if (timeoutMilliseconds < 1 || totalRead == readCount || GetTickCount64() - startTime > timeoutMilliseconds)
		{
			break;
		}
	}
    return totalRead;
}

#elif PLATFORM_IS_LINUX

static int serialPortReadTimeoutPlatformLinux(serialPortHandle* handle, unsigned char* buffer, int readCount, int timeoutMilliseconds)
{
	int totalRead = 0;
	int dtMs;
	int n;
	struct timeval start, curr;
	gettimeofday(&start, NULL);

	while (1)
	{
		n = read(handle->fd, buffer + totalRead, readCount - totalRead);
		if (n < -1)
		{
			error_message("error %d from read, fd %d", errno, handle->fd);
			return 0;
		}
		else if (n != -1)
		{
			totalRead += n;
		}
		if (timeoutMilliseconds > 0 && totalRead < readCount)
		{
			// sleep for 2 milliseconds to let more data arrive
			usleep(2000);
			gettimeofday(&curr, NULL);
			dtMs = ((curr.tv_sec - start.tv_sec) * 1000) + ((curr.tv_usec - start.tv_usec) / 1000);
			if (dtMs > timeoutMilliseconds)
			{
				break;
			}
		}
		else
		{
			break;
		}
	}
	return totalRead;
}

#endif

static int serialPortReadTimeoutPlatform(serial_port_t* serialPort, unsigned char* buffer, int readCount, int timeoutMilliseconds)
{
    serialPortHandle* handle = (serialPortHandle*)serialPort->handle;
	if (timeoutMilliseconds < 0)
	{
		timeoutMilliseconds = (handle->blocking ? SERIAL_PORT_DEFAULT_TIMEOUT : 0);
	}

#if PLATFORM_IS_WINDOWS

	return serialPortReadTimeoutPlatformWindows(handle, buffer, readCount, timeoutMilliseconds);

#elif PLATFORM_IS_LINUX

	return serialPortReadTimeoutPlatformLinux(handle, buffer, readCount, timeoutMilliseconds);

#else

	#error "Platform not implemented"
	return 0;

#endif

}

static int serialPortAsyncReadPlatform(serial_port_t* serialPort, unsigned char* buffer, int readCount, pfnSerialPortAsyncReadCompletion completion)
{
	serialPortHandle* handle = (serialPortHandle*)serialPort->handle;

#if PLATFORM_IS_WINDOWS

	readFileExCompletionStruct* c = (readFileExCompletionStruct*)malloc(sizeof(readFileExCompletionStruct));
	c->externalCompletion = completion;
	c->serialPort = serialPort;
	c->buffer = buffer;
	memset(&c->ov, 0, sizeof(c->ov));

	if (!ReadFileEx(handle->platformHandle, buffer, readCount, (LPOVERLAPPED)c, readFileExCompletion))
	{
		return 0;
	}

#elif PLATFORM_IS_LINUX

	// no support for async, just call the completion right away
	int n = read(handle->fd, buffer, readCount);
	completion(serialPort, buffer, (n < 0 ? 0 : n), (n >= 0 ? 0 : n));

#endif

	return 1;
}

static int serialPortWritePlatform(serial_port_t* serialPort, const unsigned char* buffer, int writeCount)
{
	serialPortHandle* handle = (serialPortHandle*)serialPort->handle;

#if PLATFORM_IS_WINDOWS

	DWORD dwWritten = 0;
	if (handle->writePending)
	{
		handle->writePending = 0;
		if (WaitForSingleObject(handle->ovWrite.hEvent, SERIAL_PORT_DEFAULT_TIMEOUT) != WAIT_OBJECT_0 ||
			!GetOverlappedResult(handle->platformHandle, &handle->ovWrite, &dwWritten, 0))
		{
			// error, cancel the IO
			CancelIo(handle->platformHandle);
		}
		dwWritten = 0;
	}

	if (!WriteFile(handle->platformHandle, buffer, writeCount, &dwWritten, &handle->ovWrite))
	{
		// if we have pending IO, see if we can process it right away or whether we should check the result of the write later
		if (GetLastError() == ERROR_IO_PENDING)
		{
			if (!GetOverlappedResult(handle->platformHandle, &handle->ovWrite, &dwWritten, 0))
			{
				// assume the write went through and return back immediately, the overlapped state can be handled on the next write call
				handle->writePending = 1;
				dwWritten = writeCount;
			}
		}
		else
		{
			// error, cancel the IO
			CancelIo(handle->platformHandle);
		}
	}

	return dwWritten;

#elif PLATFORM_IS_LINUX

	return write(handle->fd, buffer, writeCount);

	// if desired in the future, this will block until the data has been successfully written to the serial port
	/*
	int count = write(handle->fd, buffer, writeCount);
	int error = tcdrain(handle->fd);

	if (error != 0)
	{
	error_message("error %d from tcdrain", errno);
	return 0;
	}

	return count;
	*/

#endif

	if (serialPort->pfnWrite)
	{
		return serialPort->pfnWrite(serialPort, buffer, writeCount);
	}

	return 0;
}

static int serialPortGetByteCountAvailableToReadPlatform(serial_port_t* serialPort)
{
	serialPortHandle* handle = (serialPortHandle*)serialPort->handle;


#if PLATFORM_IS_WINDOWS

	COMSTAT commStat;
	if (ClearCommError(handle->platformHandle, 0, &commStat))
	{
		return commStat.cbInQue;
	}
	return 0;

#elif PLATFORM_IS_LINUX

	int bytesAvailable;
	ioctl(handle->fd, FIONREAD, &bytesAvailable);
	return bytesAvailable;

#else

	return 0;

#endif

}

static int serialPortGetByteCountAvailableToWritePlatform(serial_port_t* serialPort)
{
	// serialPortHandle* handle = (serialPortHandle*)serialPort->handle;
	(void)serialPort;

#if PLATFORM_IS_WINDOWS

	// we use overlapped IO, no limit on bytes that can be written or how fast, so we use an arbitrary number here of 64K
	return 65536;

#elif PLATFORM_IS_LINUX

	/*
	int bytesUsed;
	struct serial_struct serinfo;
	memset(&serinfo, 0, sizeof(serial_struct));
	ioctl(handle->fd, TIOCGSERIAL, &serinfo);
	ioctl(handle->fd, TIOCOUTQ, &bytesUsed);
	return serinfo.xmit_fifo_size - bytesUsed;
	*/

	// TODO: not sure how to include serial_struct... investigate later.
	return 65536;

#else

	return 0;

#endif

}

static int serialPortSleepPlatform(serial_port_t* serialPort, int sleepMilliseconds)
{
	(void)serialPort;

#if PLATFORM_IS_WINDOWS

	Sleep(sleepMilliseconds);

#elif PLATFORM_IS_LINUX

	usleep(sleepMilliseconds * 1000);

#else

	return 0;

#endif

	return 1;
}

int serialPortPlatformInit(serial_port_t* serialPort)
{

#if PLATFORM_IS_WINDOWS || PLATFORM_IS_LINUX

	serialPort->pfnClose = serialPortClosePlatform;
	serialPort->pfnFlush = serialPortFlushPlatform;
	serialPort->pfnOpen = serialPortOpenPlatform;
	serialPort->pfnRead = serialPortReadTimeoutPlatform;
	serialPort->pfnAsyncRead = serialPortAsyncReadPlatform;
	serialPort->pfnWrite = serialPortWritePlatform;
	serialPort->pfnGetByteCountAvailableToRead = serialPortGetByteCountAvailableToReadPlatform;
	serialPort->pfnGetByteCountAvailableToWrite = serialPortGetByteCountAvailableToWritePlatform;
	serialPort->pfnSleep = serialPortSleepPlatform;

	return 1;

#endif

	return 0;
}

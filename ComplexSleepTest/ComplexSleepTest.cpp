#include <Windows.h>

#include <chrono>
#include <iostream>

using the_clock = std::chrono::steady_clock;

struct timing {
	the_clock::duration total;
	the_clock::duration minimum;
	the_clock::duration maximum;
};

void waitForTick() {
	DWORD currentTick = timeGetTime();
	while (timeGetTime() == currentTick);
}

template <typename Body>
timing measureDuration(unsigned count, Body body) {
	timing result;
	bool first = true;
	result.total = the_clock::duration::zero();
	while (count--) {
		waitForTick();
		auto last = the_clock::now();
		body();
		auto now = the_clock::now();
		auto elapsed = now - last;
		if (first) {
			result.minimum = result.maximum = elapsed;
			first = false;
		}
		else {
			if (elapsed < result.minimum)
				result.minimum = elapsed;
			if (elapsed > result.maximum)
				result.maximum = elapsed;
		}
		result.total += elapsed;
	}
	return result;
}

HANDLE hPort = NULL;
PTP_TIMER timer = NULL;

void CALLBACK
timerCallback(PTP_CALLBACK_INSTANCE instance, PVOID context, PTP_TIMER theTimer)
{
	PostQueuedCompletionStatus(hPort, 0, 0xdeadbeef, NULL);
}

void setTimer(DWORD milliseconds)
{
	if (!timer) {
		timer = CreateThreadpoolTimer(timerCallback, NULL, NULL);
	}

	FILETIME ftDueTime;
	LARGE_INTEGER liTime;
	liTime.QuadPart = -(LONG)(milliseconds * 1000 * 10);

	ftDueTime.dwHighDateTime = liTime.HighPart;
	ftDueTime.dwLowDateTime = liTime.LowPart;

	SetThreadpoolTimer(timer, &ftDueTime, 0, 0);
}

void eventLoop()
{
	LPOVERLAPPED pOV = NULL;
	ULONG_PTR ulKey;
	DWORD dwBytes;

	while (GetQueuedCompletionStatus(hPort, &dwBytes, &ulKey, &pOV, INFINITE)) {
		if (ulKey == 0xdeadbeef) {
			break;
		} else {
			std::cout << "Unexpected key " << std::hex << ulKey << std::endl;
			exit(1);
		}
	}
}

BOOL timerActive = FALSE;
ULONGLONG ullWakeTime;

void setTimer2(DWORD milliseconds)
{
	ULONGLONG ullNow;
	QueryInterruptTime(&ullNow);

	ullWakeTime = ullNow + milliseconds * 1000 * 10;
	timerActive = TRUE;
}

#define MODE 2

void eventLoop2()
{
	LPOVERLAPPED pOV = NULL;
	ULONG_PTR ulKey;
	DWORD dwBytes;
	BOOL hiResTimer = FALSE;

	while (true) {
		DWORD dwSleepMs = INFINITE;

		if (timerActive) {
			ULONGLONG ullNow;

			QueryInterruptTime(&ullNow);

			if (ullNow >= ullWakeTime) {
				// Timer fired
				timerActive = FALSE;
				break;
			}

			dwSleepMs = DWORD((ullWakeTime - ullNow) / 10000);

#if MODE == 0
			// Adjust so we spin if it's less than a tick
			if (dwSleepMs < 15)
				dwSleepMs = 0;
			else
				dwSleepMs -= 15;
#elif MODE == 2
			if (dwSleepMs < 100 && !hiResTimer) {
				timeBeginPeriod(1);
				hiResTimer = TRUE;
			} else if (hiResTimer) {
				timeEndPeriod(1);
				hiResTimer = FALSE;
			}
#endif
		}
		
		BOOL bResult = GetQueuedCompletionStatus(hPort, &dwBytes, &ulKey, &pOV, dwSleepMs);

		if (!bResult && GetLastError() != WAIT_TIMEOUT) {
			std::cout << "Error " << std::hex << GetLastError() << std::endl;
			break;
		}
	}

	if (hiResTimer)
		timeEndPeriod(1);
}

int main()
{
	hPort = CreateIoCompletionPort(INVALID_HANDLE_VALUE, NULL, 0, 1);

	static const DWORD dwPeriods[] = { 1, 9, 10, 13, 14, 15, 16, 17, 25, 50, 100 };
	static const unsigned periodCount = sizeof(dwPeriods) / sizeof(dwPeriods[0]);
	static const unsigned reps = 10;

#if MODE == 1
	timeBeginPeriod(1);
#endif

	std::cout << "Dispatch style" << std::endl << std::endl;
	for (unsigned n = 0; n < periodCount; ++n) {
		DWORD dwPeriod = dwPeriods[n];
		auto result = measureDuration(reps, [dwPeriod] { setTimer(dwPeriod); eventLoop(); });
		using milliseconds = std::chrono::duration<double, std::milli>;
		auto average = std::chrono::duration_cast<milliseconds>(result.total).count() / double(reps);
		auto minimum = std::chrono::duration_cast<milliseconds>(result.minimum).count();
		auto maximum = std::chrono::duration_cast<milliseconds>(result.maximum).count();
		std::cout << "timer (" << dwPeriod << ") took on average " << average << "ms, with a minimum of " << minimum << "ms, and a maximum of " << maximum << "ms" << std::endl;
	}

	std::cout << std::endl;
	std::cout << "New style" << std::endl << std::endl;

	for (unsigned n = 0; n < periodCount; ++n) {
		DWORD dwPeriod = dwPeriods[n];
		auto result = measureDuration(reps, [dwPeriod] { setTimer2(dwPeriod); eventLoop2(); });
		using milliseconds = std::chrono::duration<double, std::milli>;
		auto average = std::chrono::duration_cast<milliseconds>(result.total).count() / double(reps);
		auto minimum = std::chrono::duration_cast<milliseconds>(result.minimum).count();
		auto maximum = std::chrono::duration_cast<milliseconds>(result.maximum).count();
		std::cout << "timer (" << dwPeriod << ") took on average " << average << "ms, with a minimum of " << minimum << "ms, and a maximum of " << maximum << "ms" << std::endl;
	}

	return 0;
}

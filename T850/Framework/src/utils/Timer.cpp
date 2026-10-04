#include <pch.h>
/*********************************************************
* Copyright (C) 2017 Daniel Enriquez (camus_mm@hotmail.com)
* All Rights Reserved
*
* You may use, distribute and modify this code under the
* following terms:
* ** Do not claim that you wrote this software
* ** A mention would be appreciated but not needed
* ** I do not and will not provide support, this software is "as is"
* ** Enjoy, learn and share.
*********************************************************/

#include <utils/Timer.h>
#include <stdio.h>
#ifdef OS_WEB
#include <emscripten.h>
#endif

void Timer::Init() {
#ifdef OS_WEB
	StartTime = emscripten_get_now();
	Frequency = 1000.0;
	Dt = DtSecs = 0.0;
#elif defined(OS_WINDOWS)
	LARGE_INTEGER y;
	QueryPerformanceFrequency(&y);
	Frequency = double(y.QuadPart) / 1000000.0;
	StartTime.QuadPart = 0;
	Dt = 0.0;
	QueryPerformanceCounter(&StartTime);
#elif defined(OS_LINUX) || defined(OS_ANDROID)
    gettimeofday(&StartTime,0);
    Frequency = 0.0;
    Dt = 0.0;
    DtSecs = 0.0;
#endif
}

void Timer::Update() {
#ifdef OS_WEB
	const double now = emscripten_get_now();
	DtSecs = (now - StartTime) / 1000.0;
	Dt = DtSecs * 1000000.0;
	StartTime = now;
#elif defined(OS_WINDOWS)
	LARGE_INTEGER end;
	QueryPerformanceCounter(&end);
	Dt = double(end.QuadPart - StartTime.QuadPart) / Frequency;
	QueryPerformanceCounter(&StartTime);
	DtSecs = (Dt / 1000000.0);
#elif defined(OS_LINUX) || defined(OS_ANDROID)
    timeval actual;
    gettimeofday(&actual,0);
    DtSecs = double( (actual.tv_sec - StartTime.tv_sec) + (actual.tv_usec - StartTime.tv_usec)/1000000.0);
    gettimeofday(&StartTime,0);
   // printf("FPS %f \n",1.0/DtSecs);
#endif
}

float	Timer::GetDTSecs() {
	return static_cast<float>(DtSecs);
}

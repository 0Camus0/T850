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

void Timer::Init() {
#ifdef OS_WINDOWS
	LARGE_INTEGER y;
	QueryPerformanceFrequency(&y);
	Frequency = double(y.QuadPart) / 1000000.0;
	StartTime.QuadPart = 0;
	Dt = 0.0;
	QueryPerformanceCounter(&StartTime);
#elif defined(OS_LINUX)
    gettimeofday(&StartTime,0);
#endif
}

void Timer::Update() {
#ifdef OS_WINDOWS
	LARGE_INTEGER end;
	QueryPerformanceCounter(&end);
	Dt = double(end.QuadPart - StartTime.QuadPart) / Frequency;
	QueryPerformanceCounter(&StartTime);
	DtSecs = (Dt / 1000000.0);
#elif defined(OS_LINUX)
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

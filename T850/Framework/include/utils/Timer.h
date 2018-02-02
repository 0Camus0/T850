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

#ifndef T800_TIMER_H
#define T800_TIMER_H

#include <Config.h>

#ifdef OS_WINDOWS
#include <windows.h>
#elif defined(OS_LINUX)
#include <sys/time.h>
#endif

class Timer {
public:
	void Init();
	void Update();

	float	GetDTSecs();

	#ifdef OS_WINDOWS
	LARGE_INTEGER	StartTime;
	#elif defined(OS_LINUX)
	timeval	        StartTime;
	#endif
	double			Frequency;
	double			Dt;
	double			DtSecs;
};

#endif

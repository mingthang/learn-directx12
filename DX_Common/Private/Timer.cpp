#include "pch.h"

#include "Timer.h"

#include <Windows.h>

Timer::Timer()
{
	long long count_per_sec;
	QueryPerformanceFrequency((LARGE_INTEGER*)&count_per_sec); // frequency (counts per second) of the performance timer
	m_SecondsPerCount = 1.0 / (double)count_per_sec; // number of seconds per count = reciprocal of the counts per second
}

// Returns the total time elapsed since Reset() was called, NOT counting any
// time when the clock is stopped.
float Timer::TotalTime() const
{
	// If we are stopped, do not count the time that has passed since we stopped.
	// Moreover, if we previously already had a pause, the distance 
	// m_StopTime - m_BaseTime includes paused time, which we do not want to count.
	// To correct this, we can subtract the paused time from m_StopTime:  
	//
	//                     |<--paused time-->|
	// ----*---------------*-----------------*------------*------------*------> time
	//  m_BaseTime       m_StopTime        startTime     m_StopTime    m_CurrTime

	if (m_Stopped)
	{
		return (float)(((m_StopTime - m_PausedTime) - m_BaseTime) * m_SecondsPerCount);
	}

	// The distance m_CurrTime - m_BaseTime includes paused time,
	// which we do not want to count.  To correct this, we can subtract 
	// the paused time from m_CurrTime:  
	//
	//  (m_CurrTime - m_PausedTime) - m_BaseTime 
	//
	//                     |<--paused time-->|
	// ----*---------------*-----------------*------------*------> time
	//  m_BaseTime       m_StopTime        startTime     m_CurrTime

	else
	{
		return (float)(((m_CurrTime - m_PausedTime) - m_BaseTime) * m_SecondsPerCount);
	}
}

float Timer::DeltaTime() const
{
	return (float)m_DeltaTime;
}

void Timer::Reset()
{
	__int64 currTime;
	QueryPerformanceCounter((LARGE_INTEGER*)&currTime);

	m_BaseTime = currTime;
	m_PrevTime = currTime;
	m_StopTime = 0;
	m_Stopped = false;
}

void Timer::Start()
{
	__int64 startTime;
	QueryPerformanceCounter((LARGE_INTEGER*)&startTime);


	// Accumulate the time elapsed between stop and start pairs.
	//
	//                     |<-------d------->|
	// ----*---------------*-----------------*------------> time
	//  m_BaseTime       m_StopTime        startTime     

	if (m_Stopped)
	{
		m_PausedTime += (startTime - m_StopTime);

		m_PrevTime = startTime;
		m_StopTime = 0;
		m_Stopped = false;
	}
}

void Timer::Stop()
{
	if (!m_Stopped)
	{
		__int64 currTime;
		QueryPerformanceCounter((LARGE_INTEGER*)&currTime);

		m_StopTime = currTime;
		m_Stopped = true;
	}
}

void Timer::Tick()
{
	if (m_Stopped)
	{
		m_DeltaTime = 0.0;
		return;
	}

	__int64 currTime;
	QueryPerformanceCounter((LARGE_INTEGER*)&currTime);
	m_CurrTime = currTime;

	// Time difference between this frame and the previous.
	m_DeltaTime = (m_CurrTime - m_PrevTime) * m_SecondsPerCount;

	// Prepare for next frame.
	m_PrevTime = m_CurrTime;

	// Force nonnegative.  The DXSDK's CDXUTTimer mentions that if the 
	// processor goes into a power save mode or we get shuffled to another
	// processor, then m_DeltaTime can be negative.
	if (m_DeltaTime < 0.0)
	{
		m_DeltaTime = 0.0;
	}
}

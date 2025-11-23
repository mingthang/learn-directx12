#pragma once

class Timer
{
public:
	Timer();

	float TotalTime() const; // (seconds)
	float DeltaTime() const; // (seconds)

	void Reset(); // Call before message loop.
	void Start(); // Call when unpaused.
	void Stop();  // Call when paused.
	void Tick();  // Call every frame.

private:
	double m_SecondsPerCount = 0.0;
	double m_DeltaTime = -1.0;

	__int64 m_BaseTime = 0.0;
	__int64 m_PausedTime = 0.0;
	__int64 m_StopTime = 0.0;
	__int64 m_PrevTime = 0.0;
	__int64 m_CurrTime = 0.0;
	
	bool m_Stopped = false;
};

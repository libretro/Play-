#include "MailBox.h"
#if defined(_WIN32)
#include "win32/Win32Defs.h"
#endif

bool CMailBox::IsPending() const
{
	return m_calls.size() != 0;
}

void CMailBox::WaitForCall()
{
	std::unique_lock<std::mutex> callLock(m_callMutex);
	while(!IsPending())
	{
		m_waitCondition.wait(callLock);
	}
}

void CMailBox::WaitForCall(unsigned int timeOut)
{
	std::unique_lock<std::mutex> callLock(m_callMutex);
	if(IsPending()) return;
	m_waitCondition.wait_for(callLock, std::chrono::milliseconds(timeOut));
}

void CMailBox::FlushCalls()
{
	SendCall([]() {}, true);
}

void CMailBox::SendCall(const FunctionType& function, bool waitForCompletion, bool breakpoint)
{
	std::unique_lock<std::mutex> callLock(m_callMutex);

	{
		MESSAGE message;
		message.function = function;
		message.sync = (waitForCompletion && m_canWait) || breakpoint;
		message.breakpoint = breakpoint;
		m_calls.push_back(std::move(message));
	}

	m_waitCondition.notify_all();

	if((waitForCompletion && m_canWait) || breakpoint)
	{
		m_callDone = false;
		while(!m_callDone)
		{
			m_callFinished.wait(callLock);
		}
	}
}

void CMailBox::SendCall(FunctionType&& function)
{
	std::lock_guard<std::mutex> callLock(m_callMutex);

	{
		MESSAGE message;
		message.function = std::move(function);
		message.sync = false;
		message.breakpoint = false;
		m_calls.push_back(std::move(message));
	}

	m_waitCondition.notify_all();
}

void CMailBox::SetCanWait(bool val)
{
	m_canWait = val;
	SendCall([]() {});
}

void CMailBox::ProcessUntilBreakPoint()
{
	bool isBreakPoint = false;
	while(!isBreakPoint)
	{
		if(!IsPending())
			WaitForCall();
		isBreakPoint = ReceiveCall();
	}
}

void CMailBox::Reset()
{
	m_calls.clear();
	m_callFinished.notify_all();
}

bool CMailBox::ReceiveCall()
{
	MESSAGE message;
	{
		std::lock_guard<std::mutex> waitLock(m_callMutex);
		if(!IsPending()) return false;
		message = std::move(m_calls.front());
		m_calls.pop_front();
	}
	message.function();
	if(message.sync)
	{
		std::lock_guard<std::mutex> waitLock(m_callMutex);
		m_callDone = true;
		m_callFinished.notify_all();
	}
	return message.breakpoint;
}

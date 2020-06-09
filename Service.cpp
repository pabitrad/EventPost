#include "stdafx.h"
#include "Service.h"
#include "EventLog.h"

const int g_pendingTimeout = 10000;

NTService::NTService( const std::wstring& serviceName ):
	m_serviceName( serviceName ), 
	m_statusHandle(nullptr),
	m_checkPoint(0)
{
	memset(&m_status, 0, sizeof(SERVICE_STATUS));    
	m_status.dwServiceType = SERVICE_WIN32_OWN_PROCESS; 
	m_status.dwServiceSpecificExitCode = 0;   
}

NTService::~NTService() 
{
	UpdateStatus (SERVICE_STOPPED, NO_ERROR, 0);
//	m_Log.Close();
}

void NTService::Start()
{
	m_statusHandle = RegisterServiceCtrlHandlerEx(m_serviceName.c_str(), CtrlHandler, this);
	if (!m_statusHandle)
	{
		//m_Log.WriteToLog("Cannot register service control handler");
		return;
	}
	UpdateStatus(SERVICE_RUNNING, NO_ERROR, g_pendingTimeout);
}

void NTService::Stop()
{
	UpdateStatus(SERVICE_STOPPED, NO_ERROR, 0);
	m_stopEvent.set();
}

DWORD WINAPI NTService::CtrlHandler( DWORD CtrlCode, DWORD eventType,
						 LPVOID eventData, LPVOID context)
{
	NTService* service = static_cast<NTService*>(context);
	if (!service)
	{
		if (CtrlCode == SERVICE_CONTROL_INTERROGATE)
			return NO_ERROR;
		else
			return ERROR_CALL_NOT_IMPLEMENTED;
	}
	
	switch (CtrlCode) 
	{
		case SERVICE_CONTROL_STOP:
			service->Stop();
			return NO_ERROR;
		case SERVICE_CONTROL_POWEREVENT:
		case SERVICE_CONTROL_SESSIONCHANGE:
		case SERVICE_CONTROL_SHUTDOWN:
			return NO_ERROR;
		default:
			break;
	}

	return ERROR_CALL_NOT_IMPLEMENTED;
}  

BOOL NTService::UpdateStatus(DWORD currentState, DWORD win32ExitCode, DWORD waitHint)
{    

	m_status.dwCurrentState = currentState;
	m_status.dwWin32ExitCode = win32ExitCode;
	m_status.dwWaitHint = waitHint;

	if (currentState == SERVICE_START_PENDING)
	{
		m_status.dwControlsAccepted = 0;
	}
	else
	{
		m_status.dwControlsAccepted = SERVICE_ACCEPT_STOP | SERVICE_ACCEPT_POWEREVENT | SERVICE_ACCEPT_SESSIONCHANGE | SERVICE_ACCEPT_SHUTDOWN; 
		
		//Don't forget to change the list of accepted controls if you want to handle preshutdown control instead of shutdown
		//m_status.dwControlsAccepted = SERVICE_ACCEPT_STOP | SERVICE_ACCEPT_POWEREVENT | SERVICE_ACCEPT_SESSIONCHANGE | SERVICE_ACCEPT_PRESHUTDOWN; 
	}

	if ( (currentState == SERVICE_RUNNING) || (currentState == SERVICE_STOPPED) )
	{
		m_status.dwCheckPoint = 0;
	}
	else 
	{
		m_status.dwCheckPoint = m_checkPoint++;
	}

	return ::SetServiceStatus( m_statusHandle, &m_status );
}

DWORD WINAPI NTService::ServiceWorkerThread (NTService* service)
{
	//  Periodically check if the service has been requested to stop
	constexpr DWORD napDuration			= 10000;	// 10 seconds
	constexpr DWORD napsBeforeUpload	= 30;		// 5 minutes
	DWORD napsTaken {};
	while (WaitForSingleObject(service->m_stopEvent.get(), napDuration) != WAIT_OBJECT_0)
	{
		if (napsTaken % napsBeforeUpload == 0) {
			ReadLogs();
			UploadLogs();
		}
		++napsTaken;
	}
	return ERROR_SUCCESS;
} 

VOID WINAPI NTService::ServiceMainImpl(NTService& service)
{
 	service.Start();
	std::thread workingThread (&NTService::ServiceWorkerThread, &service);
	workingThread.join();
 }
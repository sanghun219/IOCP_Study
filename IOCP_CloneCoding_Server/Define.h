#pragma once
#include <WinSock2.h>
#include <WS2tcpip.h>
#include <MSWSock.h>
#include <chrono>
const UINT32 MAX_SOCKBUF = 256;
const UINT32 MAX_WORKERTHREAD = 4;
const UINT64 RE_USE_SESSION_WAIT_TIMESEC = 3;

enum IOOperation {
	RECV,
	SEND,
	ACCEPT,
};

typedef struct stOverlappedEx {
	WSAOVERLAPPED m_wsaOverlapped;
	WSABUF m_wsaBuf;
	IOOperation m_eOperation;
	UINT32 SessionIndex;
};

struct stClientInfo {
	SOCKET m_socketClient;
	stOverlappedEx m_stRecvOverlappedEx;
	stOverlappedEx m_stSendOverlappedEx;

	stClientInfo() {
		m_socketClient = INVALID_SOCKET;
		ZeroMemory(&m_stRecvOverlappedEx, sizeof(stOverlappedEx));
		ZeroMemory(&m_stSendOverlappedEx, sizeof(stOverlappedEx));
	}
};
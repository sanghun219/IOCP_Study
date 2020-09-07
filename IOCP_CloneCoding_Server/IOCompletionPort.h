#pragma once
#include <WinSock2.h>
#include <WS2tcpip.h>
#include <thread>
#include <iostream>
#include <vector>

#pragma comment (lib,"ws2_32.lib")
#define MAX_SOCKBUF 1024
#define MAX_WORKERTHREAD 4

// 코드 작성 후 느낌
/*
	이해한 것은 블로킹/논블로킹에 관한 건이다. 기존의 Overlapped I/O 방식에서는 논블록 소켓으로 지정해
	accept와 work 처리를 번갈아가며 진행했다면, IOCP는 메인 스레드를 accept용으로 두고
	추가적인 Worker Thread를 통해 Recv/Send를 진행한 것. 따라서 IOCP를 사용한다면
	Overlapped I/O나 Select 방식처럼 논블록을 지정해야 할 이유가 없다!

	Select - WSAWOULDBLOCK 으로 논블럭 처리
	IOCP - 메인 스레드/ 작업 스레드 분리로 비동기 처리
*/

enum IOOperation {
	RECV,
	SEND,
};

typedef struct stOverlappedEx {
	WSAOVERLAPPED m_wsaOverlapped;
	SOCKET m_socketClient;
	WSABUF m_wsaBuf;
	char m_szBuf[MAX_SOCKBUF];
	IOOperation m_eOperation;
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

class IOCompletionPort {
public:

	~IOCompletionPort() {
		WSACleanup();
	}

	bool InitSocket() {
		WSADATA wsaData;
		if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
			std::cout << "InitSocket : StartUpError" << std::endl;
			return false;
		}

		mListenSocket = WSASocket(AF_INET, SOCK_STREAM, IPPROTO_TCP, NULL, NULL, WSA_FLAG_OVERLAPPED);
		if (mListenSocket == INVALID_SOCKET) {
			std::cout << "InitSocket : WSASocket Error " << std::endl;
			return false;
		}

		std::cout << "소켓 초기화 성공!" << std::endl;
		return true;
	}

	bool BindandListen(int nBindPort) {
		SOCKADDR_IN stServerAddr;
		stServerAddr.sin_family = AF_INET;
		stServerAddr.sin_port = htons(nBindPort);
		stServerAddr.sin_addr.S_un.S_addr = htonl(INADDR_ANY);

		int nRet = bind(mListenSocket, (const sockaddr*)&stServerAddr, sizeof(stServerAddr));
		if (nRet == SOCKET_ERROR) {
			std::cout << "BindandListen Error : bind error " << std::endl;
			return false;
		}

		nRet = listen(mListenSocket, 5);
		if (nRet == SOCKET_ERROR) {
			std::cout << "BindandListen Error : listen error " << std::endl;
			return false;
		}

		std::cout << "서버 등록 성공 " << std::endl;
		return true;
	}

	bool StartServer(const UINT32 maxClientCount) {
		CreateClient(maxClientCount);
		mIOCPHandle = CreateIoCompletionPort(INVALID_HANDLE_VALUE, NULL, NULL, MAX_WORKERTHREAD);
		if (NULL == mIOCPHandle) {
			std::cout << "StartServer Error : IOCPHandle Error ";
			return false;
		}

		bool bRet = CreateWorkerThread();
		if (false == bRet) {
			return false;
		}

		bRet = CreateAcceptThread();

		if (false == bRet) {
			return false;
		}
		std::cout << "서버 시작 " << std::endl;
		return true;
	}

	void DestroyThread() {
		mIsWorkerRun = false;
		CloseHandle(mIOCPHandle);

		for (auto& th : mIOWorkerThread) {
			if (th.joinable()) {
				th.join();
			}
		}

		mIsAccepterRun = false;
		closesocket(mListenSocket);

		if (mAccepterThread.joinable())
			mAccepterThread.join();
	}
private:
	void CreateClient(const UINT32 maxClientCount) {
		for (int i = 0; i < maxClientCount; i++) {
			mClientInfos.emplace_back();
		}
	}

	bool CreateWorkerThread() {
		unsigned int uiThreadId = 0;
		for (int i = 0; i < MAX_WORKERTHREAD; i++) {
			mIOWorkerThread.emplace_back([this]() {WorkerThread(); });
		}

		std::cout << "WorkerThread 시작!" << std::endl;
		return true;
	}

	bool CreateAcceptThread() {
		mAccepterThread = std::thread([this]() {AccepterThread(); });
		std::cout << "AccepterThread 시작 " << std::endl;
		return true;
	}

	stClientInfo* GetEmptyClientInfo() {
		for (auto& client : mClientInfos) {
			if (INVALID_SOCKET == client.m_socketClient)
			{
				return &client;
			}
		}
		return nullptr;
	}

	bool BindIOCompletionPort(stClientInfo* pClientInfo) {
		auto hIOCP = CreateIoCompletionPort((HANDLE)pClientInfo->m_socketClient, mIOCPHandle
			, (ULONG_PTR)(pClientInfo), 0);

		if (nullptr == hIOCP) {
			std::cout << "BindIOCompletionPort Error" << std::endl;
			return false;
		}
		return true;
	}

	bool BindRecv(stClientInfo* pClientInfo) {
		DWORD dwflag = 0;
		DWORD dwRecvNumBytes = 0;

		pClientInfo->m_stRecvOverlappedEx.m_wsaBuf.len = MAX_SOCKBUF;
		pClientInfo->m_stRecvOverlappedEx.m_wsaBuf.buf = pClientInfo->m_stRecvOverlappedEx.m_szBuf;
		pClientInfo->m_stRecvOverlappedEx.m_eOperation = IOOperation::RECV;

		int nRet = WSARecv(pClientInfo->m_socketClient, &(pClientInfo->m_stRecvOverlappedEx.m_wsaBuf),
			1, &dwRecvNumBytes, &dwflag, (LPWSAOVERLAPPED)&pClientInfo->m_stRecvOverlappedEx, NULL);

		if (nRet == SOCKET_ERROR && (WSAGetLastError() != ERROR_IO_PENDING)) {
			std::cout << "BindRecv Error" << std::endl;
			return false;
		}
		return true;
	}

	bool SendMsg(stClientInfo* pClientInfo, char* pMsg, int nLen) {
		DWORD dwRecvNumBytes = 0;
		CopyMemory(pClientInfo->m_stSendOverlappedEx.m_szBuf, pMsg, nLen);

		pClientInfo->m_stSendOverlappedEx.m_wsaBuf.len = nLen;
		pClientInfo->m_stSendOverlappedEx.m_wsaBuf.buf = pClientInfo->m_stSendOverlappedEx.m_szBuf;
		pClientInfo->m_stSendOverlappedEx.m_eOperation = IOOperation::SEND;

		int nRet = WSASend(pClientInfo->m_socketClient, &(pClientInfo->m_stSendOverlappedEx.m_wsaBuf)
			, 1, &dwRecvNumBytes, 0, (LPWSAOVERLAPPED)&pClientInfo->m_stRecvOverlappedEx.m_wsaOverlapped, NULL);

		if (nRet == SOCKET_ERROR && (WSAGetLastError() != WSA_IO_PENDING)) {
			std::cout << "SendMsg Error!" << std::endl;
			return false;
		}
		return true;
	}

	void WorkerThread() {
		stClientInfo* pClientInfo = nullptr;

		BOOL bSuccess = TRUE;

		DWORD dwIoSize = 0;

		LPOVERLAPPED lpOverlapped = NULL;

		while (mIsWorkerRun) {
			bSuccess = GetQueuedCompletionStatus(mIOCPHandle, &dwIoSize, (PULONG_PTR)&pClientInfo, &lpOverlapped,
				INFINITE);

			if (TRUE == bSuccess && 0 == dwIoSize && NULL == lpOverlapped) {
				mIsWorkerRun = FALSE;
				continue;
			}

			if (lpOverlapped == NULL) {
				continue;
			}

			if (FALSE == bSuccess || (0 == dwIoSize && TRUE == bSuccess)) {
				std::cout << "socket(" << (int)pClientInfo->m_socketClient << ") 접속 끊김" << std::endl;
				CloseSocket(pClientInfo);
				continue;
			}

			stOverlappedEx* pOverlappedEx = (stOverlappedEx*)lpOverlapped;

			if (IOOperation::RECV == pOverlappedEx->m_eOperation) {
				// Enum 값으로 패킷 id 분석후 어떻게 처리할지에 대한 구문이 들어갈 수 있음
				pOverlappedEx->m_szBuf[dwIoSize] = NULL;
				printf("[수신] bytes : %d, msg : %s\n", dwIoSize, pOverlappedEx->m_szBuf);

				// 추가 작업
				SendMsg(pClientInfo, pOverlappedEx->m_szBuf, dwIoSize);
				BindRecv(pClientInfo);
			}
			else if (IOOperation::SEND == pOverlappedEx->m_eOperation) {
				printf("[송신] bytes : %d, msg : %s\n", dwIoSize, pOverlappedEx->m_szBuf);
			}
			else {
				printf("socket(%d)에서 예외사항\n", pClientInfo->m_socketClient);
			}
		}
	}

	void CloseSocket(stClientInfo* pClientInfo, bool bIsForce = false) {
		struct linger stLinger = { 0,0 };

		if (true == bIsForce) {
			stLinger.l_onoff = 1;
		}

		shutdown(pClientInfo->m_socketClient, SD_BOTH);

		setsockopt(pClientInfo->m_socketClient, SOL_SOCKET, SO_LINGER, (const char*)&stLinger, sizeof(stLinger));

		closesocket(pClientInfo->m_socketClient);

		pClientInfo->m_socketClient = INVALID_SOCKET;
	}
	void AccepterThread() {
		SOCKADDR_IN		stClientAddr;
		int nAddrIn = sizeof(SOCKADDR_IN);

		while (mIsAccepterRun) {
			stClientInfo* pClientInfo = GetEmptyClientInfo();
			if (NULL == pClientInfo) {
				std::cout << "AccepterThread Error : stClientInfo is Null" << std::endl;
				return;
			}

			pClientInfo->m_socketClient = accept(mListenSocket, (sockaddr*)&stClientAddr, &nAddrIn);
			if (INVALID_SOCKET == pClientInfo->m_socketClient) {
				continue;
			}
			bool bRet = BindIOCompletionPort(pClientInfo);
			if (false == bRet) {
				return;
			}
			bRet = BindRecv(pClientInfo);
			if (false == bRet) {
				return;
			}

			char ClientIP[32] = { 0, };
			inet_ntop(AF_INET, &(stClientAddr.sin_addr), ClientIP, 32 - 1);
			printf("클라이언트 접속 : IP(%s) SOCKET(%d)\n", ClientIP, (int)pClientInfo->m_socketClient);

			++mClientCnt;
		}
	}
private:
	SOCKET mListenSocket;
	HANDLE mIOCPHandle;
	bool mIsWorkerRun = true;
	bool mIsAccepterRun;
	std::vector<std::thread> mIOWorkerThread;
	std::thread mAccepterThread;
	std::vector<stClientInfo> mClientInfos;
	int mClientCnt;
};
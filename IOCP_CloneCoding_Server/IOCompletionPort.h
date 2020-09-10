#pragma once
#pragma comment(lib,"ws2_32.lib")
#pragma comment(lib,"mswsock.lib")
#include "Define.h"
#include "ClientInfo.h"
#include <thread>
#include <vector>

// TODO : Select/IOCP 이해 [1주차]
// 코드 작성 후 느낌
/*
	이해한 것은 블로킹/논블로킹에 관한 건이다. 기존의 Overlapped I/O 방식에서는 논블록 소켓으로 지정해
	accept와 work 처리를 번갈아가며 진행했다면, IOCP는 메인 스레드를 accept용으로 두고
	추가적인 Worker Thread를 통해 Recv/Send를 진행한 것. 따라서 IOCP를 사용한다면
	Overlapped I/O나 Select 방식처럼 논블록을 지정해야 할 이유가 없다!

	Select - WSAWOULDBLOCK 으로 논블럭 처리
	IOCP - 메인 스레드/ 작업 스레드 분리로 비동기 처리
*/

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

	// TODO : 서버와 세션의 분리
	// 서버로부터 세션(Client)가 분리되었기에 해당 기능은 Client가 계승함.
	bool SendMsg(const UINT32 sessionIndex_, const UINT32 dataSize_, char* pData) {
		auto pClient = GetClientInfo(sessionIndex_);
		return pClient->SendMsg(dataSize_, pData);
	}

	virtual void OnConnected(const UINT32 clientIndex_) {}
	virtual void OnClose(const UINT32 clientIndex_) {}
	virtual void OnReceive(const UINT32 clientIndex_, const UINT32 size_, char* pData) {}
private:
	void CreateClient(const UINT32 maxClientCount) {
		for (int i = 0; i < maxClientCount; i++) {
			auto client = new stClientInfo;
			client->Init(i, mIOCPHandle);
			mClientInfos.push_back(client);
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
			if (client->IsConnected() == false)
				return client;
		}
		return nullptr;
	}

	stClientInfo* GetClientInfo(const UINT32 sessionIndex) {
		return mClientInfos[sessionIndex];
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

			stOverlappedEx* pOverlappedEx = (stOverlappedEx*)lpOverlapped;

			if (FALSE == bSuccess || (0 == dwIoSize && IOOperation::ACCEPT != pOverlappedEx->m_eOperation)) {
				CloseSocket(pClientInfo);
				continue;
			}

			if (IOOperation::ACCEPT == pOverlappedEx->m_eOperation) {
				pClientInfo = GetClientInfo(pOverlappedEx->SessionIndex);
				if (pClientInfo->AcceptCompletion()) {
					++mClientCnt;
					OnConnected(pClientInfo->GetIndex());
				}
				else {
					CloseSocket(pClientInfo, true);
				}
			}

			else if (IOOperation::RECV == pOverlappedEx->m_eOperation) {
				OnReceive(pClientInfo->GetIndex(), dwIoSize, pClientInfo->RecvBuffer());
				pClientInfo->BindRecv();
			}
			else if (IOOperation::SEND == pOverlappedEx->m_eOperation) {
				pClientInfo->SendCompleted(dwIoSize);
			}
			else {
				printf("socket(%d)에서 예외사항\n", pClientInfo->GetIndex());
			}
		}
	}

	void CloseSocket(stClientInfo* pClientInfo, bool bIsForce = false) {
		auto clientIndex = pClientInfo->GetIndex();
		pClientInfo->Close(bIsForce);
		OnClose(clientIndex);
	}
	// TODO : 왜 일정 시간동안 Close된 세션에 대해서 Accept를 받지 않는가?
	void AccepterThread() {
		while (mIsAccepterRun) {
			auto curTimeSec = std::chrono::duration_cast<std::chrono::seconds>(
				std::chrono::steady_clock::now().time_since_epoch()
				).count();

			for (auto client : mClientInfos) {
				if (client->IsConnected()) {
					continue;
				}

				if ((UINT64)curTimeSec < client->GetLatestClosedTimeSec()) {
					continue;
				}

				auto diff = curTimeSec - client->GetLatestClosedTimeSec();
				if (diff <= RE_USE_SESSION_WAIT_TIMESEC) {
					continue;
				}

				client->PostAccept(mListenSocket, curTimeSec);
			}
			std::this_thread::sleep_for(std::chrono::microseconds(32));
		}
	}
private:
	SOCKET mListenSocket;
	HANDLE mIOCPHandle;
	bool mIsWorkerRun = true;
	bool mIsAccepterRun;
	std::vector<std::thread> mIOWorkerThread;
	std::thread mAccepterThread;
	std::vector<stClientInfo*> mClientInfos;
	int mClientCnt;
};
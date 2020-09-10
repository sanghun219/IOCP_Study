#pragma once
#pragma comment(lib,"ws2_32.lib")
#pragma comment(lib,"mswsock.lib")
#include "Define.h"
#include "ClientInfo.h"
#include <thread>
#include <vector>

// TODO : Select/IOCP ���� [1����]
// �ڵ� �ۼ� �� ����
/*
	������ ���� ���ŷ/����ŷ�� ���� ���̴�. ������ Overlapped I/O ��Ŀ����� ���� �������� ������
	accept�� work ó���� �����ư��� �����ߴٸ�, IOCP�� ���� �����带 accept������ �ΰ�
	�߰����� Worker Thread�� ���� Recv/Send�� ������ ��. ���� IOCP�� ����Ѵٸ�
	Overlapped I/O�� Select ���ó�� ������ �����ؾ� �� ������ ����!

	Select - WSAWOULDBLOCK ���� ��� ó��
	IOCP - ���� ������/ �۾� ������ �и��� �񵿱� ó��
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

		std::cout << "���� �ʱ�ȭ ����!" << std::endl;
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

		std::cout << "���� ��� ���� " << std::endl;
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
		std::cout << "���� ���� " << std::endl;
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

	// TODO : ������ ������ �и�
	// �����κ��� ����(Client)�� �и��Ǿ��⿡ �ش� ����� Client�� �����.
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

		std::cout << "WorkerThread ����!" << std::endl;
		return true;
	}

	bool CreateAcceptThread() {
		mAccepterThread = std::thread([this]() {AccepterThread(); });
		std::cout << "AccepterThread ���� " << std::endl;
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
				printf("socket(%d)���� ���ܻ���\n", pClientInfo->GetIndex());
			}
		}
	}

	void CloseSocket(stClientInfo* pClientInfo, bool bIsForce = false) {
		auto clientIndex = pClientInfo->GetIndex();
		pClientInfo->Close(bIsForce);
		OnClose(clientIndex);
	}
	// TODO : �� ���� �ð����� Close�� ���ǿ� ���ؼ� Accept�� ���� �ʴ°�?
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
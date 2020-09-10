#pragma once

#include "Define.h"
#include <iostream>
#include <queue>
#include <mutex>

// TODO : stClientInfo[2����]
// ���� IOCompletionPort�� ��� �ִ� �Ϻ� ����� �Ű� �Դ�. (��Ʈ��ũ - ������ �и���)

class stClientInfo {
public:
	stClientInfo() {
		ZeroMemory(&mRecvOverlappedEx, sizeof(mRecvOverlappedEx));
		mSock = INVALID_SOCKET;
	}
	void Init(const UINT32 index, HANDLE iocpHandle_) {
		mIndex = index;
		mIOCPHandle = iocpHandle_;
	}
	UINT32 GetIndex()const { return mIndex; }
	bool IsConnected() { return mSock != INVALID_SOCKET; }
	SOCKET GetSocket() { return mSock; }
	char* RecvBuffer() { return mRecvBuf; }
	UINT64 GetLatestClosedTimeSec() { return mLatestClosedTimeSec; }
	bool OnConnect() {
		mIsConnect = 1;
		Clear();

		if (BindIOCompletionPort(mIOCPHandle) == false) {
			return false;
		}
		return BindRecv();
	}
	bool BindIOCompletionPort(HANDLE iocpHandle_) {
		auto hIOCP = CreateIoCompletionPort((HANDLE)GetSocket(), iocpHandle_, (ULONG_PTR)(this), 0);
		if (hIOCP == INVALID_HANDLE_VALUE) {
			printf("[����] CreateIoCompletionPort ���� :%d\n", GetLastError());
			return false;
		}
		return true;
	}
	bool BindRecv() {
		DWORD dwFlag = 0;
		DWORD dwRecvNumBytes = 0;

		mRecvOverlappedEx.m_wsaBuf.len = MAX_SOCKBUF;
		mRecvOverlappedEx.m_wsaBuf.buf = mRecvBuf;
		mRecvOverlappedEx.m_eOperation = IOOperation::RECV;

		int nRet = WSARecv(mSock, &(mRecvOverlappedEx.m_wsaBuf), 1,
			&dwRecvNumBytes, &dwFlag, (LPWSAOVERLAPPED) & (mRecvOverlappedEx), NULL);
		if (nRet == SOCKET_ERROR && (WSAGetLastError() != WSA_IO_PENDING)) {
			printf("[����] WSARecv()�Լ� ���� : %d\n", WSAGetLastError());
			return false;
		}
		return true;
	}
	void Close(bool bIsForce = false) {
		struct linger stLinger { 0, 0 };

		// l_onoff =1, l_linger =0�̸� ������ ���Ḧ �ϸ�, ���۵��� ���� �����͵��� ��� ��������.
		if (true == bIsForce) {
			stLinger.l_onoff = true;
		}

		shutdown(mSock, SD_BOTH);

		setsockopt(mSock, SOL_SOCKET, SO_LINGER, (const char*)&stLinger, sizeof(stLinger));

		closesocket(mSock);
		mIsConnect = 0;
		mLatestClosedTimeSec = std::chrono::duration_cast<std::chrono::seconds>
			(std::chrono::steady_clock::now().time_since_epoch()).count();
		mSock = INVALID_SOCKET;
	}
	void Clear() {}
	bool PostAccept(SOCKET listenSocket_, const UINT64 curTimeSec_) {
		printf("Post Accept. Client Index : %d\n", GetIndex());

		mLatestClosedTimeSec = UINT32_MAX;
		mSock = WSASocket(AF_INET, SOCK_STREAM, IPPROTO_IP, NULL, 0, WSA_FLAG_OVERLAPPED);
		if (INVALID_SOCKET == mSock) {
			printf("PostAccept Error ! :%d\n", WSAGetLastError());
			return false;
		}

		ZeroMemory(&mAcceptContext, sizeof(mAcceptContext));
		mAcceptContext.m_eOperation = IOOperation::ACCEPT;
		mAcceptContext.m_wsaBuf.len = 0;
		mAcceptContext.m_wsaBuf.buf = nullptr;
		mAcceptContext.SessionIndex = mIndex;
		DWORD bytes = 0;
		DWORD flags = 0;

		if (FALSE == AcceptEx(listenSocket_, mSock, mAcceptbuf, 0, sizeof(SOCKADDR_IN) + 16, sizeof(SOCKADDR_IN) + 16,
			&bytes, (LPWSAOVERLAPPED) & (mAcceptContext))) {
			if (WSAGetLastError() != WSA_IO_PENDING) {
				printf("AcceptEx Error! %d\n", WSAGetLastError());
				return false;
			}
		}
		return true;
	}
	bool AcceptCompletion() {
		printf("AcceptCompletion : SessionIdx :%d\n", mIndex);
		if (OnConnect() == FALSE) {
			return false;
		}

		SOCKADDR_IN stClientAddr;
		ZeroMemory(&stClientAddr, sizeof(stClientAddr));
		int nAddrIn = sizeof(stClientAddr);
		char ClientIP[32] = { 0, };
		inet_ntop(AF_INET, &stClientAddr.sin_addr, ClientIP, 32 - 1);
		printf("Ŭ���̾�Ʈ ���� : IP(%s) SOCKET(%d)\n", ClientIP, (int)mSock);
		return true;
	}
	// TODO : SendMsg
	// pMsg�� �״�� Ŀ�� ���ۿ� �������� �ʰ� ������ ������ ���� �ƹ�����
	// �ܺο��� �����Ͱ� ���ŵ� ���ɼ��� �ֱ� �����ε� �ϴ�.
	// �ݴ�� Recv ��쿡�� ClientInfo ��ü���� �����ϰ� �ֱ⿡ RAII ������ ����ȴ�

	// Send/Recv ��� ����������

	bool SendMsg(const UINT32 dataSize_, char* pMsg) {
		auto sendOverlapedEx = new stOverlappedEx;
		ZeroMemory(sendOverlapedEx, sizeof(sendOverlapedEx));
		sendOverlapedEx->m_wsaBuf.len = dataSize_;
		sendOverlapedEx->m_wsaBuf.buf = new char[dataSize_];
		CopyMemory(sendOverlapedEx->m_wsaBuf.buf, pMsg, dataSize_);
		sendOverlapedEx->m_eOperation = IOOperation::SEND;

		std::lock_guard<std::mutex> guard(mSendLock);
		mSendDataQueue.push(sendOverlapedEx);
		if (mSendDataQueue.size() == 1) {
			SendIO();
		}
		return true;
	}

	void SendCompleted(const UINT32 dataSize_) {
		printf("[�۽� �Ϸ�] bytes : %d\n", dataSize_);

		std::lock_guard<std::mutex> guard(mSendLock);

		delete[] mSendDataQueue.front()->m_wsaBuf.buf;
		delete mSendDataQueue.front();

		mSendDataQueue.pop();

		if (mSendDataQueue.empty() == false) {
			SendIO();
		}
	}
private:
	BOOL SendIO() {
		auto sendOverlappedEx = mSendDataQueue.front();

		DWORD dwRecvNumBytes = 0;

		int nRet = WSASend(mSock, &(sendOverlappedEx->m_wsaBuf), 1,
			&dwRecvNumBytes, 0, (LPWSAOVERLAPPED)&sendOverlappedEx, NULL);

		if (nRet == SOCKET_ERROR && WSAGetLastError() != WSA_IO_PENDING) {
			return false;
		}
		return true;
	}
	bool SetSocketOption() {
		int opt = 1;
		if (SOCKET_ERROR == setsockopt(mSock, IPPROTO_TCP, TCP_NODELAY, (const char*)&opt, sizeof(opt))) {
			printf("TCP_NODELAY ERROR! : %d\n", WSAGetLastError());
			return false;
		}

		opt = 0;
		if (SOCKET_ERROR == setsockopt(mSock, SOL_SOCKET, SO_RCVBUF, (const char*)&opt, sizeof(opt))) {
			printf("SO_RCVBUF change error! : %d\n", WSAGetLastError());
			return false;
		}
		return true;
	}
private:
	INT32 mIndex = 0;
	SOCKET mSock;
	HANDLE mIOCPHandle = INVALID_HANDLE_VALUE;
	stOverlappedEx mAcceptContext;
	char mAcceptbuf[64];

	stOverlappedEx mRecvOverlappedEx;
	std::mutex mSendLock;
	char mRecvBuf[MAX_SOCKBUF];
	std::queue<stOverlappedEx*> mSendDataQueue;
	UINT32 mIsConnect = 0;
	UINT64 mLatestClosedTimeSec = 0;
};
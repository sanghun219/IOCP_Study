#pragma once

#include "Define.h"
#include <iostream>

// TODO : stClientInfo[2주차]
// 원래 IOCompletionPort가 들고 있던 일부 기능을 옮겨 왔다. (네트워크 - 로직이 분리됨)

class stClientInfo {
public:
	stClientInfo() {
		ZeroMemory(&mRecvOverlappedEx, sizeof(mRecvOverlappedEx));
		mSock = INVALID_SOCKET;
	}
	void Init(const UINT32 index) {
		mIndex = index;
	}
	UINT32 GetIndex()const { return mIndex; }
	bool IsConnected() { return mSock != INVALID_SOCKET; }
	SOCKET GetSocket() { return mSock; }
	char* RecvBuffer() { return mRecvBuf; }

	bool OnConnect(HANDLE iocpHandle_, SOCKET socket_) {
		mSock = socket_;
		Clear();

		if (BindIOCompletionPort(iocpHandle_) == false) {
			return false;
		}
		return BindRecv();
	}
	bool BindIOCompletionPort(HANDLE iocpHandle_) {
		auto hIOCP = CreateIoCompletionPort((HANDLE)GetSocket(), iocpHandle_, (ULONG_PTR)(this), 0);
		if (hIOCP == INVALID_HANDLE_VALUE) {
			printf("[에러] CreateIoCompletionPort 실패 :%d\n", GetLastError());
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
			printf("[에러] WSARecv()함수 실패 : %d\n", WSAGetLastError());
			return false;
		}
		return true;
	}
	void Close(bool bIsForce = false) {
		struct linger stLinger { 0, 0 };

		// l_onoff =1, l_linger =0이면 비정상 종료를 하며, 전송되지 않은 데이터들은 모두 버려진다.
		if (true == bIsForce) {
			stLinger.l_onoff = true;
		}

		shutdown(mSock, SD_BOTH);

		setsockopt(mSock, SOL_SOCKET, SO_LINGER, (const char*)&stLinger, sizeof(stLinger));

		closesocket(mSock);
		mSock = INVALID_SOCKET;
	}
	void Clear() {}
	// TODO : SendMsg
	// pMsg를 그대로 커널 버퍼에 복사하지 않고 새로이 생성한 것은 아무래도
	// 외부에서 데이터가 제거될 가능성이 있기 때문인듯 하다.
	// 반대로 Recv 경우에는 ClientInfo 자체에서 관리하고 있기에 RAII 패턴이 적용된다

	// Send/Recv 모두 마찬가지로

	bool SendMsg(const UINT32 dataSize_, char* pMsg) {
		auto sendOverlapedEx = new stOverlappedEx;
		ZeroMemory(sendOverlapedEx, sizeof(sendOverlapedEx));
		sendOverlapedEx->m_wsaBuf.len = dataSize_;
		sendOverlapedEx->m_wsaBuf.buf = new char[dataSize_];
		CopyMemory(sendOverlapedEx->m_wsaBuf.buf, pMsg, dataSize_);
		sendOverlapedEx->m_eOperation = IOOperation::SEND;

		DWORD dwRecvNumBytes = 0;
		int nRet = WSASend(mSock, (LPWSABUF) & (sendOverlapedEx->m_wsaBuf), 1,
			&dwRecvNumBytes, 0, (LPWSAOVERLAPPED) & (sendOverlapedEx), NULL);

		if (nRet == SOCKET_ERROR || (WSAGetLastError() != WSA_IO_PENDING)) {
			printf("[에러] WsaSend() 함수 실패 : %d\n", WSAGetLastError());
			return false;
		}
		return true;
	}

	void SendCompleted(const UINT32 dataSize_) {
		printf("[송신 완료] bytes : %d\n", dataSize_);
	}
private:
	INT32 mIndex = 0;
	SOCKET mSock;
	stOverlappedEx mRecvOverlappedEx;

	char mRecvBuf[MAX_SOCKBUF];
};
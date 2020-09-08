#pragma once

#include "Define.h"
#include <iostream>

// TODO : stClientInfo[2����]
// ���� IOCompletionPort�� ��� �ִ� �Ϻ� ����� �Ű� �Դ�. (��Ʈ��ũ - ������ �и���)

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
		mSock = INVALID_SOCKET;
	}
	void Clear() {}
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

		DWORD dwRecvNumBytes = 0;
		int nRet = WSASend(mSock, (LPWSABUF) & (sendOverlapedEx->m_wsaBuf), 1,
			&dwRecvNumBytes, 0, (LPWSAOVERLAPPED) & (sendOverlapedEx), NULL);

		if (nRet == SOCKET_ERROR || (WSAGetLastError() != WSA_IO_PENDING)) {
			printf("[����] WsaSend() �Լ� ���� : %d\n", WSAGetLastError());
			return false;
		}
		return true;
	}

	void SendCompleted(const UINT32 dataSize_) {
		printf("[�۽� �Ϸ�] bytes : %d\n", dataSize_);
	}
private:
	INT32 mIndex = 0;
	SOCKET mSock;
	stOverlappedEx mRecvOverlappedEx;

	char mRecvBuf[MAX_SOCKBUF];
};
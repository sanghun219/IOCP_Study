#pragma once
// MFC�� ������ ������ ���α׷����� �ش� ����� �����صθ� ��� ���� �ʴ� (Windows ��� ���� ũ�⸦ ����)
// ����� �����Ͽ� ���� �ӵ��� ������ ���ش�
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>

//TODO : PacketData[2����]
// Stream Ŭ������ �߰����� �ȵ��� �𸣰����� ���� PacketData ����ü�� �����
// �����͸� Send/Recv �� �� �ʿ��� �ּ����� �������� (ũ��,������,�ε���(�ĺ���))���� ���ϰ� �ִ�
// Stream Ŭ������ �߰��Ǿ����ٸ� ���� ������ PacketData�� ������ ������ �����غ���.
struct PacketData {
public:
	UINT32 SessionIndex = 0;
	UINT32 DataSize = 0;
	char* pPacketData = nullptr;

	void Set(PacketData& value) {
		SessionIndex = value.SessionIndex;
		DataSize = value.DataSize;

		pPacketData = new char[value.DataSize];
		CopyMemory(pPacketData, value.pPacketData, value.DataSize);
	}

	void Set(UINT32 sessionIndex_, UINT32 dataSize_, char* pData) {
		SessionIndex = sessionIndex_;
		DataSize = dataSize_;
		pPacketData = new char[dataSize_];
		CopyMemory(pPacketData, pData, dataSize_);
	}

	void Release() {
		delete pPacketData;
	}
};
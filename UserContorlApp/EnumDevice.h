#pragma once

#include "structures.h"

class CEnumDevice
{
public:
	
	CEnumDevice();
	~CEnumDevice();


private:
	HRESULT hr;
	ICreateDevEnum *pSystemDeviceEnum;
	IEnumMoniker *pEnumMoniker;
	IMoniker *pMoniker;

	// �������� ������ ������
	void GetCompressedMethod(DWORD *dwCompressed, char *szFormat, DWORD dwBitCount);
public:
	// ������ ���������� ��� ��������� ���� 
	HRESULT GetClassEnumerator(REFCLSID clsidDeviceClass);
	// �������� �������� ���������
	// ���� pDevParamInfo - �� �����, �� ���������� ���������� ��������� � �������
	// ���� pDevParamInfo - �����, �� ���������� � ���� �������� ���������
	int GetDeviceFriendlyName(device_param_info *pDevParamInfo);
	// ����������� ��� ���������� ������
	// ���� pDevParamInfo - �� �����, �� ���������� ���������� ���������� 
	// ���� pDevParamInfo - �����, �� ���������� � ���� �������������� ���������� � �������
	int GetDeviceAvailableResolution(camera_frame_format_info *pCamResolution, IBaseFilter *pBaseFilter);
};


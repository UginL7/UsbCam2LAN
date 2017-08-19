#include "CallbackObject.h"

CCallbackObject::CCallbackObject()
{
	m_Ref = 0;
	hDevice = CreateFile("\\\\.\\VideoControl", GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL);
	if (hDevice == INVALID_HANDLE_VALUE)
	{
		DWORD dwError = GetLastError();
		CloseHandle(hDevice);
	}
}


CCallbackObject::~CCallbackObject()
{
}

STDMETHODIMP CCallbackObject::QueryInterface(REFIID riid, void **ppv)
{
	if (NULL == ppv) return E_POINTER;
	if (riid == __uuidof(IUnknown)) {
		*ppv = static_cast<IUnknown*>(this);
		return S_OK;
	}
	if (riid == __uuidof(ISampleGrabberCB)) {
		*ppv = static_cast<ISampleGrabberCB*>(this);
		return S_OK;
	}
	return E_NOINTERFACE;
}

STDMETHODIMP_(ULONG) CCallbackObject::CCallbackObject::AddRef()
{
	InterlockedIncrement(&m_Ref);
	return m_Ref;
}

STDMETHODIMP_(ULONG) CCallbackObject::CCallbackObject::Release()
{
	InterlockedDecrement(&m_Ref);
	if (m_Ref == 0)
	{
		delete this;
		if (hDevice != INVALID_HANDLE_VALUE)
		{
			CloseHandle(hDevice);
		}
		return 0;
	}
	else
		return m_Ref;
}

STDMETHODIMP CCallbackObject::SampleCB(double SampleTime, IMediaSample *pSample)
{
	if (!pSample)
	{
		return E_POINTER;
	}
	DWORD dwUYVYSize = 0;
	HRESULT hr;
	bool result;
	DWORD dwOutputBufferSize;	
	DWORD nBuffSize = pSample->GetActualDataLength();
	BYTE *pBuf = NULL;
	hr = pSample->GetPointer(&pBuf);

	unsigned char *pYUYVBuff;
	pYUYVBuff = (unsigned char*)malloc(320 * 240 * 2);

	if (nBuffSize <= 0 || pBuf == NULL)
	{
		return E_UNEXPECTED;
	}

	//////////////////////////////////////////////////////////////////////////
	// модификация потока на любой лад
	//////////////////////////////////////////////////////////////////////////
	for (int i = 0; i < nBuffSize; i+=3)
	{
		unsigned char B = pBuf[i];
		unsigned char G = pBuf[i + 1];
		unsigned char R = pBuf[i + 2];

		unsigned char Y = ((66 * R + 129 * G + 25 * B + 128) >> 8) + 16;
		unsigned char U = ((-38 * R - 74 * G + 112 * B + 128) >> 8) + 128;
		unsigned char V = ((112 * R - 94 * G - 18 * B + 128) >> 8) + 128;

		//YUYV
		if (i % 2 == 0)
		{

			pYUYVBuff[dwUYVYSize] = Y;
			pYUYVBuff[dwUYVYSize + 1] = U;
			pYUYVBuff[dwUYVYSize + 2] = Y;
			dwUYVYSize += 3;
		}
		else
		{
			pYUYVBuff[dwUYVYSize] = V;
			dwUYVYSize++;
		}
	}
// 	nCounter++;
// 	printf("%d, send complete successful - buffer size = %d\n", nCounter, dwUYVYSize);
// 	if (isFlush == false)
// 	{
// 		FILE *pFile = fopen("G:\\dmp_from_appl.bin", "wb");
// 		const size_t wrote = fwrite(pBuf, sizeof(char), dwUYVYSize, pFile);
// 		fclose(pFile);
// 		isFlush = true;
// 	}
	
	if (hDevice != INVALID_HANDLE_VALUE)
	{
		result = DeviceIoControl(hDevice, IOCTL_SEND_BUFFER_DATA, pBuf, dwUYVYSize, NULL, 0, NULL, NULL);
		if (result == false)
		{
			DWORD dwErr = GetLastError();
			printf("send not complete - buffer size = %d\nError code = %d\n", dwUYVYSize, dwErr);
		}
		else 
		{
			nCounter++;
			printf("%d, send complete successful - buffer size = %d\n", nCounter, dwUYVYSize);
		}
	}

	free(pYUYVBuff);
	return S_OK;
}

STDMETHODIMP CCallbackObject::BufferCB(double SampleTime, BYTE *pBuffer, long BufferLen)
{
	return S_OK;
}

void CCallbackObject::ConfigureDriver(VIDEOINFOHEADER *pVHeader)
{

	if (hDevice != INVALID_HANDLE_VALUE)
	{
		bool result = DeviceIoControl(hDevice, IOCTL_SEND_BUFFER_SIZE, pVHeader, sizeof(VIDEOINFOHEADER), NULL, 0, NULL, NULL);
		if (result == false)
		{
			DWORD dwErr = GetLastError();
			printf("send not complete - buffer size = %d\nError code = %d\n", sizeof(VIDEOINFOHEADER), dwErr);
		}
		else
		{
			printf("send complete successful - buffer size = %d\n", sizeof(VIDEOINFOHEADER));
		}
	}

}




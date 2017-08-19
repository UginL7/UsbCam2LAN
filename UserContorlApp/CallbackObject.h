#pragma once
#include "SampleGrabber.h"

class CCallbackObject :
	public ISampleGrabberCB
{
public:
	CCallbackObject();
	~CCallbackObject();

public:
	STDMETHODIMP QueryInterface(REFIID riid, void **ppv);
	STDMETHODIMP_(ULONG) AddRef();
	STDMETHODIMP_(ULONG) Release();

	//ISampleGrabberCB
	STDMETHODIMP SampleCB(double SampleTime, IMediaSample *pSample);
	STDMETHODIMP BufferCB(double SampleTime, BYTE *pBuffer, long BufferLen);

	void ConfigureDriver(VIDEOINFOHEADER *pVHeader);
private:
	DWORD m_Ref;
	HANDLE hDevice;
	bool isFlush = false;
	unsigned long nCounter = 0;
};


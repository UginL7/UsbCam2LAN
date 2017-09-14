#include "EnumDevice.h"

CEnumDevice::CEnumDevice()
{
	hr = CoCreateInstance(CLSID_SystemDeviceEnum, NULL, CLSCTX_INPROC_SERVER, IID_ICreateDevEnum, (void**)&pSystemDeviceEnum);
	if (FAILED(hr))
	{
		pSystemDeviceEnum = NULL;
	}
}

CEnumDevice::~CEnumDevice()
{
	pSystemDeviceEnum->Release();
}

// �������� ������ ������
void CEnumDevice::GetCompressedMethod(DWORD *dwCompressed, char *szFormat, DWORD dwBitCount)
{
	char *pCompressed = (char*)dwCompressed;
	if (*pCompressed == '\0')
	{
		sprintf(szFormat, "RGB%d", dwBitCount);
	}
	else
	{
		for (int i = 0; i < 4; i++)
		{
			*szFormat++ = *pCompressed++;
		}
	}
}

// ������ ���������� ��� ��������� ���� 
HRESULT CEnumDevice::GetClassEnumerator( REFCLSID clsidDeviceClass )
{
	if (pSystemDeviceEnum == NULL)
	{
		return S_FALSE;
	}
	hr = pSystemDeviceEnum->CreateClassEnumerator(clsidDeviceClass, &pEnumMoniker, 0);

	return hr;
}

// �������� �������� ���������
// ���� pDevParamInfo - �� �����, �� ���������� ���������� ��������� � �������
// ���� pDevParamInfo - �����, �� ���������� � ���� �������� ���������
int CEnumDevice::GetDeviceFriendlyName(device_param_info *pDevParamInfo)
{
	ULONG ulBuffSize = 0;
	IPropertyBag *pBag;
	VARIANT varName;
	ULONG ulFetched;
	struct device_param_info DevInfo;

	int nDeviceCounter = 0;
	while (pEnumMoniker->Next(1, &pMoniker, &ulFetched) == S_OK)
	{
		hr = pMoniker->BindToStorage(NULL, NULL, IID_IPropertyBag, (void**)&pBag);
		if (SUCCEEDED(hr))
		{
			VariantInit(&varName);
			hr = pBag->Read(L"Description", &varName, 0);
			if (FAILED(hr))
			{
				hr = pBag->Read(L"FriendlyName", &varName, 0);
			}
			if (SUCCEEDED(hr))
			{
				if (pDevParamInfo == nullptr)
				{
					nDeviceCounter++;
				}
				else
				{
					ulBuffSize = WideCharToMultiByte(CP_ACP, 0, (WCHAR *)((char *)varName.bstrVal), -1, 0, 0, 0, 0);
					if (ulBuffSize > 0)
					{
						memset((void*)&DevInfo, 0, sizeof(device_param_info));
						WideCharToMultiByte(CP_ACP, 0, (WCHAR *)((char *)varName.bstrVal), -1, DevInfo.szName, ulBuffSize, 0, 0);
						mbstowcs(DevInfo.wszName, DevInfo.szName, strlen(DevInfo.szName) * sizeof(wchar_t));
						hr = pMoniker->BindToObject(NULL, NULL, IID_IBaseFilter, (void**)&DevInfo.pBaseFilter);
						if (SUCCEEDED(hr))
						{
							memcpy(pDevParamInfo, (void*)&DevInfo, sizeof(device_param_info));
							pDevParamInfo++;
						}
						
					}

				}
			}
			VariantClear(&varName);
			pBag->Release();
		}
		pMoniker->Release();
	}
	if (pDevParamInfo == nullptr)
	{
		pEnumMoniker->Reset();
	}
	else
	{
		pEnumMoniker->Release();
	}
	return nDeviceCounter;
}

// ����������� ��� ���������� ������
// ���� pDevParamInfo - �� �����, �� ���������� ���������� ���������� 
// ���� pDevParamInfo - �����, �� ���������� � ���� �������������� ���������� � �������
int CEnumDevice::GetDeviceAvailableResolution(camera_frame_format_info *pCamResolution, IBaseFilter *pBaseFilter)
{
	int nCounter = 0;
	ULONG ulFetchedMT = 0;	
	ULONG ulFetched = 0;
	struct camera_frame_format_info pCamRes;
	AM_MEDIA_TYPE *pMT;
	VIDEOINFOHEADER *pVIH;	
	VIDEOINFOHEADER2 *pVIH2;
	IPin *pPin;
	IEnumPins *pEnumPin;
	IEnumMediaTypes *pEnumMediaType;
	IKsPropertySet *pKSPropSet;
	GUID PinCategory;
	DWORD dwRet;

	hr = pBaseFilter->EnumPins(&pEnumPin);
	if (SUCCEEDED(hr))
	{
		while (pEnumPin->Next(1, &pPin, &ulFetched) == S_OK)
		{
			hr = pPin->QueryInterface(IID_PPV_ARGS(&pKSPropSet));
			if (SUCCEEDED(hr))
			{
				hr = pKSPropSet->Get(AMPROPSETID_Pin, AMPROPERTY_PIN_CATEGORY, NULL, 0, &PinCategory, sizeof(GUID), &dwRet);
				if (SUCCEEDED(hr) && dwRet == sizeof(GUID))
				{
					if (PinCategory == PIN_CATEGORY_CAPTURE)
					{
						hr = pPin->EnumMediaTypes(&pEnumMediaType);
						if (SUCCEEDED(hr))
						{
							while (pEnumMediaType->Next(1, &pMT, &ulFetchedMT) == S_OK)
							{
								if (pCamResolution == nullptr)
								{
									nCounter++;
								}
								else
								{	
									if (pMT->formattype == FORMAT_VideoInfo)
									{
										if (pMT->cbFormat >= sizeof(VIDEOINFOHEADER))
										{
											pVIH = reinterpret_cast<VIDEOINFOHEADER *>(pMT->pbFormat);
											memset((void*)&pCamRes, 0, sizeof(camera_frame_format_info));
											pCamRes.majorType = pMT->majortype;
											pCamRes.subType = pMT->subtype;
											pCamRes.lSampleSize = pMT->lSampleSize;
											pCamRes.formatType = pMT->formattype;
											pCamRes.image_size.ulWidth = pVIH->bmiHeader.biWidth;
											pCamRes.image_size.ulHeight = pVIH->bmiHeader.biHeight;
											pCamRes.image_size.usBitCount = pVIH->bmiHeader.biBitCount;
											pCamRes.image_size.ulFrameSize = pVIH->bmiHeader.biSizeImage;
											pCamRes.biCompression = pVIH->bmiHeader.biCompression;
											strcpy_s(pCamRes.szVIH, "VideoInfo");
											GetCompressedMethod(&pVIH->bmiHeader.biCompression, pCamRes.szFormat, pCamRes.image_size.usBitCount);
											if (pVIH->bmiHeader.biCompression == 0 || pVIH->bmiHeader.biCompression == FOURCC('GPJM')/*MJPG*/)
											{
												pCamRes.isCSCNeeded = true;
											}
											if (pVIH->bmiHeader.biCompression == FOURCC('GPJM'))
											{
												pCamRes.isDecoderNeeded = true;
											}
											
											memcpy(pCamResolution, (void*)&pCamRes, sizeof(camera_frame_format_info));
											pCamResolution++;
										}
									}
									if (pMT->formattype == FORMAT_VideoInfo2)
									{
										if (pMT->cbFormat >= sizeof(VIDEOINFOHEADER2))
										{
											pVIH2 = reinterpret_cast<VIDEOINFOHEADER2 *>(pMT->pbFormat);
											memset((void*)&pCamRes, 0, sizeof(camera_frame_format_info));
											pCamRes.majorType = pMT->majortype;
											pCamRes.subType = pMT->subtype;
											pCamRes.lSampleSize = pMT->lSampleSize;
											pCamRes.formatType = pMT->formattype;
											pCamRes.image_size.ulWidth = pVIH2->bmiHeader.biWidth;
											pCamRes.image_size.ulHeight = pVIH2->bmiHeader.biHeight;
											pCamRes.image_size.usBitCount = pVIH2->bmiHeader.biBitCount;
											pCamRes.image_size.ulFrameSize = pVIH2->bmiHeader.biSizeImage;
											pCamRes.biCompression = pVIH2->bmiHeader.biCompression;
											strcpy_s(pCamRes.szVIH, "VideoInfo2");
											GetCompressedMethod(&pVIH2->bmiHeader.biCompression, pCamRes.szFormat, pCamRes.image_size.usBitCount);
											if (pVIH2->bmiHeader.biCompression == 0 || pVIH2->bmiHeader.biCompression == FOURCC('GPJM')/*MJPG*/)
											{
												pCamRes.isCSCNeeded = true;
											}
											if (pVIH2->bmiHeader.biCompression == FOURCC('GPJM'))
											{
												pCamRes.isDecoderNeeded = true;
											}
											memcpy(pCamResolution, (void*)&pCamRes, sizeof(camera_frame_format_info));
											pCamResolution++;
										}
									}
									
								}
							}
							pEnumMediaType->Release();
						}
					}
				}
			}
			pPin->Release();
		}
	}


	if (pCamResolution == nullptr)
	{
		pEnumPin->Reset();
	}
	else
	{
		pEnumPin->Release();
	}
	return nCounter;
}
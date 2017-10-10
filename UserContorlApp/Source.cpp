#include <conio.h>
#include <dshow.h>
#include <atlbase.h>
#include <initguid.h>
#include <dvdmedia.h>
#include <wmsdkidl.h>
#include <SetupAPI.h>

#include "EnumDevice.h"
#include "SampleGrabber.h"
#include "CallbackObject.h"
#include "structures.h"

#pragma comment (lib, "setupapi.lib")
#pragma comment(lib, "Strmiids.lib")
#pragma comment(lib, "Quartz.lib")

#define VBOX FALSE

struct camera_frame_format_info SelectedResolution;
struct device_param_info DevInfo;

static
const
CLSID CLSID_NullRenderer = { 0xC1F400A4, 0x3F08, 0x11d3,{ 0x9F, 0x0B, 0x00, 0x60, 0x08, 0x03, 0x9E, 0x37 } };

// {860BB310-5D01-11D0-BD3B-00A0C911CE86}
DEFINE_GUID(CLSID_VideoCaptureSource,
	0x860BB310, 0x5D01, 0x11D0, 0xBD, 0x3B, 0x00, 0xA0, 0xC9, 0x11, 0xCE, 0x86);

/*
// {C1F400A0-3F08-11D3-9F0B-006008039E37}
DEFINE_GUID(CLSID_SampleGrabber, 
	0xC1F400A0, 0x3F08, 0x11D3, 0x9F, 0x0B, 0x00, 0x60, 0x08, 0x03, 0x9E, 0x37); //qedit.dll*/

// {B87BEB7B-8D29-423F-AE4D-6582C10175AC}
DEFINE_GUID(CLSID_VideoRenderer,
	0xB87BEB7B, 0x8D29, 0x423F, 0xAE, 0x4D, 0x65, 0x82, 0xC1, 0x01, 0x75, 0xAC); //quartz.dll

void GetLastErrorMessage(char *szErrText)
{
	LPSTR szBuff = nullptr;
	DWORD dwErrCode = GetLastError();
	size_t size = FormatMessageA(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
		NULL, dwErrCode, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), (LPSTR)&szBuff, 0, NULL);
	sprintf(szErrText, "\t ==> Error\n\t\tCode = %d(0x%x)   Message:%s", dwErrCode, dwErrCode, szBuff);

	LocalFree(szBuff);
}


//////////////////////////////////////////////////////////////////////////
///                  Отправка потока с камеры в драйвер                ///
//////////////////////////////////////////////////////////////////////////

bool hrcheck(HRESULT hr, char *errText)
{
	DWORD dwRes = 0;
	TCHAR szErr[MAX_ERROR_TEXT_LEN] = { 0 };
	if (hr >= S_OK)
	{
		return false;
	}
	
	dwRes = AMGetErrorText(hr, szErr, MAX_ERROR_TEXT_LEN);
	if (dwRes > 0)
	{
		printf("Error %x: %s\n%s\n", hr, errText, szErr);
	}
	else
	{
		printf("Error %x: %s\n", hr, errText);
	}
	return true;
}

#define  CHECK_HR(hr, msg) if (hrcheck(hr, msg)) return hr;

CComPtr<IBaseFilter> CreateFilterByName(const WCHAR *filterName, const GUID& category)
{
	HRESULT hr = S_OK;
	CComPtr<ICreateDevEnum> pSysDevEnum;
	hr = pSysDevEnum.CoCreateInstance(CLSID_SystemDeviceEnum);
	if (hrcheck(hr, ("Can't create System Device Enumerator")))
	{
		return NULL;
	}

	CComPtr<IEnumMoniker> pEnumCat;
	hr = pSysDevEnum->CreateClassEnumerator(category, &pEnumCat, 0);

	if (hr == S_OK)
	{
		CComPtr<IMoniker> pMoniker;
		ULONG cFetched;
		while (pEnumCat->Next(1, &pMoniker, &cFetched) == S_OK)
		{
			CComPtr<IPropertyBag> pBag;
			hr = pMoniker->BindToStorage(0, 0, IID_IPropertyBag, (void **)&pBag);
			if (SUCCEEDED(hr))
			{
				VARIANT varName;
				VariantInit(&varName);
				hr = pBag->Read(L"FriendlyName", &varName, 0);
				if (SUCCEEDED(hr))
				{
					if (wcscmp(filterName, varName.bstrVal) == 0)
					{
						CComPtr<IBaseFilter> pFilter;
						hr = pMoniker->BindToObject(NULL, NULL, IID_IBaseFilter, (void**)&pFilter);
						if (hrcheck(hr, "Can't bind moniker to filter object"))
						{
							return NULL;
						}
						return pFilter;
					}
				}
				VariantClear(&varName);
			}
			pMoniker.Release();
		}
	}
	return NULL;
}

CComPtr<IPin> GetPin(IBaseFilter *pFilter, LPCOLESTR pinName)
{
	CComPtr<IEnumPins> pEnum;
	CComPtr<IPin> pPin;

	HRESULT hr = pFilter->EnumPins(&pEnum);
	if (hrcheck(hr, "Can't enumerate pins."))
		return NULL;

	while (pEnum->Next(1, &pPin, 0) == S_OK)
	{
		PIN_INFO pInfo;
		pPin->QueryPinInfo(&pInfo);
		bool bFound = !wcsicmp(pinName, pInfo.achName);
		if (pInfo.pFilter != NULL)
		{
			pInfo.pFilter->Release();
		}
		if (bFound == true)
		{
			return pPin;
		}
		pPin.Release();
	}
	printf("Pin not found!\n");
	return NULL;
}

HRESULT BuildGraph(IGraphBuilder *pGraph)
{
	HRESULT hr = S_OK;
	// Указатель на ICaptureGraphBuilder2 и добваление его в построитель графа
	CComPtr<ICaptureGraphBuilder2> pBuilder;
	hr = pBuilder.CoCreateInstance(CLSID_CaptureGraphBuilder2);
	CHECK_HR(hr, "Can't create Capture Graph Builder");
	hr = pBuilder->SetFiltergraph(pGraph);
	CHECK_HR(hr, "Can't SetFilterGraph ");

 // Получение указателя на камеру и добалвение фильтра в грфа
	CComPtr<IBaseFilter> pCamera = CreateFilterByName(DevInfo.wszName, CLSID_VideoCaptureSource);
	hr = pGraph->AddFilter(pCamera, DevInfo.wszName);
	CHECK_HR(hr, "Can't add camera to graph");

	// получение укзателя на SampleGrabber и добавление фильтра в граф
	CComPtr<IBaseFilter> pSGrabber;
	hr = pSGrabber.CoCreateInstance(CLSID_SampleGrabber);
	CHECK_HR(hr, "Can't create SampleGrabber");
	hr = pGraph->AddFilter(pSGrabber, L"SampleGrabber");
	CHECK_HR(hr, "Can't add SampleGrabber to graph");

	// заполнение структур медиатипа и видеозаголовка
	AM_MEDIA_TYPE SG_media_type;
	ZeroMemory(&SG_media_type, sizeof(AM_MEDIA_TYPE));
	SG_media_type.majortype = MEDIATYPE_Video;
	SG_media_type.subtype = MEDIASUBTYPE_RGB24;
	SG_media_type.formattype = FORMAT_VideoInfo;
	SG_media_type.bFixedSizeSamples = TRUE;
	SG_media_type.cbFormat = 88;
	SG_media_type.lSampleSize = SelectedResolution.image_size.ulFrameSize;
	SG_media_type.bTemporalCompression = FALSE;

	VIDEOINFOHEADER pSG_video_header_format;
	ZeroMemory(&pSG_video_header_format, sizeof(VIDEOINFOHEADER));
	pSG_video_header_format.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
	pSG_video_header_format.bmiHeader.biWidth = SelectedResolution.image_size.ulWidth;
	pSG_video_header_format.bmiHeader.biHeight = SelectedResolution.image_size.ulHeight;
	pSG_video_header_format.bmiHeader.biPlanes = 1;
	pSG_video_header_format.bmiHeader.biBitCount = 24;
	pSG_video_header_format.bmiHeader.biSizeImage = SelectedResolution.image_size.ulFrameSize;
	SG_media_type.pbFormat = (BYTE *)&pSG_video_header_format;
	
//////////////////////////////////////////////////////////////////////////
	IAMStreamConfig *pConfig = NULL;
	hr = pBuilder->FindInterface(&PIN_CATEGORY_CAPTURE, 0, pCamera,  IID_IAMStreamConfig, (void**)&pConfig);
	int iCount = 0, iSize = 0;
	hr = pConfig->GetNumberOfCapabilities(&iCount, &iSize);
	AM_MEDIA_TYPE *pSG_media_type;
	// Check the size to make sure we pass in the correct structure.
	if (iSize == sizeof(VIDEO_STREAM_CONFIG_CAPS))
	{
		// Use the video capabilities structure.

		VIDEO_STREAM_CONFIG_CAPS scc;

		for (int iFormat = 0; iFormat < iCount; iFormat++)
		{
			hr = pConfig->GetStreamCaps(iFormat, &pSG_media_type, (BYTE*)&scc);
			if (SUCCEEDED(hr))
			{
				if (scc.InputSize.cx == SelectedResolution.image_size.ulWidth && scc.InputSize.cy == SelectedResolution.image_size.ulHeight)
				{
					CComQIPtr<IAMStreamConfig, &IID_IAMStreamConfig> isc(GetPin(pCamera, L"Запись"));
					hr = isc->SetFormat(pSG_media_type);
					CHECK_HR(hr, "Can't set camera work resolution");
					break;
				}
			}
		}
	}
//////////////////////////////////////////////////////////////////////////

	// получение указателя ISampleGrabber указание ему медиатипа 
	CComQIPtr<ISampleGrabber, &IID_ISampleGrabber> pSG_sample_grabber(pSGrabber);
	hr = pSG_sample_grabber->SetMediaType(&SG_media_type);
	CHECK_HR(hr, "Can't set media type to sample grabber");
	/////////////////////////
	// Вызов callback - работа  с потоком (0 - SampleCB, 1 - BufferCB)
	hr = pSG_sample_grabber->SetCallback(new CCallbackObject, 0);
	CHECK_HR(hr, _T("Can't set callback SampleCB"));


	// Получение указателя для Color Space Converter и добавление его в фильтр граф
	CComPtr<IBaseFilter> pColorSpaceConverter;
	hr = pColorSpaceConverter.CoCreateInstance(CLSID_Colour);
	CHECK_HR(hr, "Can't creater Color Space Converter");
	hr = pGraph->AddFilter(pColorSpaceConverter, L"Color Space Converter");
	CHECK_HR(hr, "Can't add CSC to graph");

	// Получение указателя для VideoRenderer и добавление его в фильтр граф
	CComPtr<IBaseFilter> pVideoRend;
	hr = pVideoRend.CoCreateInstance(CLSID_VideoRenderer);
	CHECK_HR(hr, "Can't create Video Renderer");
	hr = pGraph->AddFilter(pVideoRend, L"Video Renderer");
	CHECK_HR(hr, "Can't add Video Renderer to graph");
	


#if VBOX
	// Последовательное соединение Источник(камера)->Декомпрессор->SampleGrabber(установка перехвата)->ColorSpaceConverter->VideoRenderer
	//add MJPEG Decompressor
	CComPtr<IBaseFilter> pMJPEGDecompressor;
	hr = pMJPEGDecompressor.CoCreateInstance(CLSID_MjpegDec);
	CHECK_HR(hr, _T("Can't create MJPEG Decompressor"));
	hr = pGraph->AddFilter(pMJPEGDecompressor, L"MJPEG Decompressor");
	CHECK_HR(hr, _T("Can't add MJPEG Decompressor to graph"));

	//connect VirtualBox Webcam - Logitech QuickCam Pro 5000 and MJPEG Decompressor
	hr = pGraph->ConnectDirect(GetPin(pCamera, L"Запись"), GetPin(pMJPEGDecompressor, L"XForm In"), NULL);
	CHECK_HR(hr, _T("Can't connect Camera_Out and MJPEG Decompressor"));

	MessageBox(NULL, "Error!", "Error!", MB_OK);
	//connect MJPEG Decompressor and SampleGrabber
	hr = pGraph->ConnectDirect(GetPin(pMJPEGDecompressor, L"XForm Out"), GetPin(pSGrabber, L"Input"), NULL);
	CHECK_HR(hr, _T("Can't connect MJPEG Decompressor and SampleGrabber"));
	
#else
	// Последовательное соединение Источник(камера)->SampleGrabber(установка перехвата)->ColorSpaceConverter->VideoRenderer
	hr = pGraph->ConnectDirect(GetPin(pCamera, L"Запись"), GetPin(pSGrabber, L"Input"), NULL);
	CHECK_HR(hr, _T("Can't connect Camera_Out SmapleGrabber_In"));
#endif
	
	//connect SampleGrabber and Color Space Converter
	hr = pGraph->ConnectDirect(GetPin(pSGrabber, L"Output"), GetPin(pColorSpaceConverter, L"Input"), NULL);
	CHECK_HR(hr, _T("Can't connect SampleGrabber and Color Space Converter"));
	hr = pGraph->ConnectDirect(GetPin(pColorSpaceConverter, L"XForm Out"), GetPin(pVideoRend, L"VMR Input0"), NULL);
	CHECK_HR(hr, "Can't connect CSC and Video Renderer");

	return S_OK;
}

//ТЕстовая ф-ция для трансляции BMP файла в драйвер
void TranslateBMPFile()
{
	char *pOriginal = nullptr;
	FILE *pFile = fopen("bike240.bmp", "rb");
	rewind(pFile);

	pOriginal = (char*)malloc(SelectedResolution.image_size.ulFrameSize);
	if (pOriginal == nullptr)
	{
		printf("Not enough memory!\n");
		system("pause");
		exit(-1);
	}
	strset(pOriginal, '0');

	fseek(pFile, 54, SEEK_SET);
	size_t nBuffSize = fread(pOriginal, sizeof(char), SelectedResolution.image_size.ulFrameSize, pFile);
	fclose(pFile);

	unsigned long nCounter = 0;
	unsigned long dwUYVYSize = 0;
	unsigned char *pYUYVBuff;
	int byteColor = 2;
	int pos = -1 * SelectedResolution.image_size.ulWidth;
	int line = 1;
	pYUYVBuff = (unsigned char*)malloc(SelectedResolution.image_size.ulWidth * SelectedResolution.image_size.ulHeight * byteColor);
	memset(pYUYVBuff, 0, SelectedResolution.image_size.ulWidth * SelectedResolution.image_size.ulHeight * byteColor);


	for (int i = 0; i < nBuffSize; i += 3)
	{
		unsigned char B = pOriginal[i];
		unsigned char G = pOriginal[i + 1];
		unsigned char R = pOriginal[i + 2];

		unsigned char Y = ((66 * R + 129 * G + 25 * B + 128) >> 8) + 16;
		unsigned char U = ((-38 * R - 74 * G + 112 * B + 128) >> 8) + 128;
		unsigned char V = ((112 * R - 94 * G - 18 * B + 128) >> 8) + 128;

		//YUYV
		if (i % 2 == 0)
		{
			// Запись строк снизу вверх. Для того, чтобы получить не перевернутую картинку
			pYUYVBuff[pos + (byteColor * SelectedResolution.image_size.ulHeight - line) * SelectedResolution.image_size.ulWidth] = Y;
			pYUYVBuff[pos + 1 + (byteColor * SelectedResolution.image_size.ulHeight - line) * SelectedResolution.image_size.ulWidth] = U;
			pYUYVBuff[pos + 2 + (byteColor * SelectedResolution.image_size.ulHeight - line) * SelectedResolution.image_size.ulWidth] = Y;
			dwUYVYSize += 3;
			pos += 3;
		}
		else
		{
			pYUYVBuff[pos + (byteColor * SelectedResolution.image_size.ulHeight - line) * SelectedResolution.image_size.ulWidth] = V;
			dwUYVYSize++;
			pos++;
		}

		if (pos == SelectedResolution.image_size.ulWidth)
		{
			pos = 0;
			line++;

		}
	}

	HANDLE hDevice;
	hDevice = CreateFile("\\\\.\\VideoControl", GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL);
	if (hDevice == INVALID_HANDLE_VALUE)
	{
		DWORD dwError = GetLastError();
		CloseHandle(hDevice);
	}

	do {
		nBuffSize = DeviceIoControl(hDevice, IOCTL_SEND_BUFFER_DATA, pYUYVBuff, dwUYVYSize, NULL, 0, NULL, NULL);
		if (nBuffSize == false)
		{
			DWORD dwErr = GetLastError();
			printf("send not complete - buffer size = %d\nError code = %d\n", dwUYVYSize, dwErr);
		}
		else
		{
			nCounter++;
			printf("%d, send complete successful - buffer size = %d\n", nCounter, dwUYVYSize);
		}
		Sleep(100);
	} while (nCounter < 30);

	free(pOriginal);
	free(pYUYVBuff);
}

// Функция транслирующая в драйвер видео поток с камеры 
int TranslateWebCamStream()
{
	CoInitialize(NULL);
	CComPtr<IGraphBuilder> graph;
	graph.CoCreateInstance(CLSID_FilterGraph);

	wprintf(L"Camera Name = %s\nResolution w=%d\th=%d\n", DevInfo.wszName, SelectedResolution.image_size.ulWidth, SelectedResolution.image_size.ulHeight);
	system("pause");
	printf("Building graph...\n");
	HRESULT hr = BuildGraph(graph);
	//HRESULT hr = BuildGraph_StreamControl(graph);
	if (hr == S_OK)
	{
		printf("Running");
		CComQIPtr<IMediaControl, &IID_IMediaControl> mediaControl(graph);
		hr = mediaControl->Run();
		CHECK_HR(hr, "Can't run");
		CComQIPtr<IMediaEvent, &IID_IMediaEvent> mediaEvent(graph);
		bool bStop = false;
		MSG msg;
		while (!bStop)
		{
			long ev = 0;
			long p1 = 0;
			long p2 = 0;
			//			printf(".");
			while (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE))
			{
				DispatchMessage(&msg);
			}
			while (mediaEvent->GetEvent(&ev, &p1, &p2, 0) == S_OK)
			{
				printf("Event ID = %x\n", ev);
				if (ev == EC_COMPLETE || ev == EC_USERABORT)
				{
					printf("Done!\n");
					bStop = true;
				}
				else if (ev == EC_ERRORABORT)
				{
					printf("An error occured: HRESULT=%x\n", p1);
					mediaControl->Stop();
					bStop = true;
				}
				mediaEvent->FreeEventParams(ev, p1, p2);
			}
		}
	}
	CoUninitialize();

	return 0;
}

//////////////////////////////////////////////////////////////////////////
///           Отправка разрешений и форматов сжатия в драйвер          ///
//////////////////////////////////////////////////////////////////////////

// Отправка разрешения в драйвер 
void SendBufferToDriver(void * buffRes, unsigned long ulSize)
{
	HANDLE hDriverOpen = CreateFile("\\\\.\\VideoControl", GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL);
	if (hDriverOpen == INVALID_HANDLE_VALUE)
	{
		DWORD dwError = GetLastError();
		CloseHandle(hDriverOpen);
	}

	bool result = DeviceIoControl(hDriverOpen, IOCTL_SEND_BUFFER_FORMAT, buffRes, ulSize, NULL, 0, NULL, NULL);
	if (result == false)
	{
		DWORD dwErr = GetLastError();
		printf("send not complete - buffer size = %d\nError code = %d\n", ulSize, dwErr);
	}
	
	CloseHandle(hDriverOpen);
}

// Перезагрузка драйвера
void RestartDevice(char *szSvcRestart)
{
	int i = 0;
	BOOL bRes = FALSE;
	DWORD dwBuffSize = 512;
	char szSvcName[512] = { 0 };
	char szErrBuff[1024] = { 0 };

	DWORD numGuids = 0;
	DWORD reqGuids = 16;
	LPGUID guids = new GUID[reqGuids];
	bRes = SetupDiClassGuidsFromName("Media", guids, reqGuids, &numGuids);
	if (bRes == FALSE)
	{
		GetLastErrorMessage(szErrBuff);
		OutputDebugString(szErrBuff);
		return;
	}

	for (int i = 0; i < numGuids; i++)
	{
		SP_PROPCHANGE_PARAMS spPropChangeParam;
		spPropChangeParam.ClassInstallHeader.cbSize = sizeof(SP_CLASSINSTALL_HEADER);
		spPropChangeParam.ClassInstallHeader.InstallFunction = DIF_PROPERTYCHANGE;
		spPropChangeParam.Scope = DICS_FLAG_CONFIGSPECIFIC;
		spPropChangeParam.StateChange = DICS_PROPCHANGE;
		spPropChangeParam.HwProfile = 0;

		HDEVINFO hDevInfo = SetupDiGetClassDevs(&guids[i], NULL, NULL, DIGCF_PRESENT);
		SP_DEVINFO_DATA spDevInfoData;
		spDevInfoData.cbSize = sizeof(SP_DEVINFO_DATA);

		while (SetupDiEnumDeviceInfo(hDevInfo, i, &spDevInfoData) == TRUE)
		{

			bRes = SetupDiGetDeviceRegistryProperty(hDevInfo, &spDevInfoData, SPDRP_SERVICE, NULL, (PBYTE)szSvcName, dwBuffSize, &dwBuffSize);
			if (bRes == FALSE)
			{
				GetLastErrorMessage(szErrBuff);
				OutputDebugString(szErrBuff);
				break;
			}
			if (strcmp(szSvcName, szSvcRestart) == 0)
			{
				bRes = SetupDiSetClassInstallParams(hDevInfo, &spDevInfoData, &spPropChangeParam.ClassInstallHeader, sizeof(spPropChangeParam));
				if (bRes == FALSE)
				{
					GetLastErrorMessage(szErrBuff);
					OutputDebugString(szErrBuff);
					break;
				}
				bRes = SetupDiCallClassInstaller(DIF_PROPERTYCHANGE, hDevInfo, &spDevInfoData);
				if (bRes == FALSE)
				{
					GetLastErrorMessage(szErrBuff);
					OutputDebugString(szErrBuff);
					break;
				}
				break;
			}
			i++;
		}

		SetupDiDestroyDeviceInfoList(hDevInfo);
	}
	delete guids;
}

// Получение разрешений форматов 
int GetCameraResolution()
{
	device_param_info *pAvailableDevices = nullptr;
	camera_frame_format_info *pAvailableResolution = nullptr;
	camera_frame_format_info *pResOriginal;
	device_param_info *pDevOriginal;
	int nSizeOfDeviceArray = 0;
	int nSizeOfResolutionArray = 0;
	int nSizeNeeded = 0;
	HRESULT hr;
	CEnumDevice *pEnumDevice = nullptr;
	int nCameraChoise = 0;
	int nResolutionChoise = 0;
	int nRet = 0;

	while (true)
	{
		hr = CoInitializeEx(NULL, COINIT_MULTITHREADED);
		if (hr != S_OK)
		{
			printf("CoInititializeEx error");
			nRet = -1;
			break;
		}

		pEnumDevice = new CEnumDevice();

		// получение энумератора камер(устройств видео захвата)
		hr = pEnumDevice->GetClassEnumerator(CLSID_VideoInputDeviceCategory);
		if (hr != S_OK)
		{
			printf("GetClassEnumerator error");
			nRet = -1;
			break;
		}

		// Получение количества доступных камер
		nSizeOfDeviceArray = pEnumDevice->GetDeviceFriendlyName(nullptr);
		if (nSizeOfDeviceArray == 0)
		{
			printf("GetDeviceFriendlyName error");
			nRet = -1;
			break;
		}

		pAvailableDevices = (device_param_info *)malloc(nSizeOfDeviceArray * sizeof(device_param_info));
		if (pAvailableDevices == nullptr)
		{
			printf("malloc device_param_info error");
			nRet = -1;
			break;
		}
		pDevOriginal = pAvailableDevices;

		// Перечисление доступных камер
		memset(pAvailableDevices, 0, nSizeOfDeviceArray * sizeof(device_param_info));
		pEnumDevice->GetDeviceFriendlyName(pAvailableDevices);
		for (int i = 0; i < nSizeOfDeviceArray; i++)
		{
			DevInfo = pAvailableDevices[0];
			printf("%d)\tname = %s\n\n", i + 1, DevInfo.szName);

			printf("\n\n=================================================\n\n");
			pAvailableDevices++;
		}

		// выбор камеры для работы
		printf("Input camera number : ");
		scanf_s("%d", &nCameraChoise);
		pAvailableDevices = pDevOriginal + nCameraChoise - 1;
		DevInfo = pAvailableDevices[0];

		// Получение количества разрешений для выбранной камеры 
		nSizeOfResolutionArray = pEnumDevice->GetDeviceAvailableResolution(nullptr, DevInfo.pBaseFilter);
		pAvailableResolution = (camera_frame_format_info *)malloc(sizeof(camera_frame_format_info) * nSizeOfResolutionArray);
		if (pAvailableResolution == nullptr)
		{
			printf("malloc camera_frame_format_info error");
			nRet = -1;
			break;
		}
		pResOriginal = pAvailableResolution;

		// Перечисление для выбранной камеры разрешений и форматов картинки
		memset(pAvailableResolution, 0, nSizeOfResolutionArray * sizeof(camera_frame_format_info));
		pEnumDevice->GetDeviceAvailableResolution(pAvailableResolution, DevInfo.pBaseFilter);
		pResOriginal = pAvailableResolution;
		for (int i = 0; i < nSizeOfResolutionArray; i++)
		{
			printf("%d)\twidth = %d\theight=%d\tbit color=%d\tcompressed=%s\tVIH=%s\n", i + 1, pAvailableResolution[i].image_size.ulWidth, pAvailableResolution[i].image_size.ulHeight, pAvailableResolution[i].image_size.usBitCount, pAvailableResolution[i].szFormat, pAvailableResolution[i].szVIH);
		}

		// выбор разрешения для работы
		printf("Input resolution number : ");
		scanf_s("%d", &nResolutionChoise);
		pAvailableResolution = (camera_frame_format_info*)pResOriginal + nResolutionChoise - 1;
		SelectedResolution = pAvailableResolution[0];

		// отправка данных в драйвер
 		pAvailableResolution = (camera_frame_format_info*)pResOriginal;
// 		SendBufferToDriver(pAvailableResolution, nSizeOfResolutionArray * sizeof(camera_frame_format_info));
// 		RestartDevice("avshws");

		printf("%d)\twidth = %d\theight=%d\tbit color=%d\tcompressed=%s\tVIH=%s\n", nResolutionChoise, SelectedResolution.image_size.ulWidth, SelectedResolution.image_size.ulHeight, SelectedResolution.image_size.usBitCount, SelectedResolution.szFormat, SelectedResolution.szVIH);
		break;
	}


	if (pAvailableResolution != nullptr)
	{
		pAvailableResolution = (camera_frame_format_info*)pResOriginal;
		free(pAvailableResolution);
	}

	if (pAvailableDevices != nullptr)
	{
		pAvailableDevices = (device_param_info *)pDevOriginal;
		// Освобождение ресурсов
		for (int i = 0; i < nSizeOfDeviceArray; i++)
		{
			DevInfo = pAvailableDevices[0];
			DevInfo.pBaseFilter->Release();
			pAvailableDevices++;
		}
		pAvailableDevices = (device_param_info *)pDevOriginal;
		free(pAvailableDevices);
	}

	if (pEnumDevice != nullptr)
	{
		delete pEnumDevice;
	}

	CoUninitialize();
	return nRet;
}



int main()
{
	DWORD dw = sizeof(GUID);
 	SetConsoleCP(1251);
 	SetConsoleOutputCP(1251);
	int nRet = 0;
	nRet = GetCameraResolution();
	nRet = TranslateWebCamStream();
	
	//RestartDevice("avshws");
	system("pause");
	return nRet;
}
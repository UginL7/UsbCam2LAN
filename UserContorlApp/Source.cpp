#include <conio.h>
#include <dshow.h>
#include <atlbase.h>
#include <initguid.h>
#include <dvdmedia.h>
#include <wmsdkidl.h>
#include "SampleGrabber.h"
#include "CallbackObject.h"

#pragma comment(lib, "Strmiids.lib")
#pragma comment(lib, "Quartz.lib")

#define VBOX TRUE

#if VBOX
WCHAR wszCamName[] = L"VirtualBox Webcam - Logitech QuickCam Pro 5000";
#else
WCHAR wszCamName[] = L"Logitech QuickCam Pro 5000";
#endif



static
const
CLSID CLSID_NullRenderer = { 0xC1F400A4, 0x3F08, 0x11d3,{ 0x9F, 0x0B, 0x00, 0x60, 0x08, 0x03, 0x9E, 0x37 } };

// {860BB310-5D01-11D0-BD3B-00A0C911CE86}
DEFINE_GUID(CLSID_VideoCaptureSource,
	0x860BB310, 0x5D01, 0x11D0, 0xBD, 0x3B, 0x00, 0xA0, 0xC9, 0x11, 0xCE, 0x86);

// {C1F400A0-3F08-11D3-9F0B-006008039E37}
DEFINE_GUID(CLSID_SampleGrabber, 
	0xC1F400A0, 0x3F08, 0x11D3, 0x9F, 0x0B, 0x00, 0x60, 0x08, 0x03, 0x9E, 0x37); //qedit.dll

// {B87BEB7B-8D29-423F-AE4D-6582C10175AC}
DEFINE_GUID(CLSID_VideoRenderer,
	0xB87BEB7B, 0x8D29, 0x423F, 0xAE, 0x4D, 0x65, 0x82, 0xC1, 0x01, 0x75, 0xAC); //quartz.dll


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
	CCallbackObject* pCBObject;
	HRESULT hr = S_OK;
	// Указатель на ICaptureGraphBuilder2 и добваление его в построитель графа
	CComPtr<ICaptureGraphBuilder2> pBuilder;
	hr = pBuilder.CoCreateInstance(CLSID_CaptureGraphBuilder2);
	CHECK_HR(hr, "Can't create Capture Graph Builder");
	hr = pBuilder->SetFiltergraph(pGraph);
	CHECK_HR(hr, "Can't SetFilterGraph ");

 // Получение указателя на камеру и добалвение фильтра в грфа
	CComPtr<IBaseFilter> pCamera = CreateFilterByName(wszCamName, CLSID_VideoCaptureSource);
	hr = pGraph->AddFilter(pCamera, wszCamName);
	CHECK_HR(hr, "Can't add camera to graph");

	// получение укзателя на SampleGrabber и добавление фильтра в граф
	CComPtr<IBaseFilter> pSGrabber;
	hr = pSGrabber.CoCreateInstance(CLSID_SampleGrabber);
	CHECK_HR(hr, "Can't create SampleGrabber");
	hr = pGraph->AddFilter(pSGrabber, L"SampleGrabber");
	CHECK_HR(hr, "Can't add SampleGrabber to graph");

	// заполнение структур медиатипа и видеозаголовка
	AM_MEDIA_TYPE pSG_media_type;
	ZeroMemory(&pSG_media_type, sizeof(AM_MEDIA_TYPE));
	pSG_media_type.majortype = MEDIATYPE_Video;

	pSG_media_type.subtype = MEDIASUBTYPE_RGB24;

	pSG_media_type.formattype = FORMAT_VideoInfo;
	pSG_media_type.bFixedSizeSamples = TRUE;
	pSG_media_type.cbFormat = 88;
	pSG_media_type.lSampleSize = 230400;
	pSG_media_type.bTemporalCompression = FALSE;

	VIDEOINFOHEADER pSG_video_header_format;
	ZeroMemory(&pSG_video_header_format, sizeof(VIDEOINFOHEADER));
	pSG_video_header_format.bmiHeader.biSize = 40;
	pSG_video_header_format.bmiHeader.biWidth = 320;
	pSG_video_header_format.bmiHeader.biHeight = 240;
	pSG_video_header_format.bmiHeader.biPlanes = 1;
	pSG_video_header_format.bmiHeader.biBitCount = 24;
	pSG_video_header_format.bmiHeader.biSizeImage = 230400;
	pSG_media_type.pbFormat = (BYTE *)&pSG_video_header_format;

	// получение указателя ISampleGrabber указание ему медиатипа 
	CComQIPtr<ISampleGrabber, &IID_ISampleGrabber> pSG_sample_grabber(pSGrabber);
	hr = pSG_sample_grabber->SetMediaType(&pSG_media_type);
	CHECK_HR(hr, "Can't set media type to sample grabber");

	// Получение указателя для декомпрессорa и добавление его в фильтр граф
	CComPtr<IBaseFilter> pDecompressor;
#if VBOX
	hr = pDecompressor.CoCreateInstance(CLSID_MjpegDec);
	CHECK_HR(hr, "Can't create MJPEG Decompressor");
	hr = pGraph->AddFilter(pDecompressor, L"MJPEG Decompressor");
	CHECK_HR(hr, "Can't add MJPEG Decompressor to graph");

#else
	hr = pDecompressor.CoCreateInstance(CLSID_AVIDec);
	CHECK_HR(hr, "Can't create AVI Decompressor");
	hr = pGraph->AddFilter(pDecompressor, L"AVI Decompressor");
	CHECK_HR(hr, "Can't add AVI Decompressor to graph");

#endif
	
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
	
	// Последовательное соединение Источник(камера)->Декомпрессор->SampleGrabber(установка перехвата)->ColorSpaceConverter->VideoRenderer
	
	hr = pGraph->ConnectDirect(GetPin(pCamera, L"Запись"), GetPin(pDecompressor, L"XForm In"), NULL);
	CHECK_HR(hr, _T("Can't connect Camera_Out and MJPEG_Decompressor_In"));
	
	hr = pGraph->ConnectDirect(GetPin(pDecompressor, L"XForm Out"), GetPin(pSGrabber, L"Input"), NULL);
	CHECK_HR(hr, _T("Can't connect MJPEG_Decompressor_Out SmapleGrabber_In"));

	/////////////////////////
	// Вызов callback - работа  с потоком
	/////////////////////////
	CComQIPtr<ISampleGrabber, &IID_ISampleGrabber> pSampleGrabber_isg(pSGrabber);
	// Вызов ф-ции (0 - SampleCB, 1 - BufferCB) для работы с потоком
	hr = pSampleGrabber_isg->SetCallback(pCBObject = new CCallbackObject(), 0);
	CHECK_HR(hr, _T("Can't set callback"));
	pCBObject->ConfigureDriver(&pSG_video_header_format);

	hr = pGraph->ConnectDirect(GetPin(pSGrabber, L"Output"), GetPin(pColorSpaceConverter, L"Input"), NULL);
	CHECK_HR(hr, _T("Can't connect SmapleGrabber_Out CSC_In"));
	

	hr = pGraph->ConnectDirect(GetPin(pColorSpaceConverter, L"XForm Out"), GetPin(pVideoRend, L"VMR Input0"), NULL);
	CHECK_HR(hr, "Can't connect CSC and Video Renderer");

	return S_OK;
}

// В этой ф-ции что-то портит буфер! Она не работает, как надо
// При вызове callback-функции приходят в буфере только 0
HRESULT BuildGraph_StreamControl(IGraphBuilder *pGraph)
{
	
	HRESULT hr = S_OK;

	// Создание основного графа
	CComPtr<ICaptureGraphBuilder2> pBuilder;
	hr = pBuilder.CoCreateInstance(CLSID_CaptureGraphBuilder2);
	CHECK_HR(hr, "Can't create Capture Graph Builder");
	// установка фильтра графа строителя :)
	hr = pBuilder->SetFiltergraph(pGraph);
	CHECK_HR(hr, "Can't SetFiltergraph");

	// Подключение камеры и добавления фильтра камеры
	CComPtr<IBaseFilter> pCamera = CreateFilterByName(wszCamName, CLSID_VideoCaptureSource);
	hr = pGraph->AddFilter(pCamera, wszCamName);
	CHECK_HR(hr, _T("Can't add USB2.0 Camera to graph"));

	// Заполнения структур для тип данных и заголовка формата
	AM_MEDIA_TYPE pSG_media_type;
	ZeroMemory(&pSG_media_type, sizeof(AM_MEDIA_TYPE));
	pSG_media_type.majortype = MEDIATYPE_Video;
#if VBOX
	pSG_media_type.subtype = MEDIASUBTYPE_MJPG;	
#else
	pSG_media_type.subtype = WMMEDIASUBTYPE_I420;
#endif
	pSG_media_type.formattype = FORMAT_VideoInfo;
	pSG_media_type.bFixedSizeSamples = TRUE;
	pSG_media_type.cbFormat = 88;
	pSG_media_type.lSampleSize = 115200;
	pSG_media_type.bTemporalCompression = FALSE;

	VIDEOINFOHEADER pSG_video_header;
	ZeroMemory(&pSG_video_header, sizeof(VIDEOINFOHEADER));
	pSG_video_header.bmiHeader.biSize = 40;
	pSG_video_header.bmiHeader.biWidth = 320;
	pSG_video_header.bmiHeader.biHeight = 240;
	pSG_video_header.bmiHeader.biPlanes = 1;
	pSG_video_header.bmiHeader.biBitCount = 12;
#if VBOX
	pSG_video_header.bmiHeader.biCompression = 'MJPG';	
#else
	pSG_video_header.bmiHeader.biCompression = 808596553;
#endif
	
	pSG_video_header.bmiHeader.biSizeImage = 115200;
	pSG_media_type.pbFormat = (BYTE *)&pSG_video_header;

	// получение пина захвата и установка формата для него
	CComQIPtr<IAMStreamConfig, &IID_IAMStreamConfig> pSG_MediaControl(GetPin(pCamera, L"Запись"));
	hr = pSG_MediaControl->SetFormat(&pSG_media_type);
	CHECK_HR(hr, "Can't set format");

	// получение интерфейса декомпрессора
	if (pSG_video_header.bmiHeader.biCompression == 'MJPG')
	{
		// тут надо сделать switch под разные типы декопрессоров
		CComPtr<IBaseFilter> pDecompressorMJPEG;
		hr = pDecompressorMJPEG.CoCreateInstance(CLSID_MjpegDec);
		CHECK_HR(hr, "Can't create DecompressorMJPEG");
		hr = pGraph->AddFilter(pDecompressorMJPEG, L"MJPEG Decompressor");
		CHECK_HR(hr, _T("Can't add MJPEG Decompressor to graph"));

		// Подключение сразу на выход с камеры
		hr = pGraph->ConnectDirect(GetPin(pCamera, L"Запись"), GetPin(pDecompressorMJPEG, L"XForm In"), NULL);
		CHECK_HR(hr, _T("Can't connect Camera_Out and MJPEG_Decompressor_In"));
	}


	// Получение интрефейса для работы с потоком и добавление его фильтра в граф
	CComPtr<IBaseFilter> pSampleGrabber;
	hr = pSampleGrabber.CoCreateInstance(CLSID_SampleGrabber);
	CHECK_HR(hr, "Can't create SampleGrabber");
	hr = pGraph->AddFilter(pSampleGrabber, L"SampleGrabber");
	CHECK_HR(hr, _T("Can't add SampleGrabber to graph"));

	/////////////////////////
	// Вызов callback - работа  спотоком
	/////////////////////////
	CComQIPtr<ISampleGrabber, &IID_ISampleGrabber> pSampleGrabber_isg(pSampleGrabber);
	// Вызов ф-ции (0 - SampleCB, 1 - BufferCB) для работы с потоком
	hr = pSampleGrabber_isg->SetCallback(new CCallbackObject(), 0);
	CHECK_HR(hr, _T("Can't set callback"));

	
	
	// подключение камеры к фильтру
	hr = pBuilder->RenderStream(NULL, NULL, pCamera, NULL, pSampleGrabber);
	CHECK_HR(hr, _T("Can't render stream to SampleGrabber"));
	
	
	// отрисовка видео в окне
	CComPtr<IBaseFilter> pVideoRenderer;
	hr = pVideoRenderer.CoCreateInstance(CLSID_VideoRenderer);
	CHECK_HR(hr, _T("Can't create VideoRenderer"));
	hr = pGraph->AddFilter(pVideoRenderer, L"VideoRenderer");
	// Если последний параметр поставить NULL, то тоже будет работать. Просто подхватит дефолтный рендерер
	hr = pBuilder->RenderStream(NULL, NULL, pSampleGrabber, NULL, pVideoRenderer);
	CHECK_HR(hr, _T("Can't render stream from SampleGrabber"));
	

	/*
	// Без вывода в окно
	CComPtr<IBaseFilter> pNullRenderer;
	hr = pNullRenderer.CoCreateInstance(CLSID_NullRenderer);
	CHECK_HR(hr, _T("Can't create NullRenderer"));
	hr = pGraph->AddFilter(pNullRenderer, L"NullRenderer");
	
	hr = pBuilder->RenderStream(NULL, NULL, pSampleGrabber, NULL, pNullRenderer);
	CHECK_HR(hr, _T("Can't render stream from SampleGrabber"));
	*/
	return S_OK;
}

int main()
{
#if VBOX
	SetConsoleCP(1251);
	SetConsoleOutputCP(1251);
	CoInitialize(NULL);
	CComPtr<IGraphBuilder> graph;
	graph.CoCreateInstance(CLSID_FilterGraph);

#if VBOX
	printf("In Virtual Box\n");
#else
	printf("In PC\n");	
#endif
	wprintf(L"Camera Name = %s\n", wszCamName);

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
	system("pause");
	return 0;
#else
	char pOriginal[230400] = { 0 };
	FILE *pFile = fopen("bike240.bmp", "rb");
	rewind(pFile);
	fseek(pFile, 54, SEEK_SET);
	size_t nBuffSize= fread(pOriginal, sizeof(char), 230400, pFile);
	fclose(pFile);

	unsigned long nCounter = 0;
	unsigned long dwUYVYSize = 0;
	unsigned char *pYUYVBuff;
	int heigth = 240;
	int width = 320;
	int byteColor = 2;
	int pos = -1 * width;
	int line = 1;
	pYUYVBuff = (unsigned char*)malloc(width * heigth * byteColor);
	memset(pYUYVBuff, 0, width * heigth * byteColor);
	

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
			pYUYVBuff[pos + (byteColor * heigth - line) * width] = Y;
			pYUYVBuff[pos + 1 + (byteColor * heigth - line) * width] = U;
			pYUYVBuff[pos + 2 + (byteColor * heigth - line) * width] = Y;
			dwUYVYSize += 3;
			pos += 3;
		}
		else
		{
			pYUYVBuff[pos + (byteColor * heigth - line) * width] = V;
			dwUYVYSize++;
			pos++;
		}

		if (pos == width)
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

	free(pYUYVBuff);
#endif
}
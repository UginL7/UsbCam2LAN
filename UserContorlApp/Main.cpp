#include <stdio.h>
#include <conio.h>
#include <windows.h>
#include <dshow.h>
#include <objbase.h>

#pragma comment(lib, "Strmiids.lib")

void main()
{
	HRESULT hr;
	IGraphBuilder *pGraphBuilder;
	ICaptureGraphBuilder2 *pCaptureGraphBuilder;
	ICreateDevEnum *pDevEnum;
	IEnumMoniker *pEnumMoniker;
	IMoniker *pMoniker;
	IPropertyBag *pBag;
	IBaseFilter *pBaseFilter;
	VARIANT var;

	// Инициализация для работы с COM
	hr = CoInitializeEx(NULL, COINIT_MULTITHREADED);
	if (hr != S_OK)
	{
		printf("Error CoInitializeEx");
		getch();
		return;
	}

	// Объект графа фильтров
	hr = CoCreateInstance(CLSID_FilterGraph, NULL, CLSCTX_INPROC_SERVER, IID_IGraphBuilder, (void **)&pGraphBuilder);
	if (FAILED(hr))
	{
		printf("Error CoCreateInstance - CLSID_FilterGraph");
		getch();
		return;
	}

	// Объект графа захвата
	hr = CoCreateInstance(CLSID_CaptureGraphBuilder2, NULL, CLSCTX_INPROC_SERVER, IID_ICaptureGraphBuilder2, (void **)&pCaptureGraphBuilder);
	if (FAILED(hr))
	{
		printf("Error CoCreateInstance - CLSID_CaptureGraphBuilder2");
		getch();
		return;
	}

	// Установка графа фильтров для использования в построении графа захвата
	hr = pCaptureGraphBuilder->SetFiltergraph(pGraphBuilder);
	if (FAILED(hr))
	{
		printf("Error SetFiltergraph");
		getch();
		return;
	}

	// Получение фильтров захвата
	hr = CoCreateInstance(CLSID_SystemDeviceEnum, NULL, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&pDevEnum));
	if (SUCCEEDED(hr))
	{
		hr = pDevEnum->CreateClassEnumerator(CLSID_VideoInputDeviceCategory, &pEnumMoniker, 0);
		if (hr == S_FALSE)
		{
			hr = VFW_E_NOT_FOUND;
		}
		pDevEnum->Release();
		/*pEnumMoniker->Release();*/
	}
	/// Перечисление устройств захвата
	if (SUCCEEDED(hr))
	{
		pMoniker = NULL;
		while (pEnumMoniker->Next(1, &pMoniker, NULL) == S_OK)
		{
			hr = pMoniker->BindToStorage(0, 0, IID_PPV_ARGS(&pBag));
			if (hr == S_FALSE)
			{
				pMoniker->Release();
				continue;
			}

			VariantInit(&var);

			// Дескриптор устройства подоходящего типа
			hr = pBag->Read(L"Description", &var, 0);
			if (FAILED(hr))
			{
				hr = pBag->Read(L"FriendlyName", &var, 0);
			}
			if (SUCCEEDED(hr))
			{
				printf("%S\n", var.bstrVal);
				VariantClear(&var);
			}

			hr = pBag->Read(L"DevicePath", &var, 0);
			if (SUCCEEDED(hr))
			{
				// The device path is not intended for display.
				printf("Device path: %S\n", var.bstrVal);
				VariantClear(&var);
			}

			hr = pMoniker->BindToObject(0, 0, IID_IBaseFilter, (void**)&pBaseFilter);
			if (!SUCCEEDED(hr))
			{
				printf("Error BindToObject");
				getch();
				return;
			}

			pBag->Release();
			pMoniker->Release();
		}
	}
	
	// добавление фильтра захвата в граф
	hr = pGraphBuilder->AddFilter(pBaseFilter, L"VideoCaptureFilter");
	if (FAILED(hr))
	{
		printf("Error AddFilter");
		getch();
		return;
	}
	
	hr = pCaptureGraphBuilder->RenderStream(&PIN_CATEGORY_CAPTURE, &MEDIATYPE_Video, pBaseFilter, NULL, NULL);
	if (FAILED(hr))
	{
		printf("Error RenderStream");
		getch();
		return;
	}

	IMediaControl *pMC = NULL;
	hr = pGraphBuilder->QueryInterface(IID_IMediaControl, (void **)&pMC);	
	pMC->Run();

	Sleep(INFINITE);
}